#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <glob.h>

#include "quash.h"
#include "arrays.h"


Token make_token(TokenEnum type, TokenFlags flags) {
    Token t;
    t.flags = flags;
    t.text = NULL;
    t.token = type;
    return t;
}

/**
 * any character that ends a WORD
 */
static int is_metachar(char c) {
    switch (c) {
    case ' ':
    case '\t':
    case '\r':
    case '\n':
    case '|':
    case '&':
    case '<':
    case '>':
    case '#':
    case '\'':
    case '\"':
    case '`':
        return 1;
    default:
        return 0;
    }
}

static int is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int is_number(char c) {
    return c >= '0' && c <= '9';
}

static int is_word_char(char c) {
    return c > ' ' && c <= '~' && !is_metachar(c);
}

static int is_var_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '?';
}

static int start_of_variable(char c) {
    return c == '$';
}

static size_t next_quote_char(char *string, char quotechar, size_t index) {
    index++;

    while (string[index] && string[index] != quotechar) {
        index++;
    }

    return string[index] ? index : ULONG_MAX;
}

static char* expand_variables(char *string) {
    if (!string) {
        return NULL;
    }

    char *result = NULL;

    size_t i, j, k;
    for (i = 0, j = 0, k = 0; string[i] != '\0'; i++) {
        if (string[i] == '\\') {
            i++;
            continue;
        }

        if (string[i] == '\'' || string[i] == '`') {
            i = next_quote_char(string, string[i], i);

            if (i == ULONG_MAX) { /* string was never closed */
                free(result);
                return NULL;
            }
        }

        if (start_of_variable(string[i])) {
            size_t end = i;

            do {
                end++;
            } while (is_var_char(string[end]));

            char end_char = string[end];
            string[end] = '\0';

            /* it's ok if var is NULL */
            char *var = getenv(&string[i+1]);
            size_t var_len = var ? strlen(var) : 0;
            string[end] = end_char;

            result = realloc(result, j + (i - k) + var_len + 1);
            strncpy(result + j, string + k, i - k);

            if (var) {
                strncpy(result + j + (i - k), var, var_len);
            }

            j += (i - k) + var_len;
            k = end;
            i = end - 1;
        }
    }

    j += i - k;
    if (j > 0) {
        result = realloc(result, j + 1);
        strncpy(result + j - (i - k), string + k, i - k + 1);
    }

    return result;
}


/* ---------------------------- */
/*        glob expansion        */
/* ---------------------------- */

/**
 * @param strings an initialized string buffer to fill with the glob-expanded string
 * @param input the original input string
 * @returns `1` on success, `0` on failure
 */
static int expand_globs(StringDynamicBuffer *strings, char *input) {
    static int glob_flags = GLOB_NOSORT | GLOB_NOCHECK;
    glob_t globbuf;
    memset(&globbuf, 0, sizeof globbuf);

    for (int i = 0; input[i] != '\0';) {
        if (is_whitespace(input[i]) || input[i] == '\\') {
            i++;
            continue;
        }

        if (input[i] == '\"' || input[i] == '\'' || input[i] == '`') {
            size_t string_end = next_quote_char(input, input[i], i);
            if (string_end == ULONG_MAX) {
                return 0;
            }

            append_string(strings, input + i, string_end - i);
            i = string_end + 1;
            continue;
        }

        if (is_word_char(input[i])) {
            int word_start = i;

            /* increment `i` until it is no longer indexing a word char */
            for (i += 1; input[i] && is_word_char(input[i]); i++) { }

            char temp = input[i];
            input[i] = '\0';
            glob(input + word_start, glob_flags, NULL, &globbuf);

            for (size_t j = 0; j < globbuf.gl_pathc; j++) {
                append_string(strings, globbuf.gl_pathv[j], 0);
            }

            input[i] = temp;
            globfree(&globbuf);
            continue;
        }

        if (input[i] == '$') {
            int var_start = i;

            for (i += 1; input[i] && is_var_char(input[i]); i++) { }

            char temp = input[i];
            input[i] = '\0';
            append_string(strings, input + var_start, i - var_start);
            input[i] = temp;
            continue;
        }

        if (is_metachar(input[i])) {
            int meta_start = i;
            for (i += 1; input[i] && !is_whitespace(input[i]) && is_metachar(input[i]); i++) {}

            append_string(strings, input + meta_start, i - meta_start);
            continue;
        }
    }

    return 1;
}


/* ----------------------------- */
/*      tokenizer functions      */
/* ----------------------------- */

static void tokenize_metachar(char *input, TokenDynamicArray *tokens) {
    int i = 0;
    int len = strlen(input);

    while (i < len) {
        switch (input[i]) {
        case '|':
            if (i+1 < len && input[i+1] == '|') {
                append_token(tokens, make_token(T_PIPE_PIPE, TF_OPERATOR)); i += 2;
            } else {
                append_token(tokens, make_token(T_PIPE, TF_OPERATOR)); i++;
            }
            break;
        case '&':
            if (i+1 < len && input[i+1] == '&') {
                append_token(tokens, make_token(T_AMP_AMP, TF_OPERATOR)); i += 2;
            } else {
                append_token(tokens, make_token(T_AMP, TF_OPERATOR)); i++;
            }
            break;
        case '<':
            if (i+1 < len && input[i+1] == '>') {
                append_token(tokens, make_token(T_LESS_GREATER, TF_OPERATOR)); i += 2;
            } else {
                append_token(tokens, make_token(T_LESS, TF_OPERATOR)); i++;
            }
            break;
        case '>':
            if (i+1 < len && input[i+1] == '>') {
                if (i+2 < len && input[i+2] == '&') {
                    append_token(tokens, make_token(T_GREATER_GREATER_AMP, TF_OPERATOR)); i += 3;
                } else {
                    append_token(tokens, make_token(T_GREATER_GREATER, TF_OPERATOR)); i += 2;
                }
            } else if (i+1 < len && input[i+1] == '&') {
                append_token(tokens, make_token(T_GREATER_AMP, TF_OPERATOR)); i += 2;
            } else {
                append_token(tokens, make_token(T_GREATER, TF_OPERATOR)); i++;
            } 
            break;
        default:
            append_token(tokens, make_token(T_ERROR, 0));
            return;
        }
    }
}

static void tokenize_chunk(char *string, TokenDynamicArray *tokens) {
    int expanded_tilde = 0;
    Token t;

    switch (string[0]) {
    case '\'':
        t.token = T_WORD;
        t.flags = TF_SINGLE_QUOTE_STRING;
        t.text = strdup(string + 1);
        append_token(tokens, t);
        return;
    case '\"':
        t.token = T_WORD;
        t.flags = TF_DOUBLE_QUOTE_STRING;
        t.text = expand_variables(string + 1);
        append_token(tokens, t);
        return;
    case '`':
        t.token = T_WORD;
        t.flags = TF_BACKTICK_QUOTE_STRING;
        t.text = strdup(string + 1);
        append_token(tokens, t);
        return;
    case '$':
        t.token = T_WORD;
        t.flags = 0;
        t.text = expand_variables(string);
        append_token(tokens, t);
        return;
    case '>':
    case '<':
    case '&':
    case '|':
        tokenize_metachar(string, tokens);
        return;
    case '~':
        t.token = T_WORD;
        t.flags = 0;
        t.text = strdup(getenv("HOME"));
        expanded_tilde = 1;

        if (string[1] == '\0') {
            append_token(tokens, t);
            return;
        }
        break;
    case '#':
        t.token = T_EOS;
        t.text = NULL;
        append_token(tokens, t);
        return;
    default:
        break;
    }

    t.token = T_WORD;
    t.flags = 0;

    if (expanded_tilde) {
        char *rest = expand_variables(string + 1);
        size_t lhs_len = strlen(t.text);
        size_t rhs_len = strlen(rest);
        t.text = realloc(t.text, lhs_len + rhs_len + 1);
        strcpy(t.text + lhs_len, rest);
        t.text[lhs_len + rhs_len] = '\0';

    } else {
        t.text = expand_variables(string);
    }

    int can_be_number = 1;
    int can_be_var = 1;
    for (int i = 0; t.text[i] != '\0'; i++) {
        if (!is_number(t.text[i])) {
            can_be_number = 0;
        }

        if (!is_var_char(t.text[i])) {
            can_be_var = 0;
        }
    }

    if (can_be_number) {
        t.flags |= TF_NUMBER;
    }
    if (can_be_var) {
        t.flags |= TF_VARIABLE_NAME;
    }

    append_token(tokens, t);
}

int tokenize(TokenDynamicArray *tokens, char *input) {
    StringDynamicBuffer strings; 
    create_string_array(&strings);

    if (!expand_globs(&strings, input)) {
        free_string_array(&strings);
        return 0;
    }

    for (size_t i = 0; i < strings.strings_used; i++) {
        tokenize_chunk(strings.buffer + strings.strings[i], tokens);
        if (tokens->length > 0 && tokens->tuples[tokens->length - 1].token == T_EOS) {
            free_string_array(&strings);
            return 1;
        }
    }

    append_token(tokens, make_token(T_EOS, 0));
    free_string_array(&strings);
    return 1;
}

int redirect(Token token) {
    return (token.token >= T_GREATER) && (token.token <= T_GREATER_GREATER_AMP);
}
