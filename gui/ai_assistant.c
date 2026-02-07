/*
 *  gretl -- Gnu Regression, Econometrics and Time-series Library
 *  Copyright (C) 2001 Allin Cottrell and Riccardo "Jack" Lucchetti
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "gretl.h"
#include "cmdstack.h"
#include "dialogs.h"
#include "textbuf.h"
#include "viewers.h"
#include "winstack.h"
#include "ai_assistant.h"
#include "objstack.h"
#include "modelprint.h"

typedef struct {
    windata_t *vwin;
    GtkWidget *provider_combo;
    GtkWidget *include_dataset;
    GtkWidget *include_last_error;
    GtkWidget *include_script;
    GtkWidget *enable_tools;
    GtkWidget *prompt_view;
    GtkWidget *reply_view;
    GtkWidget *ask_button;
    GtkWidget *copy_button;
    GtkWidget *insert_button;
    GtkWidget *status_label;
    char *last_reply;
    char *last_insert;
    gboolean busy;
} AiAssistant;

typedef struct {
    char *dataset;
    char *last_error;
    char *script_selection;
    char *script_full;
    char *command_log;
    char *last_model_simple;
    char *last_model_full;
} AiSnapshot;

typedef struct {
    char *name;
    int n_lines;
    char *style;
} AiToolCall;

typedef struct {
    char *assistant_text;
    char *proposed_insert;
    GPtrArray *tool_calls; /* of AiToolCall* */
} AiLLMReply;

typedef struct {
    GtkWidget *window;
    AiAssistant *asst;
    GretlLLMProvider provider;
    char *prompt;
    char *reply;
    char *insert_text;
    gboolean tools_enabled;
    AiSnapshot snapshot;
} AiJob;

static AiAssistant *global_asst;

static void ai_snapshot_clear (AiSnapshot *snap)
{
    if (snap == NULL) {
        return;
    }

    g_free(snap->dataset);
    g_free(snap->last_error);
    g_free(snap->script_selection);
    g_free(snap->script_full);
    g_free(snap->command_log);
    g_free(snap->last_model_simple);
    g_free(snap->last_model_full);
    memset(snap, 0, sizeof *snap);
}

static void ai_tool_call_free (AiToolCall *tc)
{
    if (tc == NULL) {
        return;
    }
    g_free(tc->name);
    g_free(tc->style);
    g_free(tc);
}

static void ai_llm_reply_free (AiLLMReply *r)
{
    if (r == NULL) {
        return;
    }
    g_free(r->assistant_text);
    g_free(r->proposed_insert);
    if (r->tool_calls != NULL) {
        g_ptr_array_free(r->tool_calls, TRUE);
    }
    g_free(r);
}

static windata_t *find_active_script_editor (void)
{
    GList *wins, *p;
    windata_t *fallback = NULL;

    wins = gtk_window_list_toplevels();
    for (p = wins; p != NULL; p = p->next) {
        GtkWidget *w = p->data;
        windata_t *vwin;

        if (!GTK_IS_WINDOW(w)) {
            continue;
        }

        vwin = g_object_get_data(G_OBJECT(w), "vwin");
        if (vwin == NULL) {
            continue;
        }

        if (!vwin_editing_script(vwin->role) || vwin->text == NULL) {
            continue;
        }

        if (gtk_window_is_active(GTK_WINDOW(w))) {
            g_list_free(wins);
            return vwin;
        }

        if (fallback == NULL) {
            fallback = vwin;
        }
    }

    g_list_free(wins);
    return fallback;
}

static char *get_prompt_text (GtkWidget *view)
{
    GtkTextBuffer *buf;
    GtkTextIter start, end;

    buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_get_bounds(buf, &start, &end);
    return gtk_text_buffer_get_text(buf, &start, &end, FALSE);
}

static void set_view_text (GtkWidget *view, const char *s)
{
    GtkTextBuffer *buf;

    buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_set_text(buf, (s != NULL)? s : "", -1);
}

static gchar *dataset_context_string (void)
{
    GString *gs;
    int i, vmax;

    if (dataset == NULL || dataset->v <= 0 || dataset->n <= 0) {
        return g_strdup("[Dataset]\n(no dataset loaded)\n");
    }

    gs = g_string_new("[Dataset]\n");
    g_string_append_printf(gs, "nobs=%d, vars=%d, pd=%d, sample=%s..%s\n",
                           dataset->n, dataset->v, dataset->pd,
                           dataset->stobs, dataset->endobs);

    g_string_append(gs, "vars: ");
    vmax = dataset->v;
    if (vmax > 30) {
        vmax = 30;
    }
    for (i = 1; i < vmax; i++) {
        if (dataset->varname != NULL && dataset->varname[i] != NULL) {
            g_string_append(gs, dataset->varname[i]);
            if (i < vmax - 1) {
                g_string_append(gs, ", ");
            }
        }
    }
    if (dataset->v > vmax) {
        g_string_append(gs, ", ...");
    }
    g_string_append(gs, "\n");

    return g_string_free(gs, FALSE);
}

static gchar *last_error_context_string (void)
{
    const char *msg = gui_get_last_error_message();

    if (msg == NULL || *msg == '\0') {
        msg = gui_get_last_warning_message();
    }

    if (msg == NULL || *msg == '\0') {
        return g_strdup("[Last error]\n(none)\n");
    } else {
        return g_strdup_printf("[Last error]\n%s\n", msg);
    }
}

static gchar *script_context_string (void)
{
    windata_t *vwin = find_active_script_editor();
    gboolean sel = FALSE;
    gchar *txt;
    gchar *out;
    gsize n;

    if (vwin == NULL || vwin->text == NULL) {
        return g_strdup("[Script]\n(no active script editor)\n");
    }

    txt = textview_get_selection_or_all(vwin->text, &sel);
    if (txt == NULL) {
        return g_strdup("[Script]\n(unavailable)\n");
    }

    n = strlen(txt);
    if (n > 32000) {
        txt[32000] = '\0';
        out = g_strdup_printf("[Script] (%s; truncated)\n%s\n",
                              sel ? "selection" : "full",
                              txt);
    } else {
        out = g_strdup_printf("[Script] (%s)\n%s\n",
                              sel ? "selection" : "full",
                              txt);
    }

    g_free(txt);
    return out;
}

static gchar *script_selection_string (void)
{
    windata_t *vwin = find_active_script_editor();
    GtkTextBuffer *tbuf;
    GtkTextIter start, end;
    gchar *txt;
    gchar *out;
    gsize n;

    if (vwin == NULL || vwin->text == NULL) {
        return g_strdup("(no active script editor)\n");
    }

    tbuf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(vwin->text));
    if (tbuf == NULL) {
        return g_strdup("(unavailable)\n");
    }

    if (!gtk_text_buffer_get_selection_bounds(tbuf, &start, &end)) {
        return g_strdup("(no selection)\n");
    }

    txt = gtk_text_buffer_get_text(tbuf, &start, &end, FALSE);
    if (txt == NULL) {
        return g_strdup("(unavailable)\n");
    }

    n = strlen(txt);
    if (n > 32000) {
        txt[32000] = '\0';
    }

    out = g_strdup(txt);
    g_free(txt);
    return out;
}

static gchar *script_full_string (void)
{
    windata_t *vwin = find_active_script_editor();
    GtkTextBuffer *tbuf;
    GtkTextIter start, end;
    gchar *txt;
    gchar *out;
    gsize n;

    if (vwin == NULL || vwin->text == NULL) {
        return g_strdup("(no active script editor)\n");
    }

    tbuf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(vwin->text));
    if (tbuf == NULL) {
        return g_strdup("(unavailable)\n");
    }

    gtk_text_buffer_get_start_iter(tbuf, &start);
    gtk_text_buffer_get_end_iter(tbuf, &end);
    txt = gtk_text_buffer_get_text(tbuf, &start, &end, FALSE);
    if (txt == NULL) {
        return g_strdup("(unavailable)\n");
    }

    n = strlen(txt);
    if (n > 32000) {
        txt[32000] = '\0';
    }

    out = g_strdup(txt);
    g_free(txt);
    return out;
}

static gchar *tail_n_lines (const char *s, int n_lines)
{
    const char *p;
    int n = 0;
    gsize len;
    gchar *out;

    if (s == NULL) {
        return g_strdup("(none)\n");
    }
    if (n_lines <= 0) {
        n_lines = 50;
    }

    len = strlen(s);
    p = s + len;

    while (p > s) {
        p--;
        if (*p == '\n') {
            n++;
            if (n > n_lines) {
                p++;
                break;
            }
        }
    }

    out = g_strdup(p);
    if (out != NULL && strlen(out) > 32000) {
        out[32000] = '\0';
    }
    return out;
}

static gchar *command_log_string (void)
{
    gchar *log;
    int err = 0;

    log = get_logfile_content(&err);
    if (err || log == NULL) {
        g_free(log);
        return g_strdup("(unavailable)\n");
    }

    if (strlen(log) > 200000) {
        log[200000] = '\0';
    }

    return log;
}

static gchar *last_model_summary_string (gboolean simple)
{
    GretlObjType type = GRETL_OBJ_NULL;
    void *ptr = get_last_model(&type);
    PRN *prn;
    gretlopt opt;
    int err = 0;

    if (ptr == NULL) {
        return g_strdup("(none)\n");
    }

    if (type != GRETL_OBJ_EQN) {
        return g_strdup_printf("(last model type %d not supported)\n", type);
    }

    prn = gretl_print_new(GRETL_PRINT_BUFFER, &err);
    if (prn == NULL || err) {
        gretl_print_destroy(prn);
        return g_strdup("(unavailable)\n");
    }

    opt = simple ? OPT_S : OPT_NONE;
    err = printmodel((MODEL *) ptr, dataset, opt, prn);
    if (err) {
        gretl_print_destroy(prn);
        return g_strdup("(unavailable)\n");
    }

    return gretl_print_steal_buffer(prn);
}

static GretlLLMProvider selected_provider (AiAssistant *asst)
{
    gint idx = gtk_combo_box_get_active(GTK_COMBO_BOX(asst->provider_combo));

    if (idx == 1) {
        return GRETL_LLM_GEMINI;
    } else {
        return GRETL_LLM_CODEX;
    }
}

static char *build_full_prompt (AiAssistant *asst, const char *user_prompt)
{
    GString *gs = g_string_new(NULL);

    g_string_append(gs,
        "You are an assistant embedded in the gretl GUI. "
        "Be concise. If you propose code, output plain hansl without Markdown fences.\n\n"
    );

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(asst->enable_tools))) {
        g_string_append(gs,
            "Return ONLY a single JSON object with this schema:\n"
            "{\"assistant_text\": \"...\", \"proposed_insert\": \"...\", \"tool_calls\": [{\"name\":\"...\",\"args\":{...}}]}\n"
            "If you do not need tools, set tool_calls to [].\n"
            "Available read-only tools:\n"
            "- get_dataset_summary\n"
            "- get_last_error\n"
            "- get_script_selection\n"
            "- get_script_full\n"
            "- get_command_log_tail (args: {\"n_lines\": 50})\n"
            "- get_last_model_summary (args: {\"style\": \"simple\"|\"full\"})\n"
            "Do not include Markdown fences.\n\n"
        );
    }

    g_string_append(gs, "User request:\n");
    g_string_append(gs, (user_prompt != NULL)? user_prompt : "");
    g_string_append(gs, "\n\n");

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(asst->include_dataset))) {
        gchar *ctx = dataset_context_string();
        g_string_append(gs, ctx);
        g_string_append(gs, "\n");
        g_free(ctx);
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(asst->include_last_error))) {
        gchar *ctx = last_error_context_string();
        g_string_append(gs, ctx);
        g_string_append(gs, "\n");
        g_free(ctx);
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(asst->include_script))) {
        gchar *ctx = script_context_string();
        g_string_append(gs, ctx);
        g_string_append(gs, "\n");
        g_free(ctx);
    }

    return g_string_free(gs, FALSE);
}

static const char *json_skip_ws (const char *p)
{
    while (p != NULL && *p != '\0' && g_ascii_isspace(*p)) {
        p++;
    }
    return p;
}

static gboolean json_parse_string (const char **pp, char **out)
{
    const char *p = *pp;
    GString *gs;
    gboolean esc = FALSE;

    p = json_skip_ws(p);
    if (p == NULL || *p != '\"') {
        return FALSE;
    }
    p++;

    gs = g_string_new(NULL);
    while (*p != '\0') {
        char c = *p++;

        if (esc) {
            esc = FALSE;
            switch (c) {
            case '\"': g_string_append_c(gs, '\"'); break;
            case '\\': g_string_append_c(gs, '\\'); break;
            case '/': g_string_append_c(gs, '/'); break;
            case 'b': g_string_append_c(gs, '\b'); break;
            case 'f': g_string_append_c(gs, '\f'); break;
            case 'n': g_string_append_c(gs, '\n'); break;
            case 'r': g_string_append_c(gs, '\r'); break;
            case 't': g_string_append_c(gs, '\t'); break;
            case 'u':
                /* best-effort: keep ASCII \u00XX, else '?' */
                if (g_ascii_isxdigit(p[0]) && g_ascii_isxdigit(p[1]) &&
                    g_ascii_isxdigit(p[2]) && g_ascii_isxdigit(p[3])) {
                    if (p[0] == '0' && p[1] == '0') {
                        int hi = g_ascii_xdigit_value(p[2]);
                        int lo = g_ascii_xdigit_value(p[3]);
                        g_string_append_c(gs, (char) ((hi << 4) | lo));
                    } else {
                        g_string_append_c(gs, '?');
                    }
                    p += 4;
                } else {
                    g_string_append_c(gs, '?');
                }
                break;
            default:
                g_string_append_c(gs, c);
                break;
            }
            continue;
        }

        if (c == '\\') {
            esc = TRUE;
            continue;
        }
        if (c == '\"') {
            *pp = p;
            *out = g_string_free(gs, FALSE);
            return TRUE;
        }
        g_string_append_c(gs, c);
    }

    g_string_free(gs, TRUE);
    return FALSE;
}

static gboolean json_parse_int (const char **pp, int *out)
{
    const char *p = json_skip_ws(*pp);
    char *endp = NULL;
    long v;

    if (p == NULL || *p == '\0') {
        return FALSE;
    }

    v = strtol(p, &endp, 10);
    if (endp == p) {
        return FALSE;
    }

    *pp = endp;
    *out = (int) v;
    return TRUE;
}

static const char *json_match_closing (const char *p, char open, char close)
{
    int depth = 0;
    gboolean in_str = FALSE;
    gboolean esc = FALSE;

    if (p == NULL || *p != open) {
        return NULL;
    }

    for (; *p != '\0'; p++) {
        char c = *p;

        if (in_str) {
            if (esc) {
                esc = FALSE;
            } else if (c == '\\') {
                esc = TRUE;
            } else if (c == '\"') {
                in_str = FALSE;
            }
            continue;
        }

        if (c == '\"') {
            in_str = TRUE;
            continue;
        }

        if (c == open) {
            depth++;
        } else if (c == close) {
            depth--;
            if (depth == 0) {
                return p;
            }
        }
    }

    return NULL;
}

static const char *json_find_field_value_start (const char *json, const char *field)
{
    gchar *pat;
    const char *p;

    if (json == NULL || field == NULL) {
        return NULL;
    }

    pat = g_strdup_printf("\"%s\"", field);
    p = json;
    while ((p = strstr(p, pat)) != NULL) {
        const char *q = p + strlen(pat);
        q = json_skip_ws(q);
        if (q != NULL && *q == ':') {
            q++;
            q = json_skip_ws(q);
            g_free(pat);
            return q;
        }
        p = p + 1;
    }

    g_free(pat);
    return NULL;
}

static gboolean json_extract_string_field (const char *json, const char *field, char **out)
{
    const char *p = json_find_field_value_start(json, field);
    char *s = NULL;

    if (p == NULL) {
        return FALSE;
    }
    if (!json_parse_string(&p, &s)) {
        return FALSE;
    }

    *out = s;
    return TRUE;
}

static gboolean json_extract_object_span (const char *json, const char *field,
                                         const char **pstart, const char **pend)
{
    const char *p = json_find_field_value_start(json, field);
    const char *q;

    if (p == NULL) {
        return FALSE;
    }
    p = json_skip_ws(p);
    if (p == NULL || *p != '{') {
        return FALSE;
    }

    q = json_match_closing(p, '{', '}');
    if (q == NULL) {
        return FALSE;
    }

    *pstart = p;
    *pend = q;
    return TRUE;
}

static gboolean json_extract_array_span (const char *json, const char *field,
                                        const char **pstart, const char **pend)
{
    const char *p = json_find_field_value_start(json, field);
    const char *q;

    if (p == NULL) {
        return FALSE;
    }
    p = json_skip_ws(p);
    if (p == NULL || *p != '[') {
        return FALSE;
    }

    q = json_match_closing(p, '[', ']');
    if (q == NULL) {
        return FALSE;
    }

    *pstart = p;
    *pend = q;
    return TRUE;
}

static AiLLMReply *parse_llm_json_reply (const char *json)
{
    AiLLMReply *r;
    const char *astart, *aend;
    const char *p;

    if (json == NULL) {
        return NULL;
    }

    r = g_malloc0(sizeof *r);
    r->tool_calls = g_ptr_array_new_with_free_func((GDestroyNotify) ai_tool_call_free);

    json_extract_string_field(json, "assistant_text", &r->assistant_text);
    json_extract_string_field(json, "proposed_insert", &r->proposed_insert);

    if (!json_extract_array_span(json, "tool_calls", &astart, &aend)) {
        return r;
    }

    p = astart + 1;
    while (p < aend) {
        const char *obj_end;
        char *obj;
        char *name = NULL;
        AiToolCall *tc;
        const char *args_start, *args_end;

        p = json_skip_ws(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p != '{') {
            break;
        }

        obj_end = json_match_closing(p, '{', '}');
        if (obj_end == NULL || obj_end > aend) {
            break;
        }

        obj = g_strndup(p, (obj_end - p) + 1);
        if (!json_extract_string_field(obj, "name", &name) || name == NULL) {
            g_free(obj);
            p = obj_end + 1;
            continue;
        }

        tc = g_malloc0(sizeof *tc);
        tc->name = name;
        tc->n_lines = 50;

        if (json_extract_object_span(obj, "args", &args_start, &args_end)) {
            char *args = g_strndup(args_start, (args_end - args_start) + 1);
            const char *vp;

            if (!strcmp(tc->name, "get_command_log_tail")) {
                vp = json_find_field_value_start(args, "n_lines");
                if (vp != NULL) {
                    json_parse_int(&vp, &tc->n_lines);
                }
            } else if (!strcmp(tc->name, "get_last_model_summary")) {
                json_extract_string_field(args, "style", &tc->style);
            }
            g_free(args);
        }

        g_ptr_array_add(r->tool_calls, tc);
        g_free(obj);
        p = obj_end + 1;
    }

    return r;
}

static char *format_llm_reply_for_display (const AiLLMReply *r, const char *fallback)
{
    GString *gs;

    if (r == NULL) {
        return g_strdup((fallback != NULL) ? fallback : "");
    }

    gs = g_string_new(NULL);

    if (r->assistant_text != NULL && *r->assistant_text != '\0') {
        g_string_append(gs, r->assistant_text);
    }

    if (r->proposed_insert != NULL && *r->proposed_insert != '\0') {
        if (gs->len > 0) {
            g_string_append(gs, "\n\n");
        }
        g_string_append(gs, "[Proposed script]\n");
        g_string_append(gs, r->proposed_insert);
        if (gs->len > 0 && gs->str[gs->len - 1] != '\n') {
            g_string_append_c(gs, '\n');
        }
    }

    if (gs->len == 0 && fallback != NULL) {
        g_string_append(gs, fallback);
    }

    return g_string_free(gs, FALSE);
}

static char *execute_tool_calls (const AiLLMReply *r, const AiSnapshot *snap)
{
    GString *gs;
    guint i, n;

    if (r == NULL || r->tool_calls == NULL || snap == NULL) {
        return NULL;
    }

    n = r->tool_calls->len;
    if (n == 0) {
        return NULL;
    }
    if (n > 8) {
        n = 8;
    }

    gs = g_string_new(NULL);
    for (i = 0; i < n; i++) {
        const AiToolCall *tc = g_ptr_array_index(r->tool_calls, i);
        const char *out = NULL;

        if (tc == NULL || tc->name == NULL) {
            continue;
        }

        if (!strcmp(tc->name, "get_dataset_summary")) {
            out = snap->dataset;
        } else if (!strcmp(tc->name, "get_last_error")) {
            out = snap->last_error;
        } else if (!strcmp(tc->name, "get_script_selection")) {
            out = snap->script_selection;
        } else if (!strcmp(tc->name, "get_script_full")) {
            out = snap->script_full;
        } else if (!strcmp(tc->name, "get_command_log_tail")) {
            if (snap->command_log != NULL) {
                gchar *tail = tail_n_lines(snap->command_log, tc->n_lines);
                g_string_append_printf(gs, "--- tool:%s ---\n", tc->name);
                g_string_append(gs, tail);
                if (gs->len > 0 && gs->str[gs->len - 1] != '\n') {
                    g_string_append_c(gs, '\n');
                }
                g_string_append(gs, "--- end ---\n");
                g_free(tail);
                continue;
            }
        } else if (!strcmp(tc->name, "get_last_model_summary")) {
            if (tc->style != NULL && !strcmp(tc->style, "full")) {
                out = snap->last_model_full;
            } else {
                out = snap->last_model_simple;
            }
        }

        g_string_append_printf(gs, "--- tool:%s ---\n", tc->name);
        if (out != NULL) {
            g_string_append(gs, out);
            if (gs->len > 0 && gs->str[gs->len - 1] != '\n') {
                g_string_append_c(gs, '\n');
            }
        } else {
            g_string_append(gs, "(unavailable)\n");
        }
        g_string_append(gs, "--- end ---\n");
    }

    if (gs->len > 40000) {
        g_string_truncate(gs, 40000);
        g_string_append(gs, "\n...[truncated]...\n");
    }

    return g_string_free(gs, FALSE);
}

static gboolean job_complete_idle (gpointer p)
{
    AiJob *job = p;
    AiAssistant *asst = job->asst;

    if (global_asst != asst || asst == NULL) {
        g_object_unref(job->window);
        g_free(job->reply);
        g_free(job->prompt);
        g_free(job);
        return FALSE;
    }

    asst->busy = FALSE;
    gtk_widget_set_sensitive(asst->ask_button, TRUE);
    gtk_widget_set_sensitive(asst->copy_button, TRUE);
    gtk_widget_set_sensitive(asst->insert_button, TRUE);

    set_view_text(asst->reply_view, (job->reply != NULL)? job->reply : "");

    g_free(asst->last_reply);
    asst->last_reply = g_strdup((job->reply != NULL)? job->reply : "");

    g_free(asst->last_insert);
    asst->last_insert = g_strdup((job->insert_text != NULL)? job->insert_text : "");

    gtk_label_set_text(GTK_LABEL(asst->status_label), "");

    g_object_unref(job->window);
    g_free(job->reply);
    g_free(job->insert_text);
    g_free(job->prompt);
    ai_snapshot_clear(&job->snapshot);
    g_free(job);

    return FALSE;
}

static gpointer job_thread (gpointer p)
{
    AiJob *job = p;
    char *reply = NULL;
    char *insert_text = NULL;
    char *errmsg = NULL;
    int err;
    int iter;
    GString *tool_log = NULL;

    if (job->tools_enabled) {
        tool_log = g_string_new(NULL);
    }

    for (iter = 0; iter < (job->tools_enabled ? 2 : 1); iter++) {
        char *full_prompt = NULL;
        AiLLMReply *r = NULL;
        char *tool_results = NULL;
        char *display = NULL;

        if (job->tools_enabled && tool_log != NULL && tool_log->len > 0) {
            full_prompt = g_strdup_printf("%s\n\nTool results:\n%s\n\nNow respond using the JSON schema.\n",
                                          job->prompt, tool_log->str);
        } else {
            full_prompt = g_strdup(job->prompt);
        }

        g_free(reply);
        reply = NULL;
        g_free(errmsg);
        errmsg = NULL;

        err = gretl_llm_complete_with_error(job->provider, full_prompt, &reply, &errmsg);
        g_free(full_prompt);

        if (err || reply == NULL) {
            if (errmsg != NULL) {
                g_free(reply);
                reply = g_strdup(errmsg);
            } else {
                g_free(reply);
                reply = g_strdup("LLM call failed");
            }
            break;
        }

        if (!job->tools_enabled) {
            break;
        }

        r = parse_llm_json_reply(reply);
        if (r == NULL) {
            break;
        }

        if (r->tool_calls != NULL && r->tool_calls->len > 0 && iter == 0) {
            tool_results = execute_tool_calls(r, &job->snapshot);
            if (tool_results != NULL && tool_log != NULL) {
                g_string_append(tool_log, tool_results);
            }
            g_free(tool_results);
            ai_llm_reply_free(r);
            continue;
        }

        if (r->proposed_insert != NULL && *r->proposed_insert != '\0') {
            g_free(insert_text);
            insert_text = g_strdup(r->proposed_insert);
        }

        display = format_llm_reply_for_display(r, reply);
        g_free(reply);
        reply = display;
        ai_llm_reply_free(r);
        break;
    }

    if (tool_log != NULL) {
        g_string_free(tool_log, TRUE);
    }

    job->reply = reply;
    job->insert_text = insert_text;
    g_idle_add(job_complete_idle, job);

    g_free(errmsg);
    return NULL;
}

static void ask_clicked (GtkWidget *w, gpointer p)
{
    AiAssistant *asst = p;
    char *user_prompt;
    char *full_prompt;
    AiJob *job;

    if (asst == NULL || asst->busy) {
        return;
    }

    user_prompt = get_prompt_text(asst->prompt_view);
    if (user_prompt == NULL || *user_prompt == '\0') {
        g_free(user_prompt);
        return;
    }

    full_prompt = build_full_prompt(asst, user_prompt);
    g_free(user_prompt);

    asst->busy = TRUE;
    gtk_widget_set_sensitive(asst->ask_button, FALSE);
    gtk_widget_set_sensitive(asst->copy_button, FALSE);
    gtk_widget_set_sensitive(asst->insert_button, FALSE);
    gtk_label_set_text(GTK_LABEL(asst->status_label), _("Working..."));

    job = g_malloc0(sizeof *job);
    job->window = g_object_ref(asst->vwin->main);
    job->asst = asst;
    job->provider = selected_provider(asst);
    job->prompt = full_prompt;
    job->reply = NULL;
    job->insert_text = NULL;
    job->tools_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(asst->enable_tools));

    job->snapshot.dataset = dataset_context_string();
    job->snapshot.last_error = last_error_context_string();
    job->snapshot.script_selection = script_selection_string();
    job->snapshot.script_full = script_full_string();
    job->snapshot.command_log = command_log_string();
    job->snapshot.last_model_simple = last_model_summary_string(TRUE);
    job->snapshot.last_model_full = last_model_summary_string(FALSE);

    g_thread_new("gretl-ai", job_thread, job);
}

static void copy_clicked (GtkWidget *w, gpointer p)
{
    AiAssistant *asst = p;
    GtkClipboard *cb;

    if (asst == NULL || asst->last_reply == NULL) {
        return;
    }

    cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(cb, asst->last_reply, -1);
}

static void insert_clicked (GtkWidget *w, gpointer p)
{
    AiAssistant *asst = p;
    windata_t *vwin;
    int resp;
    const char *txt;

    if (asst == NULL) {
        return;
    }

    txt = (asst->last_insert != NULL && *asst->last_insert != '\0') ?
        asst->last_insert : asst->last_reply;

    if (txt == NULL || *txt == '\0') {
        return;
    }

    vwin = find_active_script_editor();
    if (vwin == NULL || vwin->text == NULL) {
        infobox(_("No active script editor window was found."));
        return;
    }

    resp = yes_no_dialog(_("gretl: insert text"),
                         _("Insert the assistant reply into the active script editor?"),
                         vwin_toplevel(vwin));
    if (resp != GRETL_YES) {
        return;
    }

    textview_insert_text(vwin->text, txt);
}

static void ai_destroyed (GtkWidget *w, gpointer p)
{
    AiAssistant *asst = p;

    if (asst != NULL) {
        g_free(asst->last_reply);
        g_free(asst->last_insert);
        g_free(asst);
    }

    global_asst = NULL;
}

static GtkWidget *make_scrolled_text_view (GtkWidget **pview, gboolean editable)
{
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *view = gtk_text_view_new();

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), editable);
    gtk_container_add(GTK_CONTAINER(sw), view);

    if (pview != NULL) {
        *pview = view;
    }

    return sw;
}

static void ai_build_ui (AiAssistant *asst)
{
    GtkWidget *vbox = asst->vwin->vbox;
    GtkWidget *hbox;
    GtkWidget *lab;
    GtkWidget *paned;
    GtkWidget *sw;
    GtkWidget *bbox;
    GtkWidget *b;

    hbox = gtk_hbox_new(FALSE, 6);
    lab = gtk_label_new(_("Provider:"));
    gtk_box_pack_start(GTK_BOX(hbox), lab, FALSE, FALSE, 0);

    asst->provider_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(asst->provider_combo), "codex");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(asst->provider_combo), "gemini");
    gtk_combo_box_set_active(GTK_COMBO_BOX(asst->provider_combo), 0);
    gtk_box_pack_start(GTK_BOX(hbox), asst->provider_combo, FALSE, FALSE, 0);

    asst->include_dataset = gtk_check_button_new_with_label(_("Include dataset summary"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(asst->include_dataset), TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), asst->include_dataset, FALSE, FALSE, 0);

    asst->include_last_error = gtk_check_button_new_with_label(_("Include last error"));
    gtk_box_pack_start(GTK_BOX(hbox), asst->include_last_error, FALSE, FALSE, 0);

    asst->include_script = gtk_check_button_new_with_label(_("Include script selection"));
    gtk_box_pack_start(GTK_BOX(hbox), asst->include_script, FALSE, FALSE, 0);

    asst->enable_tools = gtk_check_button_new_with_label(_("Enable tools (read-only)"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(asst->enable_tools), TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), asst->enable_tools, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    paned = gtk_vpaned_new();
    gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);

    sw = make_scrolled_text_view(&asst->prompt_view, TRUE);
    gtk_paned_add1(GTK_PANED(paned), sw);

    sw = make_scrolled_text_view(&asst->reply_view, FALSE);
    gtk_paned_add2(GTK_PANED(paned), sw);

    gtk_paned_set_position(GTK_PANED(paned), 160);

    bbox = gtk_hbox_new(FALSE, 6);

    b = gtk_button_new_with_label(_("Ask"));
    g_signal_connect(G_OBJECT(b), "clicked", G_CALLBACK(ask_clicked), asst);
    gtk_box_pack_start(GTK_BOX(bbox), b, FALSE, FALSE, 0);
    asst->ask_button = b;

    b = gtk_button_new_with_label(_("Copy reply"));
    g_signal_connect(G_OBJECT(b), "clicked", G_CALLBACK(copy_clicked), asst);
    gtk_box_pack_start(GTK_BOX(bbox), b, FALSE, FALSE, 0);
    asst->copy_button = b;

    b = gtk_button_new_with_label(_("Insert into script"));
    g_signal_connect(G_OBJECT(b), "clicked", G_CALLBACK(insert_clicked), asst);
    gtk_box_pack_start(GTK_BOX(bbox), b, FALSE, FALSE, 0);
    asst->insert_button = b;

    asst->status_label = gtk_label_new("");
    gtk_box_pack_end(GTK_BOX(bbox), asst->status_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 0);

    gtk_widget_show_all(vbox);
}

void show_ai_assistant (void)
{
    AiAssistant *asst;

    if (global_asst != NULL && global_asst->vwin != NULL) {
        gretl_viewer_present(global_asst->vwin);
        return;
    }

    asst = g_malloc0(sizeof *asst);
    asst->vwin = gretl_viewer_new(VIEW_FILE, _("gretl: AI assistant"), NULL);
    if (asst->vwin == NULL) {
        g_free(asst);
        return;
    }

    gtk_window_set_default_size(GTK_WINDOW(asst->vwin->main), 780, 520);
    g_signal_connect(G_OBJECT(asst->vwin->main), "destroy",
                     G_CALLBACK(ai_destroyed), asst);

    global_asst = asst;
    ai_build_ui(asst);

    /* gretl viewers typically show the vbox and the toplevel explicitly */
    gtk_widget_show(asst->vwin->vbox);
    if (asst->vwin->main != asst->vwin->vbox) {
        gtk_widget_show(asst->vwin->main);
    }
    gretl_viewer_present(asst->vwin);
}
