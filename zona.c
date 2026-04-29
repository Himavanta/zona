#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* ---- Token types ---- */
enum TokenType {
    T_NUM, T_STR, T_SYM, T_PRIM, T_WORD
};

typedef struct {
    enum TokenType type;
    char text[256];
    double num;
} Token;

/* ---- Tokenizer ---- */

static const char *SYMS = "@;$?!~&#+-*/%^><=._'";

static int is_sym(char c) {
    return c && strchr(SYMS, c);
}

/* Tokenize a line into tokens array. Returns token count. */
static int tokenize(const char *line, Token *toks, int max) {
    int n = 0;
    const char *p = line;
    while (*p && n < max) {
        /* skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '\n') break;

        /* comment: _ to end of line */
        if (*p == '_') break;

        /* string literal */
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
            n++;
            continue;
        }

        /* negative number: - followed by digit, preceded by whitespace (start or after space) */
        if (*p == '-' && isdigit((unsigned char)*(p+1))) {
            const char *start = p;
            p++;
            while (isdigit((unsigned char)*p) || *p == '.') p++;
            int len = (int)(p - start);
            memcpy(toks[n].text, start, len);
            toks[n].text[len] = '\0';
            toks[n].type = T_NUM;
            toks[n].num = atof(toks[n].text);
            n++;
            continue;
        }

        /* number */
        if (isdigit((unsigned char)*p)) {
            const char *start = p;
            while (isdigit((unsigned char)*p) || *p == '.') p++;
            int len = (int)(p - start);
            memcpy(toks[n].text, start, len);
            toks[n].text[len] = '\0';
            toks[n].type = T_NUM;
            toks[n].num = atof(toks[n].text);
            n++;
            continue;
        }

        /* single symbol (excluding ' and _) */
        if (is_sym(*p) && *p != '\'' && *p != '_') {
            toks[n].text[0] = *p;
            toks[n].text[1] = '\0';
            toks[n].type = T_SYM;
            p++;
            n++;
            continue;
        }

        /* system primitive :xxx */
        if (*p == ':' && isalpha((unsigned char)*(p+1))) {
            const char *start = p;
            p++;
            while (isalpha((unsigned char)*p)) p++;
            int len = (int)(p - start);
            memcpy(toks[n].text, start, len);
            toks[n].text[len] = '\0';
            toks[n].type = T_PRIM;
            n++;
            continue;
        }

        /* user word */
        if (isalpha((unsigned char)*p)) {
            const char *start = p;
            while (isalpha((unsigned char)*p) || isdigit((unsigned char)*p)) p++;
            int len = (int)(p - start);
            memcpy(toks[n].text, start, len);
            toks[n].text[len] = '\0';
            toks[n].type = T_WORD;
            n++;
            continue;
        }

        /* skip unknown char */
        p++;
    }
    return n;
}

/* ---- String storage (temporary, until memory system) ---- */

#define STR_POOL_SIZE 256
#define STR_MAX_LEN 256
static char str_pool[STR_POOL_SIZE][STR_MAX_LEN];
static int str_count = 0;

/* Store a string, return its index (encoded as negative to distinguish from numbers) */
static int store_str(const char *s) {
    if (str_count >= STR_POOL_SIZE) { fprintf(stderr, "string pool full\n"); return 0; }
    strncpy(str_pool[str_count], s, STR_MAX_LEN - 1);
    str_pool[str_count][STR_MAX_LEN - 1] = '\0';
    return -(str_count++ + 1); /* -1, -2, -3 ... */
}

/* ---- Data stack ---- */

#define STACK_MAX 256
static double stack[STACK_MAX];
static int sp = 0;

static void push(double v) {
    if (sp >= STACK_MAX) { fprintf(stderr, "stack overflow\n"); return; }
    stack[sp++] = v;
}

static double pop(void) {
    if (sp <= 0) { fprintf(stderr, "stack underflow\n"); return 0; }
    return stack[--sp];
}

static double peek(void) {
    if (sp <= 0) { fprintf(stderr, "stack underflow\n"); return 0; }
    return stack[sp - 1];
}

/* ---- Execute tokens ---- */

static void exec(Token *toks, int n) {
    for (int i = 0; i < n; i++) {
        Token *t = &toks[i];

        if (t->type == T_NUM) {
            push(t->num);
            continue;
        }

        if (t->type == T_STR) {
            push(store_str(t->text));
            continue;
        }

        if (t->type == T_SYM) {
            char c = t->text[0];
            double a, b;
            switch (c) {
                case '+': b = pop(); a = pop(); push(a + b); break;
                case '-': b = pop(); a = pop(); push(a - b); break;
                case '*': b = pop(); a = pop(); push(a * b); break;
                case '/': b = pop(); a = pop();
                    if (b == 0) { fprintf(stderr, "division by zero\n"); break; }
                    push(a / b); break;
                case '%': b = pop(); a = pop(); push(fmod(a, b)); break;
                case '^': b = pop(); a = pop(); push(pow(a, b)); break;
                case '>': b = pop(); a = pop(); push(a > b ? 1 : 0); break;
                case '<': b = pop(); a = pop(); push(a < b ? 1 : 0); break;
                case '=': b = pop(); a = pop(); push(a == b ? 1 : 0); break;
                case '.': {
                    double v = pop();
                    int iv = (int)v;
                    if (iv < 0 && iv >= -str_count) {
                        printf("%s\n", str_pool[-(iv + 1)]);
                    } else if (v == (long)v) {
                        printf("%ld\n", (long)v);
                    } else {
                        printf("%g\n", v);
                    }
                    break;
                }
                default: break;
            }
            continue;
        }

        if (t->type == T_PRIM) {
            if (strcmp(t->text, ":dup") == 0) {
                push(peek());
            } else if (strcmp(t->text, ":drop") == 0) {
                pop();
            } else if (strcmp(t->text, ":swap") == 0) {
                double b = pop(), a = pop();
                push(b); push(a);
            } else if (strcmp(t->text, ":over") == 0) {
                double b = pop(), a = pop();
                push(a); push(b); push(a);
            } else if (strcmp(t->text, ":rot") == 0) {
                double c = pop(), b = pop(), a = pop();
                push(b); push(c); push(a);
            } else {
                fprintf(stderr, "unknown primitive: %s\n", t->text);
            }
            continue;
        }

        if (t->type == T_WORD) {
            fprintf(stderr, "unknown word: %s\n", t->text);
            continue;
        }
    }
}

/* ---- REPL ---- */

int main(void) {
    char line[1024];
    Token toks[256];

    printf("zona> ");
    while (fgets(line, sizeof(line), stdin)) {
        int n = tokenize(line, toks, 256);
        exec(toks, n);
        printf("zona> ");
    }
    printf("\n");
    return 0;
}
