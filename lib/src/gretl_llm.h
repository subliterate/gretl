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

#ifndef GRETL_LLM_H
#define GRETL_LLM_H

#ifdef  __cplusplus
extern "C" {
#endif

typedef enum {
    GRETL_LLM_NONE = 0,
    GRETL_LLM_CODEX = 1,
    GRETL_LLM_GEMINI = 2
} GretlLLMProvider;

int gretl_llm_provider_from_string (const char *s, GretlLLMProvider *p);

const char *gretl_llm_provider_name (GretlLLMProvider p);

/* Determine provider using GRETL_LLM_PROVIDER (codex|gemini). */
GretlLLMProvider gretl_llm_default_provider (void);

/* Run a one-shot completion using the selected provider's CLI.
   - No API keys are read by gretl.
   - No code is executed by gretl: this returns text only.
*/
int gretl_llm_complete (GretlLLMProvider provider,
                        const char *prompt,
                        char **reply);

/* Like gretl_llm_complete() but returns an allocated error message rather
   than writing to gretl's global error buffer (thread-friendlier). */
int gretl_llm_complete_with_error (GretlLLMProvider provider,
                                   const char *prompt,
                                   char **reply,
                                   char **errmsg);

#ifdef  __cplusplus
}
#endif

#endif /* GRETL_LLM_H */
