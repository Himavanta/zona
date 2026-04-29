#ifndef ZONA_H
#define ZONA_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>
#include <time.h>

/* ============================================================
   Token
   ============================================================ */

enum TokenType { T_NUM, T_STR, T_SYM, T_PRIM, T_WORD };

typedef struct {
    enum TokenType type;
    char text[256];
    double num;
    int line;
} Token;

/* ============================================================
   Tokenizer
   ============================================================ */

static const char *SYMS = "@;$?!~&#+-*/%^><=._'";

static int tokenize(const char *line, Token *toks, int max, int line_num) {
    int n = 0;
    const char *p = line;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '\n') break;
        if (*p == '_') break;
        toks[n].line = line_num;

        /* string */
        if (*p == '\'') {
            p++;
            int len = 0;
            while (*p && *p != '\'' && *p != '\n') {
                if (*p == '\\' && *(p+1)) {
                    p++;
                    switch (*p) {
                        case 'n': toks[n].text[len++] = '\n'; break;
                        case 't': toks[n].text[len++] = '\t'; break;
                        case '\\': toks[n].text[len++] = '\\'; break;
                        case '\'': toks[n].text[len++] = '\''; break;
                        default: toks[n].text[len++] = *p; break;
                    }
                } else {
                    toks[n].text[len++] = *p;
                }
                p++;
            }
            if (*p == '\'') p++;
            toks[n].text[len] = '\0';
            toks[n].type = T_STR;
            n++; continue;
        }
        /* negative number */
        if (*p == '-' && isdigit((unsigned char)*(p+1))) {
            const char *s = p; p++;
            while (isdigit((unsigned char)*p) || *p == '.') p++;
            int len = (int)(p - s);
            memcpy(toks[n].text, s, len); toks[n].text[len] = '\0';
            toks[n].type = T_NUM; toks[n].num = atof(toks[n].text);
            n++; continue;
        }
        /* number */
        if (isdigit((unsigned char)*p)) {
            const char *s = p;
            while (isdigit((unsigned char)*p) || *p == '.') p++;
            int len = (int)(p - s);
            memcpy(toks[n].text, s, len); toks[n].text[len] = '\0';
            toks[n].type = T_NUM; toks[n].num = atof(toks[n].text);
            n++; continue;
        }
        /* single symbol */
        if (strchr(SYMS, *p) && *p != '\'' && *p != '_') {
            toks[n].text[0] = *p; toks[n].text[1] = '\0';
            toks[n].type = T_SYM; p++;
            n++; continue;
        }
        /* system primitive :xxx */
        if (*p == ':' && isalpha((unsigned char)*(p+1))) {
            const char *s = p; p++;
            while (isalpha((unsigned char)*p) || isdigit((unsigned char)*p)) p++;
            int len = (int)(p - s);
            memcpy(toks[n].text, s, len); toks[n].text[len] = '\0';
            toks[n].type = T_PRIM;
            n++; continue;
        }
        /* user word */
        if (isalpha((unsigned char)*p)) {
            const char *s = p;
            while (isalpha((unsigned char)*p) || isdigit((unsigned char)*p)) p++;
            int len = (int)(p - s);
            memcpy(toks[n].text, s, len); toks[n].text[len] = '\0';
            toks[n].type = T_WORD;
            n++; continue;
        }
        p++;
    }
    return n;
}

#define TOK_MAX 4096

static int tokenize_all(const char *src, Token *toks, int max) {
    int total = 0, line_num = 1;
    const char *p = src;
    while (*p && total < max) {
        const char *eol = strchr(p, '\n');
        int line_len = eol ? (int)(eol - p) : (int)strlen(p);
        char line[4096];
        if (line_len >= (int)sizeof(line)) line_len = sizeof(line) - 1;
        memcpy(line, p, line_len);
        line[line_len] = '\0';
        total += tokenize(line, toks + total, max - total, line_num);
        p += line_len;
        if (*p == '\n') p++;
        line_num++;
    }
    return total;
}

/* ============================================================
   Word definition (shared structure)
   ============================================================ */

#define DICT_MAX 256
#define WORD_BODY_MAX 256

typedef struct {
    char name[256];
    Token body[WORD_BODY_MAX];
    int len;
} Word;

/* Read file into malloc'd buffer, caller must free */
static char *read_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) { fprintf(stderr, "cannot open: %s\n", path); return NULL; }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *src = malloc(sz + 1);
    fread(src, 1, sz, fp);
    src[sz] = '\0';
    fclose(fp);
    return src;
}

/* ============================================================
   Validation helpers for :use / :bind
   ============================================================ */

static int is_type_str(const char *s) {
    for (; *s; s++)
        if (!strchr("idfslpvS", *s)) return 0;
    return 1;
}

/* count tokens on the same line as toks[start], starting from start */
static int line_token_count(Token *toks, int n, int start) {
    int line = toks[start].line, count = 0;
    for (int j = start; j < n && toks[j].line == line; j++) count++;
    return count;
}

/* validate :use line: :use 'path' (2 tokens on line) */
static int validate_use(Token *toks, int n, int i) {
    int cnt = line_token_count(toks, n, i);
    if (cnt < 2 || toks[i+1].type != T_STR) {
        fprintf(stderr, "line %d: :use must be followed by a string path\n", toks[i].line);
        return 0;
    }
    if (cnt > 2) {
        fprintf(stderr, "line %d: :use line must contain only :use and path\n", toks[i].line);
        return 0;
    }
    return 1;
}

/* validate :bind line: :bind name 'cname' ret [params] (4 or 5 tokens) */
static int validate_bind(Token *toks, int n, int i) {
    int cnt = line_token_count(toks, n, i);
    if (cnt < 4) {
        fprintf(stderr, "line %d: :bind requires: name 'cname' retType [paramTypes]\n", toks[i].line);
        return 0;
    }
    if (toks[i+1].type != T_WORD) {
        fprintf(stderr, "line %d: :bind name must be a word\n", toks[i].line);
        return 0;
    }
    if (toks[i+2].type != T_STR) {
        fprintf(stderr, "line %d: :bind C name must be a string\n", toks[i].line);
        return 0;
    }
    if (toks[i+3].type != T_WORD || strlen(toks[i+3].text) != 1 || !is_type_str(toks[i+3].text)) {
        fprintf(stderr, "line %d: :bind return type must be a single type char (idfslpv)\n", toks[i].line);
        return 0;
    }
    if (cnt >= 5) {
        if (toks[i+4].type != T_WORD || !is_type_str(toks[i+4].text)) {
            fprintf(stderr, "line %d: :bind param types must only contain type chars (idfslpv)\n", toks[i].line);
            return 0;
        }
    }
    if (cnt > 5) {
        fprintf(stderr, "line %d: :bind has too many tokens\n", toks[i].line);
        return 0;
    }
    return 1;
}

/* check that :use/:bind don't appear inside a word body */
static void check_body_no_directives(Token *body, int len, const char *word_name) {
    for (int j = 0; j < len; j++) {
        if (body[j].type == T_PRIM &&
            (strcmp(body[j].text, ":use") == 0 || strcmp(body[j].text, ":bind") == 0)) {
            fprintf(stderr, "line %d: %s cannot appear inside word '%s'\n",
                    body[j].line, body[j].text, word_name);
            exit(1);
        }
    }
}

#endif
