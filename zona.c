#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* ---- Token ---- */
enum TokenType { T_NUM, T_STR, T_SYM, T_PRIM, T_WORD };

typedef struct {
    enum TokenType type;
    char text[256];
    double num;
} Token;

/* ---- Tokenizer ---- */
static const char *SYMS = "@;$?!~&#+-*/%^><=._'";

static int tokenize(const char *line, Token *toks, int max) {
    int n = 0;
    const char *p = line;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '\n') break;
        if (*p == '_') break;

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

        if (*p == '-' && isdigit((unsigned char)*(p+1))) {
            const char *s = p; p++;
            while (isdigit((unsigned char)*p) || *p == '.') p++;
            int len = (int)(p - s);
            memcpy(toks[n].text, s, len); toks[n].text[len] = '\0';
            toks[n].type = T_NUM; toks[n].num = atof(toks[n].text);
            n++; continue;
        }

        if (isdigit((unsigned char)*p)) {
            const char *s = p;
            while (isdigit((unsigned char)*p) || *p == '.') p++;
            int len = (int)(p - s);
            memcpy(toks[n].text, s, len); toks[n].text[len] = '\0';
            toks[n].type = T_NUM; toks[n].num = atof(toks[n].text);
            n++; continue;
        }

        if (strchr(SYMS, *p) && *p != '\'' && *p != '_') {
            toks[n].text[0] = *p; toks[n].text[1] = '\0';
            toks[n].type = T_SYM; p++;
            n++; continue;
        }

        if (*p == ':' && isalpha((unsigned char)*(p+1))) {
            const char *s = p; p++;
            while (isalpha((unsigned char)*p)) p++;
            int len = (int)(p - s);
            memcpy(toks[n].text, s, len); toks[n].text[len] = '\0';
            toks[n].type = T_PRIM;
            n++; continue;
        }

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

/* ---- Dictionary (user-defined words) ---- */
#define DICT_MAX 256
#define WORD_BODY_MAX 256

typedef struct {
    char name[256];
    Token body[WORD_BODY_MAX];
    int len;
} Word;

static Word dict[DICT_MAX];
static int dict_count = 0;

static Word *find_word(const char *name) {
    for (int i = dict_count - 1; i >= 0; i--)
        if (strcmp(dict[i].name, name) == 0) return &dict[i];
    return NULL;
}

/* ---- Memory (cell-based) ---- */
#define MEM_CELLS 65536
static double mem[MEM_CELLS];
static int here = 0;

/* Store a string into memory, push addr and length */
static void store_str(const char *s) {
    int len = (int)strlen(s);
    int addr = here;
    for (int i = 0; i < len; i++) {
        if (here >= MEM_CELLS) { fprintf(stderr, "out of memory\n"); return; }
        mem[here++] = (unsigned char)s[i];
    }
    push(addr);
    push(len);
}

/* ---- Return stack (for nested word calls) ---- */
#define RSTACK_MAX 256
typedef struct { Token *toks; int len; int ip; } Frame;
static Frame rstack[RSTACK_MAX];
static int rsp = 0;

/* ---- Token execution ---- */
/* Return: 0=normal, 1=return($), 2=loop(~) */
enum ExecResult { EX_OK, EX_RETURN, EX_LOOP };

static void exec_body(Token *toks, int n);

static void exec_prim(const char *name) {
    if (strcmp(name, ":dup") == 0) push(peek());
    else if (strcmp(name, ":drop") == 0) pop();
    else if (strcmp(name, ":swap") == 0) { double b = pop(), a = pop(); push(b); push(a); }
    else if (strcmp(name, ":over") == 0) { double b = pop(), a = pop(); push(a); push(b); push(a); }
    else if (strcmp(name, ":rot") == 0) { double c = pop(), b = pop(), a = pop(); push(b); push(c); push(a); }
    else if (strcmp(name, ":here") == 0) push(here);
    else if (strcmp(name, ":allot") == 0) {
        int n = (int)pop();
        if (here + n > MEM_CELLS) fprintf(stderr, "out of memory\n");
        else here += n;
    }
    else if (strcmp(name, ":type") == 0) {
        int len = (int)pop(), addr = (int)pop();
        for (int i = 0; i < len; i++) putchar((int)mem[addr + i]);
    }
    else if (strcmp(name, ":emit") == 0) { putchar((int)pop()); }
    else if (strcmp(name, ":stack") == 0) {
        printf("<%d>", sp);
        for (int i = 0; i < sp; i++) {
            double v = stack[i];
            if (v == (long)v) printf(" %ld", (long)v);
            else printf(" %g", v);
        }
        putchar('\n');
    }
    else fprintf(stderr, "unknown primitive: %s\n", name);
}

static void exec_sym(char c) {
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
            if (v == (long)v) printf("%ld\n", (long)v);
            else printf("%g\n", v);
            break;
        }
        case '&': { int addr = (int)pop();
            if (addr < 0 || addr >= MEM_CELLS) { fprintf(stderr, "bad address: %d\n", addr); push(0); }
            else push(mem[addr]); break; }
        case '#': { int addr = (int)pop(); double val = pop();
            if (addr < 0 || addr >= MEM_CELLS) fprintf(stderr, "bad address: %d\n", addr);
            else mem[addr] = val; break; }
        default: break;
    }
}

/* Execute a single token in-place. Returns EX_RETURN for $, EX_LOOP for ~, EX_OK otherwise. */
static enum ExecResult exec_one(Token *t) {
    switch (t->type) {
        case T_NUM: push(t->num); return EX_OK;
        case T_STR: store_str(t->text); return EX_OK;
        case T_SYM: {
            char c = t->text[0];
            if (c == '$') return EX_RETURN;
            if (c == '~') return EX_LOOP;
            exec_sym(c);
            return EX_OK;
        }
        case T_PRIM: exec_prim(t->text); return EX_OK;
        case T_WORD: {
            Word *w = find_word(t->text);
            if (w) exec_body(w->body, w->len);
            else fprintf(stderr, "unknown word: %s\n", t->text);
            return EX_OK;
        }
    }
    return EX_OK;
}

static void exec_body(Token *toks, int n) {
    if (rsp >= RSTACK_MAX) { fprintf(stderr, "return stack overflow\n"); return; }
    Frame *f = &rstack[rsp++];
    f->toks = toks; f->len = n; f->ip = 0;

    while (f->ip < f->len) {
        Token *t = &f->toks[f->ip];

        /* handle ? inline for correct $ and ~ behavior */
        if (t->type == T_SYM && t->text[0] == '?') {
            double cond = pop();
            f->ip++;
            if (f->ip >= f->len) break;

            int has_else = (f->ip + 2 <= f->len &&
                            f->toks[f->ip + 1].type == T_SYM &&
                            f->toks[f->ip + 1].text[0] == '!');
            Token *chosen = has_else
                ? (cond ? &f->toks[f->ip] : &f->toks[f->ip + 2])
                : (cond ? &f->toks[f->ip] : NULL);

            if (chosen) {
                enum ExecResult r = exec_one(chosen);
                if (r == EX_RETURN) goto done;
                if (r == EX_LOOP) { f->ip = 0; continue; }
            }
            f->ip += has_else ? 3 : 1;
            continue;
        }

        /* normal token execution */
        enum ExecResult r = exec_one(t);
        if (r == EX_RETURN) goto done;
        if (r == EX_LOOP) { f->ip = 0; continue; }
        f->ip++;
    }
done:
    rsp--;
}

/* Top-level: handles @ definitions and executes top-level code */
static void exec_line(Token *toks, int n) {
    int i = 0;
    while (i < n) {
        if (toks[i].type == T_SYM && toks[i].text[0] == '@') {
            i++;
            if (i >= n || toks[i].type != T_WORD) {
                fprintf(stderr, "@ must be followed by a word name\n"); return;
            }
            if (dict_count >= DICT_MAX) { fprintf(stderr, "dictionary full\n"); return; }
            Word *w = &dict[dict_count];
            strncpy(w->name, toks[i].text, 255);
            i++;
            w->len = 0;
            while (i < n) {
                if (toks[i].type == T_SYM && toks[i].text[0] == ';') { i++; break; }
                if (w->len >= WORD_BODY_MAX) { fprintf(stderr, "word body too long\n"); return; }
                w->body[w->len++] = toks[i++];
            }
            dict_count++;
            continue;
        }
        int start = i;
        while (i < n && !(toks[i].type == T_SYM && toks[i].text[0] == '@'))
            i++;
        Word tmp = {.len = i - start};
        memcpy(tmp.body, &toks[start], tmp.len * sizeof(Token));
        exec_body(tmp.body, tmp.len);
    }
}

/* ---- Tokenize entire source (multi-line) ---- */
#define TOK_MAX 4096

static int tokenize_all(const char *src, Token *toks, int max) {
    int total = 0;
    const char *p = src;
    while (*p && total < max) {
        const char *eol = strchr(p, '\n');
        int line_len = eol ? (int)(eol - p) : (int)strlen(p);
        char line[4096];
        if (line_len >= (int)sizeof(line)) line_len = sizeof(line) - 1;
        memcpy(line, p, line_len);
        line[line_len] = '\0';
        total += tokenize(line, toks + total, max - total);
        p += line_len;
        if (*p == '\n') p++;
    }
    return total;
}

/* ---- Main ---- */

static void run_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) { fprintf(stderr, "cannot open: %s\n", path); exit(1); }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *src = malloc(sz + 1);
    fread(src, 1, sz, fp);
    src[sz] = '\0';
    fclose(fp);

    Token toks[TOK_MAX];
    int n = tokenize_all(src, toks, TOK_MAX);
    free(src);
    exec_line(toks, n);
}

static void repl(void) {
    char line[4096];
    Token toks[TOK_MAX];
    printf("zona> ");
    while (fgets(line, sizeof(line), stdin)) {
        int n = tokenize(line, toks, TOK_MAX);
        exec_line(toks, n);
        printf("zona> ");
    }
    printf("\n");
}

int main(int argc, char **argv) {
    if (argc > 1) run_file(argv[1]);
    else repl();
    return 0;
}
