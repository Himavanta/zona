/*
 * Zona Next — 解释器
 *
 * 完整实现：栈效应声明 + 类型分离 + 调试/内省原语 + REPL。
 * 编译器的基础——所有语言特性先在解释器验证，再搬进 QBE 编译器。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

/* ============================================================
   类型系统
   ============================================================ */

/* 统一类型词汇表中的类型标记 */
typedef enum { TY_L = 'l', TY_D = 'd', TY_P = 'p' } Type;

static const char *type_name(Type t) {
    switch (t) {
        case TY_L: return "l";
        case TY_D: return "d";
        case TY_P: return "p";
    }
    return "?";
}

/* ============================================================
   Token
   ============================================================ */

enum TokenType { T_NUM, T_STR, T_SYM, T_PRIM, T_WORD, T_MEMBER };

typedef struct {
    enum TokenType type;
    char text[256];
    double num;      /* T_NUM value */
    int    ival;     /* 1 if integer literal, 0 if float */
    int    line;
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
        if (*p == '_') break;  /* comment to EOL */
        toks[n].line = line_num;

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
            toks[n].ival = 0;
            n++; continue;
        }

        /* negative number: '-' followed by digit (but not inside a word) */
        if (*p == '-' && isdigit((unsigned char)*(p+1))) {
            const char *s = p; p++;
            int has_dot = 0;
            while (isdigit((unsigned char)*p) || *p == '.') {
                if (*p == '.') has_dot = 1;
                p++;
            }
            int len = (int)(p - s);
            memcpy(toks[n].text, s, len); toks[n].text[len] = '\0';
            toks[n].type = T_NUM;
            toks[n].num = atof(toks[n].text);
            toks[n].ival = !has_dot;
            n++; continue;
        }

        /* number */
        if (isdigit((unsigned char)*p)) {
            const char *s = p;
            int has_dot = 0;
            while (isdigit((unsigned char)*p) || *p == '.') {
                if (*p == '.') has_dot = 1;
                p++;
            }
            int len = (int)(p - s);
            memcpy(toks[n].text, s, len); toks[n].text[len] = '\0';
            toks[n].type = T_NUM;
            toks[n].num = atof(toks[n].text);
            toks[n].ival = !has_dot;
            n++; continue;
        }

        /* single symbol */
        if (strchr(SYMS, *p) && *p != '\'' && *p != '_') {
            toks[n].text[0] = *p; toks[n].text[1] = '\0';
            toks[n].type = T_SYM; toks[n].ival = 0;
            p++;
            n++; continue;
        }

        /* system primitive :xxx — colon followed by alpha */
        if (*p == ':' && isalpha((unsigned char)*(p+1))) {
            const char *s = p; p++;
            while (isalpha((unsigned char)*p) || isdigit((unsigned char)*p)) p++;
            int len = (int)(p - s);
            memcpy(toks[n].text, s, len); toks[n].text[len] = '\0';
            toks[n].type = T_PRIM; toks[n].ival = 0;
            n++; continue;
        }
        /* standalone colon ':' — stack effect separator */
        if (*p == ':') {
            toks[n].text[0] = ':'; toks[n].text[1] = '\0';
            toks[n].type = T_SYM; toks[n].ival = 0;
            p++;
            n++; continue;
        }

        /* user word or member access (xxx.yyy) */
        if (isalpha((unsigned char)*p)) {
            const char *s = p;
            while (isalpha((unsigned char)*p) || isdigit((unsigned char)*p) || *p == '.') p++;
            int len = (int)(p - s);
            memcpy(toks[n].text, s, len); toks[n].text[len] = '\0';
            /* T_MEMBER if contains '.', T_WORD otherwise */
            toks[n].type = strchr(toks[n].text, '.') ? T_MEMBER : T_WORD;
            toks[n].ival = 0;
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
   Data stack — unified 64-bit slots, no runtime tags
   ============================================================ */

#define STACK_MAX 4096

typedef union {
    int64_t l;
    double  d;
    void   *p;
} Slot;

static Slot stack[STACK_MAX];
static int  sp = 0;
static int  cur_line = 0;

static void push_l(int64_t v) {
    if (sp >= STACK_MAX) { fprintf(stderr, "line %d: stack overflow\n", cur_line); exit(1); }
    stack[sp].l = v; sp++;
}
static void push_d(double v) {
    if (sp >= STACK_MAX) { fprintf(stderr, "line %d: stack overflow\n", cur_line); exit(1); }
    stack[sp].d = v; sp++;
}
static void push_p(void *v) {
    if (sp >= STACK_MAX) { fprintf(stderr, "line %d: stack overflow\n", cur_line); exit(1); }
    stack[sp].p = v; sp++;
}
static int64_t pop_l(void) {
    if (sp <= 0) { fprintf(stderr, "line %d: stack underflow\n", cur_line); exit(1); }
    return stack[--sp].l;
}
static double pop_d(void) {
    if (sp <= 0) { fprintf(stderr, "line %d: stack underflow\n", cur_line); exit(1); }
    return stack[--sp].d;
}
static void *pop_p(void) {
    if (sp <= 0) { fprintf(stderr, "line %d: stack underflow\n", cur_line); exit(1); }
    return stack[--sp].p;
}

/* ============================================================
   String pool — B2 static preallocation
   ============================================================ */

#define STR_POOL_MAX 512

typedef struct {
    int64_t len;
    char    data[256];
} StrEntry;

static StrEntry str_pool[STR_POOL_MAX];
static int     str_count = 0;

/* Store a C string in the pool, return pointer to data portion.
   Caller gets a p value: pointer to first char, len at ptr[-8]. */
static void *store_string(const char *s) {
    if (str_count >= STR_POOL_MAX) {
        fprintf(stderr, "line %d: string pool full\n", cur_line);
        exit(1);
    }
    int64_t len = (int64_t)strlen(s);
    str_pool[str_count].len = len;
    memcpy(str_pool[str_count].data, s, len);
    str_pool[str_count].data[len] = '\0';
    return str_pool[str_count++].data;
}

/* ============================================================
   Stack effect signature
   ============================================================ */

#define SIG_MAX 16

typedef struct {
    Type in[SIG_MAX];
    Type out[SIG_MAX];
    int  n_in;
    int  n_out;
} Sig;

/* Parse stack effect signature like "dd:d" or ":i" or "l:" */
static int parse_sig(const char *s, Sig *sig, int line) {
    memset(sig, 0, sizeof(Sig));
    const char *colon = strchr(s, ':');
    if (!colon) {
        fprintf(stderr, "line %d: missing ':' in stack effect '%s'\n", line, s);
        return 0;
    }
    /* input types (before :) */
    int ni = 0;
    for (const char *p = s; p < colon; p++) {
        if (*p == 'l' || *p == 'd' || *p == 'p') {
            if (ni >= SIG_MAX) { fprintf(stderr, "line %d: too many input types\n", line); return 0; }
            sig->in[ni++] = (Type)*p;
        } else {
            fprintf(stderr, "line %d: unknown type '%c' in stack effect\n", line, *p);
            return 0;
        }
    }
    sig->n_in = ni;
    /* output types (after :) */
    int no = 0;
    for (const char *p = colon + 1; *p; p++) {
        if (*p == 'l' || *p == 'd' || *p == 'p') {
            if (no >= SIG_MAX) { fprintf(stderr, "line %d: too many output types\n", line); return 0; }
            sig->out[no++] = (Type)*p;
        } else {
            fprintf(stderr, "line %d: unknown type '%c' in stack effect\n", line, *p);
            return 0;
        }
    }
    sig->n_out = no;
    return 1;
}

/* ============================================================
   Compile-time type tracker (for stack effect verification)
   ============================================================ */

#define TSTACK_MAX 64

typedef struct {
    Type types[TSTACK_MAX];
    int  n;
} TypeStack;

static void ts_init(TypeStack *ts) { ts->n = 0; }

static void ts_push(TypeStack *ts, Type t) {
    if (ts->n >= TSTACK_MAX) { fprintf(stderr, "type stack overflow\n"); exit(1); }
    ts->types[ts->n++] = t;
}

static Type ts_pop(TypeStack *ts) {
    if (ts->n <= 0) { fprintf(stderr, "line %d: type stack underflow during verification\n", cur_line); exit(1); }
    return ts->types[--ts->n];
}

static int ts_len(const TypeStack *ts) { return ts->n; }

/* ============================================================
   Word definition
   ============================================================ */

#define DICT_MAX 4096
#define WORD_BODY_MAX 1024

typedef struct {
    char name[256];
    Sig  sig;
    Token body[WORD_BODY_MAX];
    int  len;
    int  is_recursive;  /* 1 if word calls itself */
} Word;

static Word dict[DICT_MAX];
static int  dict_count = 0;

static Word *find_word(const char *name) {
    for (int i = dict_count - 1; i >= 0; i--)
        if (strcmp(dict[i].name, name) == 0) return &dict[i];
    return NULL;
}

/* ============================================================
   Stack effect verification
   ============================================================ */

/* Forward declaration */
static int verify_body(Word *w, TypeStack *ts, int *error_line);

static int verify_token(Token *t, TypeStack *ts, const char *word_name, int *error_line) {
    switch (t->type) {
    case T_NUM:
        if (t->ival) ts_push(ts, TY_L);
        else         ts_push(ts, TY_D);
        break;

    case T_STR:
        ts_push(ts, TY_P);  /* single pointer to {len, data} */
        break;

    case T_SYM: {
        char c = t->text[0];
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '^') {
            /* binary arithmetic: pop 2 same-type, push 1 same-type */
            Type b = ts_pop(ts), a = ts_pop(ts);
            if (a != b) {
                fprintf(stderr, "line %d: type mismatch in '%c': %s vs %s\n",
                        t->line, c, type_name(a), type_name(b));
                *error_line = t->line; return 0;
            }
            ts_push(ts, a);
        } else if (c == '>' || c == '<' || c == '=') {
            /* comparison: pop 2 same-type, push l */
            Type b = ts_pop(ts), a = ts_pop(ts);
            if (a != b) {
                fprintf(stderr, "line %d: type mismatch in '%c': %s vs %s\n",
                        t->line, c, type_name(a), type_name(b));
                *error_line = t->line; return 0;
            }
            ts_push(ts, TY_L);
        } else if (c == '&') {
            /* memory read: p -- l  (8 bytes as int64) */
            Type a = ts_pop(ts);
            if (a != TY_P) {
                fprintf(stderr, "line %d: & requires p type, got %s\n",
                        t->line, type_name(a));
                *error_line = t->line; return 0;
            }
            ts_push(ts, TY_L);
        } else if (c == '#') {
            /* memory write: l p -- */
            Type b = ts_pop(ts), a = ts_pop(ts);
            if (b != TY_P) {
                fprintf(stderr, "line %d: # requires p as second arg, got %s\n",
                        t->line, type_name(b));
                *error_line = t->line; return 0;
            }
            if (a != TY_L && a != TY_D) {
                fprintf(stderr, "line %d: # requires l or d value, got %s\n",
                        t->line, type_name(a));
                *error_line = t->line; return 0;
            }
        } else if (c == '?') {
            /* conditional — handled in exec_body, not here */
            /* For verification, we need to check both branches.
               This is handled specially in verify_body. */
        } else if (c == '$' || c == '~') {
            /* return/loop — handled in verify_body */
        } else if (c == '!') {
            /* else marker — handled in verify_body */
        }
        break;
    }

    case T_PRIM:
        if (strcmp(t->text, ":dup") == 0) {
            Type a = ts_pop(ts);
            ts_push(ts, a);
            ts_push(ts, a);
        } else if (strcmp(t->text, ":drop") == 0) {
            ts_pop(ts);
        } else if (strcmp(t->text, ":swap") == 0) {
            Type b = ts_pop(ts), a = ts_pop(ts);
            ts_push(ts, b);
            ts_push(ts, a);
        } else if (strcmp(t->text, ":over") == 0) {
            Type b = ts_pop(ts), a = ts_pop(ts);
            ts_push(ts, a);
            ts_push(ts, b);
            ts_push(ts, a);
        } else if (strcmp(t->text, ":rot") == 0) {
            Type c = ts_pop(ts), b = ts_pop(ts), a = ts_pop(ts);
            ts_push(ts, b);
            ts_push(ts, c);
            ts_push(ts, a);
        } else if (strcmp(t->text, ":print") == 0) {
            /* :print pops one value — type doesn't matter for printing */
            ts_pop(ts);
        } else if (strcmp(t->text, ":emit") == 0) {
            Type a = ts_pop(ts);
            if (a != TY_L) {
                fprintf(stderr, "line %d: :emit requires l type, got %s\n", t->line, type_name(a));
                *error_line = t->line; return 0;
            }
        } else if (strcmp(t->text, ":type") == 0) {
            Type a = ts_pop(ts);
            if (a != TY_P) {
                fprintf(stderr, "line %d: :type requires p type, got %s\n", t->line, type_name(a));
                *error_line = t->line; return 0;
            }
        } else if (strcmp(t->text, ":key") == 0) {
            ts_push(ts, TY_L);
        } else if (strcmp(t->text, ":alloc") == 0) {
            Type a = ts_pop(ts);
            if (a != TY_L) {
                fprintf(stderr, "line %d: :alloc requires l type, got %s\n", t->line, type_name(a));
                *error_line = t->line; return 0;
            }
            ts_push(ts, TY_P);
        } else if (strcmp(t->text, ":free") == 0) {
            Type a = ts_pop(ts);
            if (a != TY_P) {
                fprintf(stderr, "line %d: :free requires p type, got %s\n", t->line, type_name(a));
                *error_line = t->line; return 0;
            }
        } else if (strcmp(t->text, ":stack") == 0) {
            /* no stack effect — prints, doesn't consume */
        } else if (strcmp(t->text, ":words") == 0) {
            /* no stack effect — prints, doesn't consume */
        } else if (strcmp(t->text, ":clear") == 0) {
            /* clears stack — verification can't track this statically.
               For safety, treat as consuming all and producing nothing.
               In practice this is a REPL debugging tool. */
            while (ts_len(ts) > 0) ts_pop(ts);
        } else if (strcmp(t->text, ":time") == 0) {
            ts_push(ts, TY_L);
        } else if (strcmp(t->text, ":rand") == 0) {
            Type a = ts_pop(ts);
            if (a != TY_L) {
                fprintf(stderr, "line %d: :rand requires l type, got %s\n", t->line, type_name(a));
                *error_line = t->line; return 0;
            }
            ts_push(ts, TY_L);
        } else if (strcmp(t->text, ":exit") == 0) {
            Type a = ts_pop(ts);
            if (a != TY_L) {
                fprintf(stderr, "line %d: :exit requires l type, got %s\n", t->line, type_name(a));
                *error_line = t->line; return 0;
            }
        } else if (strcmp(t->text, ":argc") == 0) {
            ts_push(ts, TY_L);
        } else if (strcmp(t->text, ":argv") == 0) {
            Type a = ts_pop(ts);
            if (a != TY_L) {
                fprintf(stderr, "line %d: :argv requires l index, got %s\n", t->line, type_name(a));
                *error_line = t->line; return 0;
            }
            ts_push(ts, TY_P);
        }
        break;

    case T_WORD: {
        Word *cw = find_word(t->text);
        /* Allow self-reference during verification (recursive words) */
        if (!cw) {
            /* Check if it's the word being verified (self-reference) */
            if (strcmp(t->text, word_name) == 0) {
                /* Use the signature of the word being defined */
                if (ts_len(ts) < /* placeholder */ 0) { /* skip check */ }
                /* We need access to the current word's sig.
                   For now, just trust the declared signature. */
                Sig *self_sig = NULL;
                for (int j = dict_count - 1; j >= 0; j--) {
                    if (strcmp(dict[j].name, word_name) == 0) {
                        self_sig = &dict[j].sig;
                        break;
                    }
                }
                if (!self_sig) {
                    fprintf(stderr, "line %d: cannot verify self-reference to '%s'\n", t->line, t->text);
                    *error_line = t->line; return 0;
                }
                if (ts_len(ts) < self_sig->n_in) {
                    fprintf(stderr, "line %d: stack underflow calling '%s' (need %d, have %d)\n",
                            t->line, t->text, self_sig->n_in, ts_len(ts));
                    *error_line = t->line; return 0;
                }
                for (int i = self_sig->n_in - 1; i >= 0; i--) {
                    Type got = ts_pop(ts);
                    if (got != self_sig->in[i]) {
                        fprintf(stderr, "line %d: type mismatch in arg %d of '%s': expected %s, got %s\n",
                                t->line, i, t->text, type_name(self_sig->in[i]), type_name(got));
                        *error_line = t->line; return 0;
                    }
                }
                for (int i = 0; i < self_sig->n_out; i++)
                    ts_push(ts, self_sig->out[i]);
                break;
            }
            fprintf(stderr, "line %d: unknown word '%s' in verification\n", t->line, t->text);
            *error_line = t->line; return 0;
        }
        /* check if recursive */
        if (strcmp(t->text, word_name) == 0) {
            /* marking happens at definition time */
        }
        /* consume inputs, produce outputs */
        if (ts_len(ts) < cw->sig.n_in) {
            fprintf(stderr, "line %d: stack underflow calling '%s' (need %d, have %d)\n",
                    t->line, t->text, cw->sig.n_in, ts_len(ts));
            *error_line = t->line; return 0;
        }
        /* pop inputs */
        for (int i = cw->sig.n_in - 1; i >= 0; i--) {
            Type got = ts_pop(ts);
            if (got != cw->sig.in[i]) {
                fprintf(stderr, "line %d: type mismatch in arg %d of '%s': expected %s, got %s\n",
                        t->line, i, t->text, type_name(cw->sig.in[i]), type_name(got));
                *error_line = t->line; return 0;
            }
        }
        /* push outputs */
        for (int i = 0; i < cw->sig.n_out; i++)
            ts_push(ts, cw->sig.out[i]);
        break;
    }

    case T_MEMBER:
        /* member access — not yet implemented for struct, treat as word for module */
        break;
    }

    return 1;
}

/* Verify a word's body against its declared stack effect.
   Returns 1 on success, 0 on error. */
static int verify_word(Word *w) {
    TypeStack ts;
    ts_init(&ts);

    /* push declared input types */
    for (int i = 0; i < w->sig.n_in; i++)
        ts_push(&ts, w->sig.in[i]);

    int error_line = 0;
    /* Simple verification: walk body, handle ?/!/ $/~ specially */
    int ip = 0;
    while (ip < w->len) {
        Token *t = &w->body[ip];

        if (t->type == T_SYM && t->text[0] == '?') {
            /* conditional: pop condition (l type), then verify branches */
            Type cond_type = ts_pop(&ts);
            if (cond_type != TY_L) {
                fprintf(stderr, "line %d: ? condition must be l type, got %s\n",
                        t->line, type_name(cond_type));
                return 0;
            }
            ip++;  /* skip ? */

            if (ip >= w->len) break;

            /* detect else branch: token after true-branch is '!' */
            int has_else = (ip + 1 < w->len &&
                            w->body[ip + 1].type == T_SYM &&
                            w->body[ip + 1].text[0] == '!');

            /* Save type stack before branches */
            TypeStack ts_save = ts;

            /* Verify true branch */
            int true_uses_return = 0;
            if (w->body[ip].type == T_SYM && w->body[ip].text[0] == '$') {
                true_uses_return = 1;
            } else {
                if (!verify_token(&w->body[ip], &ts, w->name, &error_line)) return 0;
            }
            TypeStack ts_true = ts;

            if (has_else) {
                /* Verify false branch (token at ip+2, or empty if past end) */
                ts = ts_save;
                int false_uses_return = 0;
                if (ip + 2 < w->len &&
                    w->body[ip + 2].type == T_SYM && w->body[ip + 2].text[0] == '$') {
                    false_uses_return = 1;
                } else if (ip + 2 < w->len) {
                    if (!verify_token(&w->body[ip + 2], &ts, w->name, &error_line)) return 0;
                }
                /* else: no false branch token, ts stays as ts_save */
                TypeStack ts_false = ts;

                /* Merge: if both branches return, no merge needed.
                   If one returns and one doesn't, use the non-return branch. */
                if (true_uses_return && false_uses_return) {
                    /* both return — stack type doesn't matter after */
                    ts = ts_save;  /* won't be used, but reset */
                } else if (true_uses_return) {
                    ts = ts_false;
                } else if (false_uses_return) {
                    ts = ts_true;
                } else {
                    /* Both branches continue — types must match */
                    if (ts_true.n != ts_false.n) {
                        fprintf(stderr, "line %d: branch stack depth mismatch (%d vs %d)\n",
                                t->line, ts_true.n, ts_false.n);
                        return 0;
                    }
                    for (int i = 0; i < ts_true.n; i++) {
                        if (ts_true.types[i] != ts_false.types[i]) {
                            fprintf(stderr, "line %d: branch type mismatch at position %d (%s vs %s)\n",
                                    t->line, i, type_name(ts_true.types[i]), type_name(ts_false.types[i]));
                            return 0;
                        }
                    }
                    ts = ts_true;
                }
                ip += 3;  /* skip true-token, !, false-token */
            } else {
                /* no else — skipping the true branch leaves stack unchanged */
                ts = ts_save;
                ip += 1;  /* skip true-token */
            }
            continue;
        }

        if (t->type == T_SYM && t->text[0] == '$') {
            /* return — skip rest of body for verification */
            break;
        }

        if (t->type == T_SYM && t->text[0] == '~') {
            /* loop — stack must match word's declared input types */
            if (ts_len(&ts) != w->sig.n_in) {
                fprintf(stderr, "line %d: loop stack depth mismatch for '%s': need %d (input), have %d\n",
                        t->line, w->name, w->sig.n_in, ts_len(&ts));
                return 0;
            }
            for (int j = 0; j < w->sig.n_in; j++) {
                if (ts.types[j] != w->sig.in[j]) {
                    fprintf(stderr, "line %d: loop type mismatch at position %d for '%s': expected %s, got %s\n",
                            t->line, j, w->name, type_name(w->sig.in[j]), type_name(ts.types[j]));
                    return 0;
                }
            }
            ip++;
            continue;
        }

        if (t->type == T_SYM && t->text[0] == '!') {
            /* stray else marker outside ? context */
            fprintf(stderr, "line %d: '!' without preceding '?'\n", t->line);
            return 0;
        }

        if (!verify_token(t, &ts, w->name, &error_line)) return 0;
        ip++;
    }

    /* Check final stack matches declared output.
       Words with ~ (loop) exit via $, not by falling through to ;.
       For those, skip output count check — $ handles the stack. */
    int has_loop = 0;
    for (int i = 0; i < w->len; i++)
        if (w->body[i].type == T_SYM && w->body[i].text[0] == '~') { has_loop = 1; break; }

    if (!has_loop) {
        if (ts_len(&ts) != w->sig.n_out) {
            fprintf(stderr, "line %d: stack effect mismatch for '%s': declared %d output(s), got %d\n",
                    cur_line, w->name, w->sig.n_out, ts_len(&ts));
            return 0;
        }
        for (int i = 0; i < w->sig.n_out; i++) {
            if (ts.types[i] != w->sig.out[i]) {
                fprintf(stderr, "line %d: output type mismatch at position %d for '%s': declared %s, got %s\n",
                        cur_line, i, w->name, type_name(w->sig.out[i]), type_name(ts.types[i]));
                return 0;
            }
        }
    }

    return 1;
}

/* ============================================================
   Runtime type tracker — tracks type of each stack slot
   ============================================================ */

#define RTSTACK_MAX STACK_MAX

static Type rt_types[RTSTACK_MAX];

static void rt_push(Type t) {
    if (sp >= RTSTACK_MAX) return;
    rt_types[sp - 1] = t;  /* sp already incremented by push_* */
}

/* We need to track types alongside the value stack.
   Simpler approach: maintain a parallel type stack. */
static Type rt_type_stack[STACK_MAX];
static int  rt_sp = 0;

static void rtpush(Type t) {
    if (rt_sp >= STACK_MAX) { fprintf(stderr, "type tracker overflow\n"); exit(1); }
    rt_type_stack[rt_sp++] = t;
}
static Type rtpop(void) {
    if (rt_sp <= 0) { fprintf(stderr, "type tracker underflow\n"); exit(1); }
    return rt_type_stack[--rt_sp];
}

/* ============================================================
   Interpreter
   ============================================================ */

enum ExecResult { EX_OK, EX_RETURN, EX_LOOP };
static void exec_body(Token *toks, int n);

static int  prog_argc = 0;
static char **prog_argv = NULL;

static void exec_prim(const char *name) {
    if (strcmp(name, ":dup") == 0) {
        Type t = rtpop();
        Slot s = stack[--sp];
        stack[sp] = s; stack[sp+1] = s;
        sp += 2;
        rtpush(t); rtpush(t);
        return;
    }
    if (strcmp(name, ":drop") == 0) {
        sp--; rtpop();
        return;
    }
    if (strcmp(name, ":swap") == 0) {
        Type b = rtpop(), a = rtpop();
        Slot sb = stack[sp-1], sa = stack[sp-2];
        stack[sp-2] = sb; stack[sp-1] = sa;
        rtpush(b); rtpush(a);
        return;
    }
    if (strcmp(name, ":over") == 0) {
        Type b = rtpop(), a = rtpop();
        Slot sb = stack[sp-1], sa = stack[sp-2];
        stack[sp] = sa; sp++;
        rtpush(a); rtpush(b); rtpush(a);
        return;
    }
    if (strcmp(name, ":rot") == 0) {
        Type c = rtpop(), b = rtpop(), a = rtpop();
        Slot sc = stack[sp-1], sb = stack[sp-2], sa = stack[sp-3];
        stack[sp-3] = sb; stack[sp-2] = sc; stack[sp-1] = sa;
        rtpush(b); rtpush(c); rtpush(a);
        return;
    }
    if (strcmp(name, ":print") == 0) {
        Type t = rtpop();
        if (t == TY_D) {
            double v = stack[--sp].d;
            if (v == (long)v) printf("%.0f\n", v);
            else printf("%g\n", v);
        } else {
            int64_t v = stack[--sp].l;
            printf("%ld\n", (long)v);
        }
        return;
    }
    if (strcmp(name, ":emit") == 0) {
        rtpop();
        int64_t c = stack[--sp].l;
        putchar((int)c);
        return;
    }
    if (strcmp(name, ":type") == 0) {
        rtpop();
        char *ptr = stack[--sp].p;
        int64_t len = *(int64_t *)(ptr - 8);
        for (int64_t i = 0; i < len; i++) putchar(ptr[i]);
        return;
    }
    if (strcmp(name, ":key") == 0) {
        int c = getchar();
        stack[sp].l = (int64_t)c; sp++;
        rtpush(TY_L);
        return;
    }
    if (strcmp(name, ":alloc") == 0) {
        rtpop();
        int64_t n = stack[--sp].l;
        void *p = calloc((size_t)n, 1);
        if (!p) { fprintf(stderr, "line %d: alloc failed\n", cur_line); stack[sp].p = NULL; }
        else     { stack[sp].p = p; }
        sp++; rtpush(TY_P);
        return;
    }
    if (strcmp(name, ":free") == 0) {
        rtpop();
        void *p = stack[--sp].p;
        free(p);
        return;
    }
    if (strcmp(name, ":stack") == 0) {
        printf("<%d>", sp);
        for (int i = 0; i < sp; i++) {
            Type t = rt_type_stack[i];
            putchar(' ');
            if (t == TY_D) {
                double v = stack[i].d;
                if (v == (long)v) printf("%.0f", v);
                else printf("%g", v);
            } else if (t == TY_P) {
                printf("%p", stack[i].p);
            } else {
                printf("%ld", (long)stack[i].l);
            }
            printf(":%s", type_name(t));
        }
        putchar('\n');
        return;
    }
    if (strcmp(name, ":words") == 0) {
        for (int i = 0; i < dict_count; i++) {
            Word *w = &dict[i];
            printf("%s (", w->name);
            for (int j = 0; j < w->sig.n_in; j++)  putchar(w->sig.in[j]);
            printf(":");
            for (int j = 0; j < w->sig.n_out; j++) putchar(w->sig.out[j]);
            printf(")\n");
        }
        return;
    }
    if (strcmp(name, ":clear") == 0) {
        sp = 0; rt_sp = 0;
        return;
    }
    if (strcmp(name, ":time") == 0) {
        stack[sp].l = (int64_t)time(NULL); sp++;
        rtpush(TY_L);
        return;
    }
    if (strcmp(name, ":rand") == 0) {
        rtpop();
        int64_t max = stack[--sp].l;
        stack[sp].l = (int64_t)(rand() % (int)max); sp++;
        rtpush(TY_L);
        return;
    }
    if (strcmp(name, ":exit") == 0) {
        rtpop();
        int64_t code = stack[--sp].l;
        exit((int)code);
    }
    if (strcmp(name, ":argc") == 0) {
        stack[sp].l = (int64_t)prog_argc; sp++;
        rtpush(TY_L);
        return;
    }
    if (strcmp(name, ":argv") == 0) {
        rtpop();
        int64_t idx = stack[--sp].l;
        if (idx >= 0 && idx < prog_argc && prog_argv) {
            push_p(store_string(prog_argv[idx]));
            rtpush(TY_P);
        } else {
            fprintf(stderr, "line %d: argv index out of range: %ld\n", cur_line, (long)idx);
            push_p(NULL); rtpush(TY_P);
        }
        return;
    }

    fprintf(stderr, "line %d: unknown primitive: %s\n", cur_line, name);
    exit(1);
}

static void exec_sym(char c) {
    switch (c) {
    case '+': {
        Type tb = rtpop(), ta = rtpop();
        if (ta != tb) { fprintf(stderr, "line %d: + type mismatch\n", cur_line); exit(1); }
        if (ta == TY_D) { double b = stack[--sp].d, a = stack[--sp].d; stack[sp].d = a + b; sp++; }
        else            { int64_t b = stack[--sp].l, a = stack[--sp].l; stack[sp].l = a + b; sp++; }
        rtpush(ta);
        break;
    }
    case '-': {
        Type tb = rtpop(), ta = rtpop();
        if (ta != tb) { fprintf(stderr, "line %d: - type mismatch\n", cur_line); exit(1); }
        if (ta == TY_D) { double b = stack[--sp].d, a = stack[--sp].d; stack[sp].d = a - b; sp++; }
        else            { int64_t b = stack[--sp].l, a = stack[--sp].l; stack[sp].l = a - b; sp++; }
        rtpush(ta);
        break;
    }
    case '*': {
        Type tb = rtpop(), ta = rtpop();
        if (ta != tb) { fprintf(stderr, "line %d: * type mismatch\n", cur_line); exit(1); }
        if (ta == TY_D) { double b = stack[--sp].d, a = stack[--sp].d; stack[sp].d = a * b; sp++; }
        else            { int64_t b = stack[--sp].l, a = stack[--sp].l; stack[sp].l = a * b; sp++; }
        rtpush(ta);
        break;
    }
    case '/': {
        Type tb = rtpop(), ta = rtpop();
        if (ta != tb) { fprintf(stderr, "line %d: / type mismatch\n", cur_line); exit(1); }
        if (ta == TY_D) {
            double b = stack[--sp].d, a = stack[--sp].d;
            if (b == 0) { fprintf(stderr, "line %d: division by zero\n", cur_line); exit(1); }
            stack[sp].d = a / b; sp++;
        } else {
            int64_t b = stack[--sp].l, a = stack[--sp].l;
            if (b == 0) { fprintf(stderr, "line %d: division by zero\n", cur_line); exit(1); }
            stack[sp].l = a / b; sp++;
        }
        rtpush(ta);
        break;
    }
    case '%': {
        Type tb = rtpop(), ta = rtpop();
        if (ta != tb) { fprintf(stderr, "line %d: %% type mismatch\n", cur_line); exit(1); }
        if (ta == TY_D) {
            double b = stack[--sp].d, a = stack[--sp].d;
            stack[sp].d = fmod(a, b); sp++;
        } else {
            int64_t b = stack[--sp].l, a = stack[--sp].l;
            stack[sp].l = a % b; sp++;
        }
        rtpush(ta);
        break;
    }
    case '^': {
        Type tb = rtpop(), ta = rtpop();
        if (ta != tb) { fprintf(stderr, "line %d: ^ type mismatch\n", cur_line); exit(1); }
        if (ta == TY_D) {
            double b = stack[--sp].d, a = stack[--sp].d;
            stack[sp].d = pow(a, b); sp++;
        } else {
            int64_t b = stack[--sp].l, a = stack[--sp].l;
            stack[sp].l = (int64_t)pow((double)a, (double)b); sp++;
        }
        rtpush(ta);
        break;
    }
    case '>': {
        Type tb = rtpop(), ta = rtpop();
        if (ta != tb) { fprintf(stderr, "line %d: > type mismatch\n", cur_line); exit(1); }
        int64_t r;
        if (ta == TY_D) { double b = stack[--sp].d, a = stack[--sp].d; r = a > b; }
        else            { int64_t b = stack[--sp].l, a = stack[--sp].l; r = a > b; }
        stack[sp].l = r; sp++;
        rtpush(TY_L);
        break;
    }
    case '<': {
        Type tb = rtpop(), ta = rtpop();
        if (ta != tb) { fprintf(stderr, "line %d: < type mismatch\n", cur_line); exit(1); }
        int64_t r;
        if (ta == TY_D) { double b = stack[--sp].d, a = stack[--sp].d; r = a < b; }
        else            { int64_t b = stack[--sp].l, a = stack[--sp].l; r = a < b; }
        stack[sp].l = r; sp++;
        rtpush(TY_L);
        break;
    }
    case '=': {
        Type tb = rtpop(), ta = rtpop();
        if (ta != tb) { fprintf(stderr, "line %d: = type mismatch\n", cur_line); exit(1); }
        int64_t r;
        if (ta == TY_D) { double b = stack[--sp].d, a = stack[--sp].d; r = a == b; }
        else            { int64_t b = stack[--sp].l, a = stack[--sp].l; r = a == b; }
        stack[sp].l = r; sp++;
        rtpush(TY_L);
        break;
    }
    case '&': {
        /* memory read: p -- l */
        rtpop();
        void *ptr = stack[--sp].p;
        stack[sp].l = *(int64_t *)ptr; sp++;
        rtpush(TY_L);
        break;
    }
    case '#': {
        /* memory write: value p -- */
        Type tb = rtpop(), ta = rtpop();
        void *ptr = stack[--sp].p;
        if (ta == TY_D) {
            double v = stack[--sp].d;
            *(double *)ptr = v;
        } else {
            int64_t v = stack[--sp].l;
            *(int64_t *)ptr = v;
        }
        break;
    }
    default: break;
    }
}

static enum ExecResult exec_one(Token *t) {
    switch (t->type) {
    case T_NUM:
        if (t->ival) { stack[sp].l = (int64_t)t->num; rtpush(TY_L); }
        else         { stack[sp].d = t->num;           rtpush(TY_D); }
        sp++;
        break;
    case T_STR:
        stack[sp].p = store_string(t->text);
        rtpush(TY_P); sp++;
        break;
    case T_SYM:
        if (t->text[0] == '$') return EX_RETURN;
        if (t->text[0] == '~') return EX_LOOP;
        exec_sym(t->text[0]);
        break;
    case T_PRIM:
        exec_prim(t->text);
        break;
    case T_WORD: {
        Word *w = find_word(t->text);
        if (w) exec_body(w->body, w->len);
        else { fprintf(stderr, "line %d: unknown word: %s\n", t->line, t->text); exit(1); }
        break;
    }
    case T_MEMBER:
        /* module member access — not yet implemented */
        fprintf(stderr, "line %d: member access not yet implemented: %s\n", t->line, t->text);
        break;
    }
    return EX_OK;
}

/* Return stack */
#define RSTACK_MAX 1024

typedef struct { Token *toks; int len; int ip; } Frame;
static Frame rstack[RSTACK_MAX];
static int rsp = 0;

static void exec_body(Token *toks, int n) {
    if (rsp >= RSTACK_MAX) { fprintf(stderr, "line %d: return stack overflow\n", cur_line); exit(1); }
    Frame *f = &rstack[rsp++];
    f->toks = toks; f->len = n; f->ip = 0;

    while (f->ip < f->len) {
        Token *t = &f->toks[f->ip];
        cur_line = t->line;

        if (t->type == T_SYM && t->text[0] == '?') {
            /* pop condition (must be l type) */
            rtpop();
            int64_t cond = stack[--sp].l;
            f->ip++;
            if (f->ip >= f->len) break;

            int has_else = (f->ip + 1 < f->len &&
                            f->toks[f->ip + 1].type == T_SYM &&
                            f->toks[f->ip + 1].text[0] == '!');

            Token *chosen;
            if (has_else) {
                chosen = cond ? &f->toks[f->ip] :
                    (f->ip + 2 < f->len ? &f->toks[f->ip + 2] : NULL);
            } else {
                chosen = cond ? &f->toks[f->ip] : NULL;
            }

            if (chosen) {
                enum ExecResult r = exec_one(chosen);
                if (r == EX_RETURN) goto done;
                if (r == EX_LOOP) { f->ip = 0; continue; }
            }
            f->ip += has_else ? 3 : 1;
            continue;
        }

        enum ExecResult r = exec_one(t);
        if (r == EX_RETURN) goto done;
        if (r == EX_LOOP) { f->ip = 0; continue; }
        f->ip++;
    }
done:
    rsp--;
}

/* ============================================================
   File loading
   ============================================================ */

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

static char used_files[64][512];
static int used_count = 0;

static int already_used(const char *path) {
    for (int i = 0; i < used_count; i++)
        if (strcmp(used_files[i], path) == 0) return 1;
    return 0;
}

static void run_file(const char *path);

/* ============================================================
   Top-level: parse and execute
   ============================================================ */

static void exec_toplevel(Token *toks, int n) {
    int i = 0;
    while (i < n) {
        /* skip :use (not implemented yet, but don't crash) */
        if (toks[i].type == T_PRIM && strcmp(toks[i].text, ":use") == 0) {
            int line = toks[i].line;
            while (i < n && toks[i].line == line) i++;
            continue;
        }
        /* skip :bind */
        if (toks[i].type == T_PRIM && strcmp(toks[i].text, ":bind") == 0) {
            int line = toks[i].line;
            while (i < n && toks[i].line == line) i++;
            continue;
        }
        /* skip :struct */
        if (toks[i].type == T_PRIM && strcmp(toks[i].text, ":struct") == 0) {
            int line = toks[i].line;
            while (i < n && toks[i].line == line) i++;
            continue;
        }
        /* word definition: @ name sig body ; */
        if (toks[i].type == T_SYM && toks[i].text[0] == '@') {
            i++;
            if (i >= n || toks[i].type != T_WORD) {
                fprintf(stderr, "line %d: @ must be followed by a word name\n", toks[i-1].line);
                return;
            }
            if (dict_count >= DICT_MAX) { fprintf(stderr, "dictionary full\n"); return; }

            /* reuse slot if redefining */
            int slot = dict_count;
            for (int j = 0; j < dict_count; j++)
                if (strcmp(dict[j].name, toks[i].text) == 0) { slot = j; break; }

            Word *w = &dict[slot];
            strncpy(w->name, toks[i].text, 255);
            w->name[255] = '\0';
            i++;

            /* Parse signature: [letters] : [letters]
               Token stream can be: T_WORD "d" T_SYM ":" T_WORD "d"
               or: T_WORD "d" T_PRIM ":d" (when : follows type letter without space)
               Handle both cases. */
            char sig_str[SIG_MAX * 2 + 2];
            int si = 0;

            /* Collect input types */
            while (i < n && toks[i].type == T_WORD && strlen(toks[i].text) == 1 &&
                   strchr("ldp", toks[i].text[0])) {
                sig_str[si++] = toks[i].text[0];
                i++;
            }

            /* Expect T_SYM ":" or T_PRIM ":x" (compact form like ":d") */
            if (i < n && toks[i].type == T_SYM && toks[i].text[0] == ':') {
                sig_str[si++] = ':';
                i++;
            } else if (i < n && toks[i].type == T_PRIM &&
                       strlen(toks[i].text) == 2 && toks[i].text[0] == ':' &&
                       strchr("ldp", toks[i].text[1])) {
                /* Compact form ":d" — split into ":" + "d" */
                sig_str[si++] = ':';
                sig_str[si++] = toks[i].text[1];
                i++;
            } else {
                fprintf(stderr, "line %d: expected ':' in stack effect signature for '%s'\n",
                        cur_line, w->name);
                return;
            }

            /* Collect output types */
            while (i < n && toks[i].type == T_WORD && strlen(toks[i].text) == 1 &&
                   strchr("ldp", toks[i].text[0])) {
                sig_str[si++] = toks[i].text[0];
                i++;
            }
            sig_str[si] = '\0';

            if (!parse_sig(sig_str, &w->sig, cur_line)) return;

            /* Collect body until ; */
            w->len = 0;
            while (i < n) {
                if (toks[i].type == T_SYM && toks[i].text[0] == ';') { i++; break; }
                if (w->len >= WORD_BODY_MAX) { fprintf(stderr, "word body too long\n"); return; }
                w->body[w->len++] = toks[i++];
            }

            /* Check for recursion */
            w->is_recursive = 0;
            for (int j = 0; j < w->len; j++) {
                if (w->body[j].type == T_WORD && strcmp(w->body[j].text, w->name) == 0) {
                    w->is_recursive = 1;
                    break;
                }
            }

            /* Add to dictionary before verification so self-reference works */
            if (slot == dict_count) dict_count++;

            /* Verify stack effect */
            if (!verify_word(w)) {
                exit(1);
            }

            continue;
        }

        /* Loose code: execute directly */
        int start = i;
        while (i < n && !(toks[i].type == T_SYM && toks[i].text[0] == '@')
                      && !(toks[i].type == T_PRIM && strcmp(toks[i].text, ":use") == 0)
                      && !(toks[i].type == T_PRIM && strcmp(toks[i].text, ":bind") == 0)
                      && !(toks[i].type == T_PRIM && strcmp(toks[i].text, ":struct") == 0))
            i++;
        if (i > start) exec_body(&toks[start], i - start);
    }
}

static void run_file(const char *path) {
    char *src = read_file(path);
    if (!src) return;
    Token toks[TOK_MAX];
    int n = tokenize_all(src, toks, TOK_MAX);
    free(src);
    exec_toplevel(toks, n);
}

static void repl(void) {
    Token toks[TOK_MAX];
    int line_num = 1;
    char buf[4096];

    printf("zona> ");
    fflush(stdout);
    while (fgets(buf, sizeof(buf), stdin)) {
        int n = tokenize(buf, toks, TOK_MAX, line_num++);
        exec_toplevel(toks, n);
        printf("zona> ");
        fflush(stdout);
    }
    printf("\n");
}

/* ============================================================
   Main
   ============================================================ */

int main(int argc, char **argv) {
    srand((unsigned)time(NULL));
    if (argc > 1) {
        prog_argc = argc - 2;
        prog_argv = argv + 2;
        run_file(argv[1]);
    } else {
        repl();
    }
    return 0;
}
