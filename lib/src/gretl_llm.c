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

#include "libgretl.h"
#include "gretl_llm.h"

#include <glib/gstdio.h>

#ifndef WIN32
# include <unistd.h>
#endif

#define GRETL_LLM_MAX_REPLY (2 * 1024 * 1024)
#define GRETL_LLM_DEFAULT_TIMEOUT_SEC 300

static const char *default_codex_bin (void)
{
    return "/home/terry/.nvm/versions/node/v24.5.0/bin/codex";
}

static const char *default_gemini_bin (void)
{
    return "/home/terry/.nvm/versions/node/v24.5.0/bin/gemini";
}

static int llm_timeout_seconds (void)
{
    const char *s = getenv("GRETL_LLM_TIMEOUT_SEC");
    long v;
    char *endp = NULL;

    if (s == NULL || *s == '\0') {
        return GRETL_LLM_DEFAULT_TIMEOUT_SEC;
    }

    v = strtol(s, &endp, 10);
    if (endp == s || v <= 0 || v > 3600) {
        return GRETL_LLM_DEFAULT_TIMEOUT_SEC;
    }

    return (int) v;
}

static gboolean codex_dangerous_default (void)
{
    const char *s = getenv("GRETL_LLM_UNSAFE");
    if (s != NULL && *s != '\0' && strcmp(s, "0")) {
        return TRUE;
    }
    s = getenv("GRETL_CODEX_DANGEROUS");
    if (s != NULL && *s != '\0' && strcmp(s, "0")) {
        return TRUE;
    }
    return FALSE;
}

static gchar **argv_wrap_timeout (gchar *const *base_argv)
{
#ifdef WIN32
    return g_strdupv((gchar **) base_argv);
#else
    gchar *timeout_bin;
    gchar **argv;
    gchar *dur;
    int i, n, j;

    timeout_bin = g_find_program_in_path("timeout");
    if (timeout_bin == NULL) {
        return g_strdupv((gchar **) base_argv);
    }

    dur = g_strdup_printf("%ds", llm_timeout_seconds());

    n = g_strv_length((gchar **) base_argv);
    argv = g_new0(gchar *, n + 4);

    j = 0;
    argv[j++] = timeout_bin;
    argv[j++] = g_strdup("--signal=KILL");
    argv[j++] = dur;
    for (i = 0; i < n; i++) {
        argv[j++] = g_strdup(base_argv[i]);
    }
    argv[j] = NULL;

    return argv;
#endif
}

static void free_spawn_argv (gchar **argv)
{
    g_strfreev(argv);
}

static int check_child_status_buf (const char *prog,
                                   gint status,
                                   const gchar *stdout_buf,
                                   const gchar *stderr_buf,
                                   char **errmsg)
{
    GError *gerr = NULL;

    if (g_spawn_check_exit_status(status, &gerr)) {
        return 0;
    }

    if (errmsg != NULL) {
        if (gerr != NULL && gerr->message != NULL) {
            *errmsg = g_strdup_printf("%s failed: %s", prog, gerr->message);
        } else {
            *errmsg = g_strdup_printf("%s failed", prog);
        }

        if (status == (124 << 8) || status == (137 << 8)) {
            gchar *tmp = *errmsg;
            *errmsg = g_strdup_printf("%s (timed out; set GRETL_LLM_TIMEOUT_SEC)", tmp);
            g_free(tmp);
        }

        if (stderr_buf != NULL && *stderr_buf != '\0') {
            gchar *tmp = *errmsg;
            *errmsg = g_strdup_printf("%s\n\nstderr:\n%s", tmp, stderr_buf);
            g_free(tmp);
        } else if (stdout_buf != NULL && *stdout_buf != '\0') {
            gchar *tmp = *errmsg;
            *errmsg = g_strdup_printf("%s\n\nstdout:\n%s", tmp, stdout_buf);
            g_free(tmp);
        }
    }

    if (gerr != NULL) {
        g_error_free(gerr);
    }
    return E_EXTERNAL;
}

static gchar *find_executable (GretlLLMProvider provider)
{
    const char *envvar = NULL;
    const char *fallback = NULL;
    gchar *path = NULL;

    if (provider == GRETL_LLM_CODEX) {
        envvar = "GRETL_CODEX_BIN";
        fallback = default_codex_bin();
    } else if (provider == GRETL_LLM_GEMINI) {
        envvar = "GRETL_GEMINI_BIN";
        fallback = default_gemini_bin();
    } else {
        return NULL;
    }

    if (envvar != NULL) {
        const char *s = getenv(envvar);
        if (s != NULL && *s != '\0') {
            return g_strdup(s);
        }
    }

    if (fallback != NULL && g_file_test(fallback, G_FILE_TEST_IS_EXECUTABLE)) {
        return g_strdup(fallback);
    }

    if (provider == GRETL_LLM_CODEX) {
        path = g_find_program_in_path("codex");
    } else if (provider == GRETL_LLM_GEMINI) {
        path = g_find_program_in_path("gemini");
    }

    return path;
}

static int set_spawn_error (const char *prefix, const GError *gerr)
{
    if (gerr != NULL && gerr->message != NULL) {
        gretl_errmsg_sprintf("%s: %s", prefix, gerr->message);
    } else {
        gretl_errmsg_set(prefix);
    }
    return E_EXTERNAL;
}

static int set_spawn_error_buf (const char *prefix,
                                const GError *gerr,
                                char **errmsg)
{
    if (errmsg == NULL) {
        return set_spawn_error(prefix, gerr);
    }

    if (gerr != NULL && gerr->message != NULL) {
        *errmsg = g_strdup_printf("%s: %s", prefix, gerr->message);
    } else {
        *errmsg = g_strdup(prefix);
    }

    return E_EXTERNAL;
}

static char *strip_to_json (const char *s)
{
    const char *p0;
    const char *p1;

    if (s == NULL) {
        return NULL;
    }

    p0 = strchr(s, '{');
    p1 = strrchr(s, '}');
    if (p0 == NULL || p1 == NULL || p1 <= p0) {
        return NULL;
    }

    return g_strndup(p0, (gsize) (p1 - p0 + 1));
}

static int hexval (char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static gboolean append_utf8_from_codepoint (GString *gs, unsigned int u)
{
    gchar buf[8];
    gint n;

    if (u > 0x10FFFF) {
        return FALSE;
    }

    n = g_unichar_to_utf8((gunichar) u, buf);
    if (n <= 0) {
        return FALSE;
    }

    g_string_append_len(gs, buf, n);
    return TRUE;
}

static char *json_unescape_string (const char *s, int len)
{
    const char *p = s;
    const char *pe = s + len;
    GString *gs;

    gs = g_string_sized_new((gsize) len);

    while (p < pe) {
        if (*p != '\\') {
            g_string_append_c(gs, *p);
            p++;
            continue;
        }

        p++;
        if (p >= pe) {
            break;
        }

        switch (*p) {
        case '"':
        case '\\':
        case '/':
            g_string_append_c(gs, *p);
            p++;
            break;
        case 'b':
            g_string_append_c(gs, '\b');
            p++;
            break;
        case 'f':
            g_string_append_c(gs, '\f');
            p++;
            break;
        case 'n':
            g_string_append_c(gs, '\n');
            p++;
            break;
        case 'r':
            g_string_append_c(gs, '\r');
            p++;
            break;
        case 't':
            g_string_append_c(gs, '\t');
            p++;
            break;
        case 'u': {
            int h0, h1, h2, h3;
            unsigned int u;

            if (pe - p < 5) {
                p = pe;
                break;
            }
            h0 = hexval(p[1]);
            h1 = hexval(p[2]);
            h2 = hexval(p[3]);
            h3 = hexval(p[4]);
            if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0) {
                p += 5;
                break;
            }
            u = ((unsigned int) h0 << 12) | ((unsigned int) h1 << 8) |
                ((unsigned int) h2 << 4) | (unsigned int) h3;
            (void) append_utf8_from_codepoint(gs, u);
            p += 5;
            break;
        }
        default:
            g_string_append_c(gs, *p);
            p++;
            break;
        }
    }

    return g_string_free(gs, FALSE);
}

static char *json_extract_string_field (const char *json, const char *field)
{
    const char *p, *q;
    char *needle;
    size_t nlen;

    if (json == NULL || field == NULL || *field == '\0') {
        return NULL;
    }

    needle = g_strdup_printf("\"%s\"", field);
    p = strstr(json, needle);
    g_free(needle);
    if (p == NULL) {
        return NULL;
    }

    p += 2 + strlen(field);
    p = strchr(p, ':');
    if (p == NULL) {
        return NULL;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    if (*p != '"') {
        return NULL;
    }
    p++;

    q = p;
    while (*q != '\0') {
        if (*q == '"' && (q == p || q[-1] != '\\')) {
            break;
        }
        q++;
    }
    if (*q != '"') {
        return NULL;
    }

    nlen = (size_t) (q - p);
    if (nlen == 0) {
        return g_strdup("");
    }

    return json_unescape_string(p, (int) nlen);
}

int gretl_llm_provider_from_string (const char *s, GretlLLMProvider *p)
{
    if (p == NULL) {
        return E_DATA;
    }

    *p = GRETL_LLM_NONE;
    if (s == NULL || *s == '\0') {
        return 0;
    }

    if (!g_ascii_strcasecmp(s, "codex")) {
        *p = GRETL_LLM_CODEX;
    } else if (!g_ascii_strcasecmp(s, "gemini")) {
        *p = GRETL_LLM_GEMINI;
    } else if (!g_ascii_strcasecmp(s, "none")) {
        *p = GRETL_LLM_NONE;
    } else {
        gretl_errmsg_sprintf("Unknown LLM provider '%s' (expected codex|gemini)", s);
        return E_DATA;
    }

    return 0;
}

const char *gretl_llm_provider_name (GretlLLMProvider p)
{
    if (p == GRETL_LLM_CODEX) {
        return "codex";
    } else if (p == GRETL_LLM_GEMINI) {
        return "gemini";
    } else {
        return "none";
    }
}

GretlLLMProvider gretl_llm_default_provider (void)
{
    GretlLLMProvider p = GRETL_LLM_NONE;
    const char *s = getenv("GRETL_LLM_PROVIDER");

    if (s != NULL && *s != '\0') {
        if (gretl_llm_provider_from_string(s, &p) == 0) {
            return p;
        }
    }

    return GRETL_LLM_CODEX;
}

static int read_reply_file (const char *path, char **reply)
{
    gchar *buf = NULL;
    gsize len = 0;
    gboolean ok;
    GError *gerr = NULL;

    ok = g_file_get_contents(path, &buf, &len, &gerr);
    if (!ok) {
        return set_spawn_error("Failed to read LLM output file", gerr);
    }
    if (len > GRETL_LLM_MAX_REPLY) {
        g_free(buf);
        gretl_errmsg_set("LLM reply too long");
        return E_TOOLONG;
    }

    *reply = (char *) buf;
    return 0;
}

static int read_reply_file_buf (const char *path, char **reply, char **errmsg)
{
    gchar *buf = NULL;
    gsize len = 0;
    gboolean ok;
    GError *gerr = NULL;

    ok = g_file_get_contents(path, &buf, &len, &gerr);
    if (!ok) {
        return set_spawn_error_buf("Failed to read LLM output file", gerr, errmsg);
    }
    if (len > GRETL_LLM_MAX_REPLY) {
        g_free(buf);
        if (errmsg != NULL) {
            *errmsg = g_strdup("LLM reply too long");
        }
        return E_TOOLONG;
    }

    *reply = (char *) buf;
    return 0;
}

static int run_codex_cli (const char *bin, const char *prompt, char **reply)
{
    gchar *tmpname = NULL;
    GError *gerr = NULL;
    gint fd;
    gint status = 0;
    gchar *stderr_buf = NULL;
    gchar *stdout_buf = NULL;
    gchar **argv = NULL;
    int err = 0;

    fd = g_file_open_tmp("gretl_codex_lastmsg_XXXXXX", &tmpname, &gerr);
    if (fd < 0) {
        return set_spawn_error("Failed to create temp file for codex output", gerr);
    }
#ifndef WIN32
    close(fd);
#endif

    {
        gchar *base_argv_safe[] = {
            (gchar *) bin,
            (gchar *) "-a", (gchar *) "never",
            (gchar *) "exec",
            (gchar *) "-s", (gchar *) "read-only",
            (gchar *) "--color", (gchar *) "never",
            (gchar *) "--skip-git-repo-check",
            (gchar *) "--output-last-message", tmpname,
            (gchar *) prompt,
            NULL
        };
        gchar *base_argv_danger[] = {
            (gchar *) bin,
            (gchar *) "exec",
            (gchar *) "--dangerously-bypass-approvals-and-sandbox",
            (gchar *) "--color", (gchar *) "never",
            (gchar *) "--skip-git-repo-check",
            (gchar *) "--output-last-message", tmpname,
            (gchar *) prompt,
            NULL
        };
        gchar **base_argv = codex_dangerous_default() ? base_argv_danger : base_argv_safe;

        argv = argv_wrap_timeout(base_argv);
        if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_STDIN_FROM_DEV_NULL, NULL, NULL,
                          &stdout_buf, &stderr_buf, &status, &gerr)) {
            free_spawn_argv(argv);
            g_unlink(tmpname);
            g_free(tmpname);
            g_free(stdout_buf);
            g_free(stderr_buf);
            return set_spawn_error("Failed to run codex CLI", gerr);
        }
        free_spawn_argv(argv);
    }

    if (!g_spawn_check_exit_status(status, NULL)) {
        if (status == (124 << 8) || status == (137 << 8)) {
            gretl_errmsg_set("codex timed out (set GRETL_LLM_TIMEOUT_SEC)");
        } else if (stderr_buf != NULL && *stderr_buf != '\0') {
            gretl_errmsg_sprintf("codex failed (stderr follows)\n%s", stderr_buf);
        } else if (stdout_buf != NULL && *stdout_buf != '\0') {
            gretl_errmsg_sprintf("codex failed (stdout follows)\n%s", stdout_buf);
        } else {
            gretl_errmsg_set("codex failed");
        }
        g_unlink(tmpname);
        g_free(tmpname);
        g_free(stdout_buf);
        g_free(stderr_buf);
        return E_EXTERNAL;
    }

    err = read_reply_file(tmpname, reply);
    g_unlink(tmpname);
    g_free(tmpname);

    if (err == 0 && (*reply == NULL || **reply == '\0')) {
        if (stderr_buf != NULL && *stderr_buf != '\0') {
            gretl_errmsg_sprintf("codex returned no reply (stderr follows)\n%s", stderr_buf);
        } else if (stdout_buf != NULL && *stdout_buf != '\0') {
            gretl_errmsg_sprintf("codex returned no reply (stdout follows)\n%s", stdout_buf);
        } else {
            gretl_errmsg_set("codex returned no reply");
        }
        err = E_EXTERNAL;
    }

    g_free(stdout_buf);
    g_free(stderr_buf);
    return err;
}

static int run_codex_cli_buf (const char *bin,
                              const char *prompt,
                              char **reply,
                              char **errmsg)
{
    gchar *tmpname = NULL;
    GError *gerr = NULL;
    gint fd;
    gint status = 0;
    gchar *stderr_buf = NULL;
    gchar *stdout_buf = NULL;
    gchar **argv = NULL;
    int err = 0;

    fd = g_file_open_tmp("gretl_codex_lastmsg_XXXXXX", &tmpname, &gerr);
    if (fd < 0) {
        return set_spawn_error_buf("Failed to create temp file for codex output", gerr, errmsg);
    }
#ifndef WIN32
    close(fd);
#endif

    {
        gchar *base_argv_safe[] = {
            (gchar *) bin,
            (gchar *) "-a", (gchar *) "never",
            (gchar *) "exec",
            (gchar *) "-s", (gchar *) "read-only",
            (gchar *) "--color", (gchar *) "never",
            (gchar *) "--skip-git-repo-check",
            (gchar *) "--output-last-message", tmpname,
            (gchar *) prompt,
            NULL
        };
        gchar *base_argv_danger[] = {
            (gchar *) bin,
            (gchar *) "exec",
            (gchar *) "--dangerously-bypass-approvals-and-sandbox",
            (gchar *) "--color", (gchar *) "never",
            (gchar *) "--skip-git-repo-check",
            (gchar *) "--output-last-message", tmpname,
            (gchar *) prompt,
            NULL
        };
        gchar **base_argv = codex_dangerous_default() ? base_argv_danger : base_argv_safe;

        argv = argv_wrap_timeout(base_argv);
        if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_STDIN_FROM_DEV_NULL, NULL, NULL,
                          &stdout_buf, &stderr_buf, &status, &gerr)) {
            free_spawn_argv(argv);
            g_unlink(tmpname);
            g_free(tmpname);
            g_free(stdout_buf);
            g_free(stderr_buf);
            return set_spawn_error_buf("Failed to run codex CLI", gerr, errmsg);
        }
        free_spawn_argv(argv);
    }

    if (check_child_status_buf("codex", status, stdout_buf, stderr_buf, errmsg)) {
        g_unlink(tmpname);
        g_free(tmpname);
        g_free(stdout_buf);
        g_free(stderr_buf);
        return E_EXTERNAL;
    }

    err = read_reply_file_buf(tmpname, reply, errmsg);
    g_unlink(tmpname);
    g_free(tmpname);

    if (err == 0 && (*reply == NULL || **reply == '\0')) {
        if (errmsg != NULL) {
            if (stderr_buf != NULL && *stderr_buf != '\0') {
                *errmsg = g_strdup_printf("codex returned no reply (stderr follows)\n%s", stderr_buf);
            } else if (stdout_buf != NULL && *stdout_buf != '\0') {
                *errmsg = g_strdup_printf("codex returned no reply (stdout follows)\n%s", stdout_buf);
            } else {
                *errmsg = g_strdup("codex returned no reply");
            }
        }
        err = E_EXTERNAL;
    }

    g_free(stdout_buf);
    g_free(stderr_buf);
    return err;
}

static int run_gemini_cli (const char *bin, const char *prompt, char **reply)
{
    GError *gerr = NULL;
    gint status = 0;
    gchar *stderr_buf = NULL;
    gchar *stdout_buf = NULL;
    gchar **argv = NULL;
    char *json = NULL;
    char *resp = NULL;

    {
        gchar *base_argv[] = {
            (gchar *) bin,
            (gchar *) "-p", (gchar *) prompt,
            (gchar *) "--output-format", (gchar *) "json",
            (gchar *) "--allowed-mcp-server-names", (gchar *) "gretl-none",
            (gchar *) "--allowed-tools", (gchar *) "gretl-none",
            NULL
        };

        argv = argv_wrap_timeout(base_argv);
        if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_STDIN_FROM_DEV_NULL, NULL, NULL,
                          &stdout_buf, &stderr_buf, &status, &gerr)) {
            free_spawn_argv(argv);
            g_free(stdout_buf);
            g_free(stderr_buf);
            return set_spawn_error("Failed to run gemini CLI", gerr);
        }
        free_spawn_argv(argv);
    }

    if (!g_spawn_check_exit_status(status, NULL)) {
        if (status == (124 << 8) || status == (137 << 8)) {
            gretl_errmsg_set("gemini timed out (set GRETL_LLM_TIMEOUT_SEC)");
        } else if (stderr_buf != NULL && *stderr_buf != '\0') {
            gretl_errmsg_sprintf("gemini failed (stderr follows)\n%s", stderr_buf);
        } else if (stdout_buf != NULL && *stdout_buf != '\0') {
            gretl_errmsg_sprintf("gemini failed (stdout follows)\n%s", stdout_buf);
        } else {
            gretl_errmsg_set("gemini failed");
        }
        g_free(stdout_buf);
        g_free(stderr_buf);
        return E_EXTERNAL;
    }

    if (stderr_buf != NULL && strstr(stderr_buf, "An unexpected critical error occurred") != NULL) {
        gretl_errmsg_sprintf("gemini CLI error:\n%s", stderr_buf);
        g_free(stdout_buf);
        g_free(stderr_buf);
        return E_EXTERNAL;
    }

    json = strip_to_json(stdout_buf);
    if (json != NULL) {
        resp = json_extract_string_field(json, "response");
    }

    if (resp == NULL) {
        if (stdout_buf != NULL && *stdout_buf != '\0') {
            gretl_errmsg_sprintf("Failed to parse gemini response (output follows)\n%s", stdout_buf);
        } else if (stderr_buf != NULL && *stderr_buf != '\0') {
            gretl_errmsg_sprintf("gemini returned no JSON (stderr follows)\n%s", stderr_buf);
        } else {
            gretl_errmsg_set("gemini returned no reply");
        }
        g_free(json);
        g_free(stdout_buf);
        g_free(stderr_buf);
        return E_EXTERNAL;
    }

    if (strlen(resp) > GRETL_LLM_MAX_REPLY) {
        g_free(resp);
        gretl_errmsg_set("LLM reply too long");
        g_free(json);
        g_free(stdout_buf);
        g_free(stderr_buf);
        return E_TOOLONG;
    }

    *reply = resp;

    g_free(json);
    g_free(stdout_buf);
    g_free(stderr_buf);
    return 0;
}

static int run_gemini_cli_buf (const char *bin,
                               const char *prompt,
                               char **reply,
                               char **errmsg)
{
    GError *gerr = NULL;
    gint status = 0;
    gchar *stderr_buf = NULL;
    gchar *stdout_buf = NULL;
    gchar **argv = NULL;
    char *json = NULL;
    char *resp = NULL;

    {
        gchar *base_argv[] = {
            (gchar *) bin,
            (gchar *) "-p", (gchar *) prompt,
            (gchar *) "--output-format", (gchar *) "json",
            (gchar *) "--allowed-mcp-server-names", (gchar *) "gretl-none",
            (gchar *) "--allowed-tools", (gchar *) "gretl-none",
            NULL
        };

        argv = argv_wrap_timeout(base_argv);
        if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_STDIN_FROM_DEV_NULL, NULL, NULL,
                          &stdout_buf, &stderr_buf, &status, &gerr)) {
            free_spawn_argv(argv);
            g_free(stdout_buf);
            g_free(stderr_buf);
            return set_spawn_error_buf("Failed to run gemini CLI", gerr, errmsg);
        }
        free_spawn_argv(argv);
    }

    if (check_child_status_buf("gemini", status, stdout_buf, stderr_buf, errmsg)) {
        g_free(stdout_buf);
        g_free(stderr_buf);
        return E_EXTERNAL;
    }

    if (stderr_buf != NULL && strstr(stderr_buf, "An unexpected critical error occurred") != NULL) {
        if (errmsg != NULL) {
            *errmsg = g_strdup_printf("gemini CLI error:\n%s", stderr_buf);
        }
        g_free(stdout_buf);
        g_free(stderr_buf);
        return E_EXTERNAL;
    }

    json = strip_to_json(stdout_buf);
    if (json != NULL) {
        resp = json_extract_string_field(json, "response");
    }

    if (resp == NULL) {
        if (errmsg != NULL) {
            if (stdout_buf != NULL && *stdout_buf != '\0') {
                *errmsg = g_strdup_printf("Failed to parse gemini response (output follows)\n%s", stdout_buf);
            } else if (stderr_buf != NULL && *stderr_buf != '\0') {
                *errmsg = g_strdup_printf("gemini returned no JSON (stderr follows)\n%s", stderr_buf);
            } else {
                *errmsg = g_strdup("gemini returned no reply");
            }
        }
        g_free(json);
        g_free(stdout_buf);
        g_free(stderr_buf);
        return E_EXTERNAL;
    }

    if (strlen(resp) > GRETL_LLM_MAX_REPLY) {
        g_free(resp);
        if (errmsg != NULL) {
            *errmsg = g_strdup("LLM reply too long");
        }
        g_free(json);
        g_free(stdout_buf);
        g_free(stderr_buf);
        return E_TOOLONG;
    }

    *reply = resp;

    g_free(json);
    g_free(stdout_buf);
    g_free(stderr_buf);
    return 0;
}

int gretl_llm_complete (GretlLLMProvider provider,
                        const char *prompt,
                        char **reply)
{
    gchar *bin = NULL;
    int err = 0;

    if (reply == NULL) {
        return E_DATA;
    }
    *reply = NULL;

    if (prompt == NULL || *prompt == '\0') {
        gretl_errmsg_set("Missing prompt");
        return E_DATA;
    }

    if (provider == GRETL_LLM_NONE) {
        provider = gretl_llm_default_provider();
    }

    bin = find_executable(provider);
    if (bin == NULL || *bin == '\0') {
        gretl_errmsg_sprintf("Cannot find %s executable (set GRETL_%s_BIN)",
                             gretl_llm_provider_name(provider),
                             provider == GRETL_LLM_CODEX ? "CODEX" : "GEMINI");
        g_free(bin);
        return E_EXTERNAL;
    }

    if (provider == GRETL_LLM_CODEX) {
        err = run_codex_cli(bin, prompt, reply);
    } else if (provider == GRETL_LLM_GEMINI) {
        err = run_gemini_cli(bin, prompt, reply);
    } else {
        gretl_errmsg_set("No LLM provider selected");
        err = E_DATA;
    }

    g_free(bin);
    return err;
}

int gretl_llm_complete_with_error (GretlLLMProvider provider,
                                   const char *prompt,
                                   char **reply,
                                   char **errmsg)
{
    gchar *bin = NULL;
    int err = 0;

    if (reply == NULL) {
        return E_DATA;
    }
    *reply = NULL;
    if (errmsg != NULL) {
        *errmsg = NULL;
    }

    if (prompt == NULL || *prompt == '\0') {
        if (errmsg != NULL) {
            *errmsg = g_strdup("Missing prompt");
        }
        return E_DATA;
    }

    if (provider == GRETL_LLM_NONE) {
        provider = gretl_llm_default_provider();
    }

    bin = find_executable(provider);
    if (bin == NULL || *bin == '\0') {
        if (errmsg != NULL) {
            *errmsg = g_strdup_printf("Cannot find %s executable (set GRETL_%s_BIN)",
                                      gretl_llm_provider_name(provider),
                                      provider == GRETL_LLM_CODEX ? "CODEX" : "GEMINI");
        }
        g_free(bin);
        return E_EXTERNAL;
    }

    if (provider == GRETL_LLM_CODEX) {
        err = run_codex_cli_buf(bin, prompt, reply, errmsg);
    } else if (provider == GRETL_LLM_GEMINI) {
        err = run_gemini_cli_buf(bin, prompt, reply, errmsg);
    } else {
        if (errmsg != NULL) {
            *errmsg = g_strdup("No LLM provider selected");
        }
        err = E_DATA;
    }

    g_free(bin);
    return err;
}
