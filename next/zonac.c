/*
 * Zona — QBE 编译器
 *
 * 栈效应签名 → 带类型的函数参数 → 寄存器传参。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>

/* ============================================================
   类型系统
   ============================================================ */

typedef enum { TY_L = 'l', TY_D = 'd', TY_P = 'p' } Type;

/* ============================================================
   Token (int/float distinguished via ival)
   ============================================================ */

enum TokenType { T_NUM, T_STR, T_SYM, T_PRIM, T_WORD, T_MEMBER };

typedef struct {
    enum TokenType type;
    char text[256];
    double num;
    int    ival;     /* 1 if int literal, 0 if float */
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
        if (*p == '_') break;
        toks[n].line = line_num;

        if (*p == '\'') {
            p++; int len = 0;
            while (*p && *p != '\'' && *p != '\n') {
                if (*p == '\\' && *(p+1)) { p++; switch (*p) {
                    case 'n': toks[n].text[len++] = '\n'; break;
                    case 't': toks[n].text[len++] = '\t'; break;
                    case '\\': toks[n].text[len++] = '\\'; break;
                    case '\'': toks[n].text[len++] = '\''; break;
                    default: toks[n].text[len++] = *p; break;
                }} else toks[n].text[len++] = *p;
                p++;
            }
            if (*p == '\'') p++;
            toks[n].text[len] = '\0';
            toks[n].type = T_STR; toks[n].ival = 0;
            n++; continue;
        }

        if (*p == '-' && isdigit((unsigned char)*(p+1))) {
            const char *s = p; p++;
            int has_dot = 0;
            while (isdigit((unsigned char)*p) || *p == '.') { if (*p == '.') has_dot = 1; p++; }
            int len = (int)(p - s);
            memcpy(toks[n].text, s, len); toks[n].text[len] = '\0';
            toks[n].type = T_NUM; toks[n].num = atof(toks[n].text); toks[n].ival = !has_dot;
            n++; continue;
        }

        if (isdigit((unsigned char)*p)) {
            const char *s = p;
            int has_dot = 0;
            while (isdigit((unsigned char)*p) || *p == '.') { if (*p == '.') has_dot = 1; p++; }
            int len = (int)(p - s);
            memcpy(toks[n].text, s, len); toks[n].text[len] = '\0';
            toks[n].type = T_NUM; toks[n].num = atof(toks[n].text); toks[n].ival = !has_dot;
            n++; continue;
        }

        if (strchr(SYMS, *p) && *p != '\'' && *p != '_') {
            toks[n].text[0] = *p; toks[n].text[1] = '\0';
            toks[n].type = T_SYM; toks[n].ival = 0;
            p++; n++; continue;
        }

        if (*p == ':' && isalpha((unsigned char)*(p+1))) {
            const char *s = p; p++;
            while (isalpha((unsigned char)*p) || isdigit((unsigned char)*p)) p++;
            int len = (int)(p - s);
            memcpy(toks[n].text, s, len); toks[n].text[len] = '\0';
            toks[n].type = T_PRIM; toks[n].ival = 0;
            n++; continue;
        }
        if (*p == ':') {
            toks[n].text[0] = ':'; toks[n].text[1] = '\0';
            toks[n].type = T_SYM; toks[n].ival = 0;
            p++; n++; continue;
        }

        if (isalpha((unsigned char)*p)) {
            const char *s = p;
            while (isalpha((unsigned char)*p) || isdigit((unsigned char)*p) || *p == '.') p++;
            int len = (int)(p - s);
            memcpy(toks[n].text, s, len); toks[n].text[len] = '\0';
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
        if (line_len >= (int)sizeof(line)) line_len = sizeof(line)-1;
        memcpy(line, p, line_len); line[line_len] = '\0';
        total += tokenize(line, toks+total, max-total, line_num);
        p += line_len; if (*p == '\n') p++;
        line_num++;
    }
    return total;
}

static char *read_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) { fprintf(stderr, "cannot open: %s\n", path); exit(1); }
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    char *src = malloc(sz+1); fread(src, 1, sz, fp); src[sz] = '\0'; fclose(fp);
    return src;
}

/* ============================================================
   Path utilities for :use
   ============================================================ */

static char current_dir[512] = ".";

static void dir_of(const char *path, char *out, int out_size) {
    strncpy(out, path, out_size-1); out[out_size-1] = '\0';
    char *last = strrchr(out, '/');
    if (last) *last = '\0'; else strcpy(out, ".");
}

static void resolve_path(const char *base, const char *rel, char *out, int out_size) {
    if (rel[0] == '/') { snprintf(out, out_size, "%s", rel); return; }
    snprintf(out, out_size, "%s/%s", base, rel);
    char *p;
    while ((p = strstr(out, "/./"))) memmove(p, p+2, strlen(p+2)+1);
    while ((p = strstr(out, "/../"))) {
        char *prev = p-1; while (prev > out && *prev != '/') prev--;
        if (prev >= out) memmove(prev, p+3, strlen(p+3)+1); else break;
    }
}

/* ============================================================
   Stack effect signature
   ============================================================ */

#define SIG_MAX 16

typedef struct {
    Type in[SIG_MAX], out[SIG_MAX];
    int  n_in, n_out;
} Sig;

static int parse_sig(const char *s, Sig *sig) {
    memset(sig, 0, sizeof(Sig));
    const char *colon = strchr(s, ':');
    if (!colon) return 0;
    int ni = 0;
    for (const char *p = s; p < colon; p++) {
        if (*p == 'l' || *p == 'd' || *p == 'p') sig->in[ni++] = (Type)*p;
        else return 0;
    }
    sig->n_in = ni;
    int no = 0;
    for (const char *p = colon+1; *p; p++) {
        if (*p == 'l' || *p == 'd' || *p == 'p') sig->out[no++] = (Type)*p;
        else return 0;
    }
    sig->n_out = no;
    return 1;
}

/* ============================================================
   Module system
   ============================================================ */

#define MODULE_MAX 64

typedef struct {
    char name[256];
    char file[512];  /* dedup by absolute path */
} Module;

static Module modules[MODULE_MAX];
static int    module_count = 0;

static Module *find_module(const char *name) {
    for (int i = module_count - 1; i >= 0; i--)
        if (strcmp(modules[i].name, name) == 0) return &modules[i];
    return NULL;
}

/* ============================================================
   Word definition
   ============================================================ */

#define DICT_MAX 1024
#define WORD_BODY_MAX 256

typedef struct {
    char name[256]; Sig sig;
    Token body[WORD_BODY_MAX]; int len;
    int compiled;
    Module *module;  /* NULL = global, else module-owned */
} Word;

static Word dict[DICT_MAX];
static int  dict_count = 0;

static Word *find_word(const char *name) {
    for (int i = dict_count-1; i >= 0; i--)
        if (strcmp(dict[i].name, name) == 0) return &dict[i];
    return NULL;
}

static Word *find_word_in_module(Module *m, const char *name) {
    for (int i = dict_count-1; i >= 0; i--)
        if (strcmp(dict[i].name, name) == 0 && dict[i].module == m)
            return &dict[i];
    return NULL;
}

/* ============================================================
   QBE IR emitter
   ============================================================ */

static FILE *out;
static int tmp_id = 0, lbl_id = 0, str_id = 0;

/* Virtual stack — typed temps */
#define VSTACK_MAX 256
static int  vstack[VSTACK_MAX];
static Type vtype[VSTACK_MAX];
static int  vsp = 0;

static int newtmp(void) { return tmp_id++; }
static int newlbl(void) { return lbl_id++; }

/* Pop from runtime stack — typed version. Stack stores as d, convert for l. */
static int emit_pop_typed(Type ty);

static void vpush(int t, Type ty) { vstack[vsp] = t; vtype[vsp] = ty; vsp++; }
static int  vpop(Type *ty) {
    if (vsp > 0) { vsp--; if (ty) *ty = vtype[vsp]; return vstack[vsp]; }
    int v = emit_pop_typed(TY_D);
    if (ty) *ty = TY_D;
    return v;
}
static int  vpeek(Type *ty) {
    if (vsp > 0) { if (ty) *ty = vtype[vsp-1]; return vstack[vsp-1]; }
    int v = emit_pop_typed(TY_D);
    vpush(v, TY_D);
    if (ty) *ty = TY_D;
    return v;
}

static char qt(Type t) { return t == TY_D ? 'd' : 'l'; }

/* flush virtual stack to runtime stack — stores as d for type uniformity */
static void vsync(void) {
    for (int i = 0; i < vsp; i++) {
        int si = newtmp(), off = newtmp(), off2 = newtmp(), addr = newtmp(), si2 = newtmp();
        fprintf(out, "    %%t%d =w loadw $sp\n", si);
        fprintf(out, "    %%t%d =l extsw %%t%d\n", off, si);
        fprintf(out, "    %%t%d =l mul %%t%d, 8\n", off2, off);
        fprintf(out, "    %%t%d =l add $stack, %%t%d\n", addr, off2);
        int st = vstack[i];
        if (vtype[i] != TY_D) {
            int cv = newtmp();
            fprintf(out, "    %%t%d =d sltof %%t%d\n", cv, st);
            fprintf(out, "    stored %%t%d, %%t%d\n", cv, addr);
        } else {
            fprintf(out, "    stored %%t%d, %%t%d\n", st, addr);
        }
        fprintf(out, "    %%t%d =w add %%t%d, 1\n", si2, si);
        fprintf(out, "    storew %%t%d, $sp\n", si2);
    }
    vsp = 0;
}

/* Pop from runtime stack — typed version. Stack stores as d, convert for l. */
int emit_pop_typed(Type ty) {
    int si = newtmp(), si2 = newtmp(), off = newtmp(), off2 = newtmp(), addr = newtmp(), v = newtmp();
    fprintf(out, "    %%t%d =w loadw $sp\n", si);
    fprintf(out, "    %%t%d =w sub %%t%d, 1\n", si2, si);
    fprintf(out, "    storew %%t%d, $sp\n", si2);
    fprintf(out, "    %%t%d =l extsw %%t%d\n", off, si2);
    fprintf(out, "    %%t%d =l mul %%t%d, 8\n", off2, off);
    fprintf(out, "    %%t%d =l add $stack, %%t%d\n", addr, off2);
    fprintf(out, "    %%t%d =d loadd %%t%d\n", v, addr);
    if (ty != TY_D) {
        int cv = newtmp();
        fprintf(out, "    %%t%d =l dtosi %%t%d\n", cv, v);
        return cv;
    }
    return v;
}

/* Pop from runtime stack (untyped — returns d) */
static int emit_pop(void) {
    return emit_pop_typed(TY_D);
}

/* ============================================================
   String literals
   ============================================================ */

#define STR_MAX 256
static struct { int id; char text[256]; } strs[STR_MAX];
static int str_count = 0;

static int add_str(const char *text) {
    for (int i = 0; i < str_count; i++)
        if (strcmp(strs[i].text, text) == 0) return strs[i].id;
    int id = str_id++;
    strncpy(strs[str_count].text, text, 255);
    strs[str_count].id = id; str_count++;
    return id;
}

/* ============================================================
   Emit helpers
   ============================================================ */

static void emit_data_section(void) {
    fprintf(out, "data $fmt_int = { b \"%%ld\\n\", b 0 }\n");
    fprintf(out, "data $fmt_flt = { b \"%%g\\n\", b 0 }\n");
    fprintf(out, "data $fmt_str = { b \"%%.*s\", b 0 }\n");
    for (int i = 0; i < str_count; i++) {
        fprintf(out, "data $str%d = { b \"", strs[i].id);
        for (const char *p = strs[i].text; *p; p++) {
            if (*p == '\n') fprintf(out, "\\n");
            else if (*p == '\\') fprintf(out, "\\\\");
            else if (*p == '"') fprintf(out, "\\\"");
            else fputc(*p, out);
        }
        fprintf(out, "\", b 0 }\n");
    }
    fprintf(out, "data $stack = { z 2048 }\n");
    fprintf(out, "data $sp = { w 0 }\n");
    fprintf(out, "data $prog_argc = { w 0 }\n");
    fprintf(out, "data $prog_argv = { l 0 }\n\n");
}

/* ============================================================
   Code generation — two passes
   ============================================================ */

static int cur_line;

/* Forward: generate one word from body tokens */
static int gen_word(Word *w);

/* Generate code for ONE token. Returns 1 on success. */
static int gen_token(Token *t) {
    switch (t->type) {
    case T_NUM: {
        int v = newtmp();
        if (t->ival) {
            fprintf(out, "    %%t%d =l copy %ld\n", v, (long)t->num);
            vpush(v, TY_L);
        } else {
            /* QBE requires d_ prefix with decimal point: d_5.0, d_3.5 */
            fprintf(out, "    %%t%d =d copy d_%.17g\n", v, t->num);
            vpush(v, TY_D);
        }
        break;
    }
    case T_STR: {
        int id = add_str(t->text);
        int ptr = newtmp();
        fprintf(out, "    %%t%d =l copy $str%d\n", ptr, id);
        vpush(ptr, TY_P);
        int len = newtmp();
        fprintf(out, "    %%t%d =l copy %ld\n", len, (long)strlen(t->text));
        vpush(len, TY_L);
        break;
    }
    case T_SYM: {
        char c = t->text[0];
        if (c == '+' || c == '-' || c == '*' || c == '/') {
            Type tb, ta; int b = vpop(&tb), a = vpop(&ta);
            int r = newtmp(); char q = qt(ta);
            if (c == '+') fprintf(out, "    %%t%d =%c add %%t%d, %%t%d\n", r, q, a, b);
            else if (c == '-') fprintf(out, "    %%t%d =%c sub %%t%d, %%t%d\n", r, q, a, b);
            else if (c == '*') fprintf(out, "    %%t%d =%c mul %%t%d, %%t%d\n", r, q, a, b);
            else {
                if (ta == TY_D) fprintf(out, "    %%t%d =d div %%t%d, %%t%d\n", r, a, b);
                else fprintf(out, "    %%t%d =l div %%t%d, %%t%d\n", r, a, b);
            }
            vpush(r, ta);
        } else if (c == '%') {
            Type tb, ta; int b = vpop(&tb), a = vpop(&ta);
            int r = newtmp();
            if (ta == TY_D) {
                int div = newtmp(), flr = newtmp(), mul = newtmp();
                fprintf(out, "    %%t%d =d div %%t%d, %%t%d\n", div, a, b);
                fprintf(out, "    %%t%d =d floor %%t%d\n", flr, div);
                fprintf(out, "    %%t%d =d mul %%t%d, %%t%d\n", mul, flr, b);
                fprintf(out, "    %%t%d =d sub %%t%d, %%t%d\n", r, a, mul);
            } else {
                fprintf(out, "    %%t%d =l rem %%t%d, %%t%d\n", r, a, b);
            }
            vpush(r, ta);
        } else if (c == '^') {
            Type tb, ta; int b = vpop(&tb), a = vpop(&ta);
            int pa = newtmp(), pb = newtmp(), pr = newtmp();
            if (ta == TY_D) {
                fprintf(out, "    %%t%d =d copy %%t%d\n", pa, a);
                fprintf(out, "    %%t%d =d copy %%t%d\n", pb, b);
            } else {
                fprintf(out, "    %%t%d =d sltof %%t%d\n", pa, a);
                fprintf(out, "    %%t%d =d sltof %%t%d\n", pb, b);
            }
            fprintf(out, "    %%t%d =d call $pow(d %%t%d, d %%t%d)\n", pr, pa, pb);
            int r = newtmp();
            if (ta == TY_D) fprintf(out, "    %%t%d =d copy %%t%d\n", r, pr);
            else fprintf(out, "    %%t%d =l dtosi %%t%d\n", r, pr);
            vpush(r, ta);
        } else if (c == '>' || c == '<' || c == '=') {
            Type tb, ta; int b = vpop(&tb), a = vpop(&ta);
            int cmp = newtmp(), r = newtmp();
            if (ta == TY_L) {
                if (c == '>') fprintf(out, "    %%t%d =w csgtl %%t%d, %%t%d\n", cmp, a, b);
                else if (c == '<') fprintf(out, "    %%t%d =w csltl %%t%d, %%t%d\n", cmp, a, b);
                else fprintf(out, "    %%t%d =w ceql %%t%d, %%t%d\n", cmp, a, b);
            } else {
                if (c == '>') fprintf(out, "    %%t%d =w cgtd %%t%d, %%t%d\n", cmp, a, b);
                else if (c == '<') fprintf(out, "    %%t%d =w cltd %%t%d, %%t%d\n", cmp, a, b);
                else fprintf(out, "    %%t%d =w ceqd %%t%d, %%t%d\n", cmp, a, b);
            }
            fprintf(out, "    %%t%d =l extsw %%t%d\n", r, cmp);
            vpush(r, TY_L);
        } else if (c == '&') {
            Type ta; int a = vpop(&ta);
            int r = newtmp();
            fprintf(out, "    %%t%d =l loadl %%t%d\n", r, a);
            vpush(r, TY_L);
        } else if (c == '#') {
            Type tb, ta; int b = vpop(&tb), a = vpop(&ta);
            char q = qt(ta);
            fprintf(out, "    store%c %%t%d, %%t%d\n", q, a, b);
        } else if (c == '.') {
            Type ty; int v = vpop(&ty);
            if (ty == TY_D) {
                int fi = newtmp();
                fprintf(out, "    %%t%d =l call $printf(l $fmt_flt, ..., d %%t%d)\n", fi, v);
            } else {
                int fi = newtmp();
                fprintf(out, "    %%t%d =l call $printf(l $fmt_int, ..., l %%t%d)\n", fi, v);
            }
        }
        break;
    }
    case T_PRIM:
        if (strcmp(t->text, ":dup") == 0) {
            Type ty; int v = vpeek(&ty), cp = newtmp();
            fprintf(out, "    %%t%d =%c copy %%t%d\n", cp, qt(ty), v);
            vpush(cp, ty);
        } else if (strcmp(t->text, ":drop") == 0) { vpop(NULL); }
        else if (strcmp(t->text, ":swap") == 0) {
            Type tb, ta; int b = vpop(&tb), a = vpop(&ta);
            vpush(b, tb); vpush(a, ta);
        } else if (strcmp(t->text, ":over") == 0) {
            Type tb, ta; int b = vpop(&tb), a = vpop(&ta);
            int cp = newtmp();
            fprintf(out, "    %%t%d =%c copy %%t%d\n", cp, qt(ta), a);
            vpush(a, ta); vpush(b, tb); vpush(cp, ta);
        } else if (strcmp(t->text, ":rot") == 0) {
            Type tc, tb, ta; int c = vpop(&tc), b = vpop(&tb), a = vpop(&ta);
            vpush(b, tb); vpush(c, tc); vpush(a, ta);
        } else if (strcmp(t->text, ":print") == 0) {
            Type ty; int v = vpop(&ty);
            if (ty == TY_D) {
                int fi = newtmp();
                fprintf(out, "    %%t%d =l call $printf(l $fmt_flt, ..., d %%t%d)\n", fi, v);
            } else {
                int fi = newtmp();
                fprintf(out, "    %%t%d =l call $printf(l $fmt_int, ..., l %%t%d)\n", fi, v);
            }
        } else if (strcmp(t->text, ":type") == 0) {
            Type ty_l, ty_p;
            int len = vpop(&ty_l), ptr = vpop(&ty_p);
            /* Output string via printf %.*s */
            fprintf(out, "    call $printf(l $fmt_str, ..., l %%t%d, l %%t%d)\n", len, ptr);
        } else if (strcmp(t->text, ":emit") == 0) {
            Type ty; int v = vpop(&ty);
            fprintf(out, "    call $putchar(w %%t%d)\n", v);
        } else if (strcmp(t->text, ":alloc") == 0) {
            Type ty; int n = vpop(&ty);
            int r = newtmp();
            fprintf(out, "    %%t%d =l call $malloc(l %%t%d)\n", r, n);
            vpush(r, TY_P);
        } else if (strcmp(t->text, ":free") == 0) {
            Type ty; int p = vpop(&ty);
            fprintf(out, "    call $free(l %%t%d)\n", p);
        } else if (strcmp(t->text, ":time") == 0) {
            int r = newtmp();
            fprintf(out, "    %%t%d =l call $time(l 0)\n", r);
            vpush(r, TY_L);
        } else if (strcmp(t->text, ":exit") == 0) {
            Type ty; int v = vpop(&ty);
            fprintf(out, "    call $exit(w %%t%d)\n", v);
        } else if (strcmp(t->text, ":key") == 0) {
            int r = newtmp();
            fprintf(out, "    %%t%d =l call $getchar()\n", r);
            vpush(r, TY_L);
        } else if (strcmp(t->text, ":rand") == 0) {
            Type ty; int n = vpop(&ty);
            int r = newtmp();
            fprintf(out, "    %%t%d =l call $rand()\n", r);
            int mod = newtmp();
            fprintf(out, "    %%t%d =l rem %%t%d, %%t%d\n", mod, r, n);
            vpush(mod, TY_L);
        } else if (strcmp(t->text, ":argc") == 0) {
            int r = newtmp();
            fprintf(out, "    %%t%d =w loadw $prog_argc\n", r);
            int r2 = newtmp();
            fprintf(out, "    %%t%d =l extsw %%t%d\n", r2, r);
            vpush(r2, TY_L);
        } else if (strcmp(t->text, ":argv") == 0) {
            /* index l -- ptr:l len:l  (p l on stack) */
            Type ty; int idx = vpop(&ty);
            int base = newtmp(), off = newtmp(), addr = newtmp(), sptr = newtmp();
            fprintf(out, "    %%t%d =l loadl $prog_argv\n", base);
            fprintf(out, "    %%t%d =l mul %%t%d, 8\n", off, idx);
            fprintf(out, "    %%t%d =l add %%t%d, %%t%d\n", addr, base, off);
            fprintf(out, "    %%t%d =l loadl %%t%d\n", sptr, addr);
            vpush(sptr, TY_P);
            int len = newtmp();
            fprintf(out, "    %%t%d =l call $strlen(l %%t%d)\n", len, sptr);
            vpush(len, TY_L);
        }
        break;
    case T_WORD: {
        Word *w = find_word(t->text);
        if (!w) { fprintf(stderr, "line %d: unknown word '%s'\n", t->line, t->text); exit(1); }
        int args[16]; Type arg_t[16];
        for (int i = w->sig.n_in - 1; i >= 0; i--) {
            if (vsp > 0) {
                args[i] = vpop(&arg_t[i]);
            } else {
                args[i] = emit_pop_typed(w->sig.in[i]);
                arg_t[i] = w->sig.in[i];
            }
        }
        if (w->sig.n_out == 1) {
            int r = newtmp();
            fprintf(out, "    %%t%d =%c call $zona_%s(", r, qt(w->sig.out[0]), w->name);
            for (int i = 0; i < w->sig.n_in; i++)
                fprintf(out, "%c %%t%d%s", qt(w->sig.in[i]), args[i], i < w->sig.n_in-1 ? ", " : "");
            fprintf(out, ")\n");
            vpush(r, w->sig.out[0]);
        } else {
            fprintf(out, "    call $zona_%s(", w->name);
            for (int i = 0; i < w->sig.n_in; i++)
                fprintf(out, "%c %%t%d%s", qt(w->sig.in[i]), args[i], i < w->sig.n_in-1 ? ", " : "");
            fprintf(out, ")\n");
            if (w->sig.n_out > 1) {
                for (int i = 0; i < w->sig.n_out; i++) {
                    int r = emit_pop_typed(w->sig.out[w->sig.n_out - 1 - i]);
                    vpush(r, w->sig.out[w->sig.n_out - 1 - i]);
                }
            }
        }
        break;
    }
    case T_MEMBER: {
        /* Walk dotted path: a.b.c.word → traverse modules a→b→c, find word in c */
        char path[256];
        strncpy(path, t->text, sizeof(path)); path[255] = '\0';

        /* Split into segments, find the word in the last module */
        Module *cur_mod = NULL;
        char *seg = path;
        char *next_dot;
        while ((next_dot = strchr(seg, '.')) != NULL) {
            *next_dot = '\0';
            Module *m = find_module(seg);
            if (!m) {
                fprintf(stderr, "line %d: unknown module '%s' in '%s'\n",
                        t->line, seg, t->text);
                exit(1);
            }
            cur_mod = m;
            seg = next_dot + 1;
        }
        /* seg now points to the word name */
        if (!cur_mod) {
            fprintf(stderr, "line %d: invalid member access '%s'\n", t->line, t->text);
            exit(1);
        }
        Word *w = find_word_in_module(cur_mod, seg);
        if (!w) {
            fprintf(stderr, "line %d: '%s' has no word '%s'\n", t->line, cur_mod->name, seg);
            exit(1);
        }

        int args[16]; Type arg_t[16];
        for (int i = w->sig.n_in - 1; i >= 0; i--) {
            if (vsp > 0) {
                args[i] = vpop(&arg_t[i]);
            } else {
                args[i] = emit_pop_typed(w->sig.in[i]);
                arg_t[i] = w->sig.in[i];
            }
        }
        if (w->sig.n_out == 1) {
            int r = newtmp();
            fprintf(out, "    %%t%d =%c call $zona_%s(", r, qt(w->sig.out[0]), w->name);
            for (int i = 0; i < w->sig.n_in; i++)
                fprintf(out, "%c %%t%d%s", qt(w->sig.in[i]), args[i], i < w->sig.n_in-1 ? ", " : "");
            fprintf(out, ")\n");
            vpush(r, w->sig.out[0]);
        } else {
            fprintf(out, "    call $zona_%s(", w->name);
            for (int i = 0; i < w->sig.n_in; i++)
                fprintf(out, "%c %%t%d%s", qt(w->sig.in[i]), args[i], i < w->sig.n_in-1 ? ", " : "");
            fprintf(out, ")\n");
            if (w->sig.n_out > 1) {
                for (int i = 0; i < w->sig.n_out; i++) {
                    int r = emit_pop_typed(w->sig.out[w->sig.n_out - 1 - i]);
                    vpush(r, w->sig.out[w->sig.n_out - 1 - i]);
                }
            }
        }
        break;
    }
    default: break;
    }
    return 1;
}

/* Generate code for a word body with control flow */
static int gen_word(Word *w) {
    w->compiled = 1;
    vsp = 0;  /* reset virtual stack */
    if (w->sig.n_out == 1)
        fprintf(out, "function %c $zona_%s(", qt(w->sig.out[0]), w->name);
    else
        fprintf(out, "function $zona_%s(", w->name);
    for (int i = 0; i < w->sig.n_in; i++)
        fprintf(out, "%c %%p%d%s", qt(w->sig.in[i]), i, i < w->sig.n_in-1 ? ", " : "");
    fprintf(out, ") {\n@Lentry\n");
    /* Copy inputs to virtual stack */
    for (int i = 0; i < w->sig.n_in; i++) {
        int t = newtmp();
        fprintf(out, "    %%t%d =%c copy %%p%d\n", t, qt(w->sig.in[i]), i);
        vpush(t, w->sig.in[i]);
    }
    /* First entry: jump to body directly (vstack already has inputs) */
    fprintf(out, "    jmp @Lbody\n");
    /* @Lreload: reached from ~ loop — reload values from runtime into vstack */
    fprintf(out, "@Lreload\n");
    vsp = 0;
    for (int i = w->sig.n_in - 1; i >= 0; i--) {
        int v = emit_pop_typed(w->sig.in[i]);
        vpush(v, w->sig.in[i]);
    }
    fprintf(out, "    jmp @Lbody\n");
    fprintf(out, "@Lbody\n");

    int ip = 0;
    while (ip < w->len) {
        Token *t = &w->body[ip];
        cur_line = t->line;

        /* ? conditional */
        if (t->type == T_SYM && t->text[0] == '?') {
            Type ty; int cond = vpop(&ty);
            int lbl_true = newlbl(), lbl_false = newlbl(), lbl_end = newlbl();
            fprintf(out, "    jnz %%t%d, @l%d, @l%d\n", cond, lbl_true, lbl_false);
            ip++;

            int has_else = (ip+1 < w->len && w->body[ip+1].type == T_SYM && w->body[ip+1].text[0] == '!');

            /* Save virtual stack state before branches (after condition popped) */
            int save_vsp = vsp;
            int save_vstack[VSTACK_MAX];
            Type save_vtype[VSTACK_MAX];
            memcpy(save_vstack, vstack, save_vsp * sizeof(int));
            memcpy(save_vtype, vtype, save_vsp * sizeof(Type));

            /* Track branch output (must match between branches) */
            int out_vsp = -1;
            Type out_vtype[VSTACK_MAX];

            /* True branch */
            fprintf(out, "@l%d\n", lbl_true);
            vsp = save_vsp;
            memcpy(vstack, save_vstack, save_vsp * sizeof(int));
            memcpy(vtype, save_vtype, save_vsp * sizeof(Type));
            if (ip < w->len) {
                if (w->body[ip].type == T_SYM && w->body[ip].text[0] == '$') {
                    if (w->sig.n_out > 1) vsync();
                    if (w->sig.n_out == 1) {
                        int v = (vsp > 0) ? vstack[vsp-1] : newtmp();
                        fprintf(out, "    ret %%t%d\n", v);
                    } else fprintf(out, "    ret\n");
                } else {
                    gen_token(&w->body[ip]);
                    out_vsp = vsp;
                    memcpy(out_vtype, vtype, vsp * sizeof(Type));
                    vsync();
                    fprintf(out, "    jmp @l%d\n", lbl_end);
                }
            }

            /* False branch */
            fprintf(out, "@l%d\n", lbl_false);
            vsp = save_vsp;
            memcpy(vstack, save_vstack, save_vsp * sizeof(int));
            memcpy(vtype, save_vtype, save_vsp * sizeof(Type));
            if (has_else && ip+2 < w->len) {
                if (w->body[ip+2].type == T_SYM && w->body[ip+2].text[0] == '$') {
                    if (w->sig.n_out > 1) vsync();
                    if (w->sig.n_out == 1) {
                        int v = (vsp > 0) ? vstack[vsp-1] : newtmp();
                        fprintf(out, "    ret %%t%d\n", v);
                    } else fprintf(out, "    ret\n");
                } else {
                    gen_token(&w->body[ip+2]);
                    out_vsp = vsp;
                    memcpy(out_vtype, vtype, vsp * sizeof(Type));
                    vsync();
                    fprintf(out, "    jmp @l%d\n", lbl_end);
                }
            } else {
                out_vsp = save_vsp;
                memcpy(out_vtype, save_vtype, save_vsp * sizeof(Type));
                vsync();
                fprintf(out, "    jmp @l%d\n", lbl_end);
            }

            /* Merge point — reload branch outputs from runtime stack */
            fprintf(out, "@l%d\n", lbl_end);
            vsp = 0;
            for (int i = 0; i < out_vsp; i++) {
                int v = emit_pop_typed(out_vtype[out_vsp - 1 - i]);
                vpush(v, out_vtype[out_vsp - 1 - i]);
            }
            /* Reverse vstack to restore original output order */
            for (int i = 0; i < out_vsp / 2; i++) {
                int tmp_t = vstack[i];
                Type tmp_ty = vtype[i];
                vstack[i] = vstack[out_vsp-1-i];
                vtype[i] = vtype[out_vsp-1-i];
                vstack[out_vsp-1-i] = tmp_t;
                vtype[out_vsp-1-i] = tmp_ty;
            }
            ip += has_else ? 3 : 1;
            continue;
        }

        /* $ return */
        if (t->type == T_SYM && t->text[0] == '$') {
            if (w->sig.n_out > 1) vsync();
            if (w->sig.n_out == 1) {
                Type ty; int v = vpop(&ty);
                fprintf(out, "    ret %%t%d\n", v);
            } else {
                fprintf(out, "    ret\n");
            }
            fprintf(out, "}\n\n");
            return 1;
        }

        /* ~ loop — save current state and restart body */
        if (t->type == T_SYM && t->text[0] == '~') {
            vsync();
            fprintf(out, "    jmp @Lreload\n");
            fprintf(out, "}\n\n");
            return 1;
        }

        gen_token(t);
        ip++;
    }

    /* End of function */
    if (w->sig.n_out > 1) vsync();
    if (w->sig.n_out == 1) {
        Type ty; int v = vpop(&ty);
        fprintf(out, "    ret %%t%d\n", v);
    } else {
        fprintf(out, "    ret\n");
    }
    fprintf(out, "}\n\n");
    return 1;
}

/* Generate pop_l helper */
static void emit_runtime(void) {
    /* pop_l: pop from d-typed stack, return l */
    fprintf(out, "function l $zona_pop_l() {\n@start\n");
    int si = newtmp(), si2 = newtmp(), off = newtmp(), off2 = newtmp(), addr = newtmp(), v = newtmp();
    fprintf(out, "    %%t%d =w loadw $sp\n", si);
    fprintf(out, "    %%t%d =w sub %%t%d, 1\n", si2, si);
    fprintf(out, "    storew %%t%d, $sp\n", si2);
    fprintf(out, "    %%t%d =l extsw %%t%d\n", off, si2);
    fprintf(out, "    %%t%d =l mul %%t%d, 8\n", off2, off);
    fprintf(out, "    %%t%d =l add $stack, %%t%d\n", addr, off2);
    fprintf(out, "    %%t%d =d loadd %%t%d\n", v, addr);
    int cv = newtmp();
    fprintf(out, "    %%t%d =l dtosi %%t%d\n", cv, v);
    fprintf(out, "    ret %%t%d\n}\n\n", cv);

    /* pop_d: pop from d-typed stack, return d */
    fprintf(out, "function d $zona_pop_d() {\n@start\n");
    int si_d = newtmp(), si2_d = newtmp(), off_d = newtmp(), off2_d = newtmp(), addr_d = newtmp(), v_d = newtmp();
    fprintf(out, "    %%t%d =w loadw $sp\n", si_d);
    fprintf(out, "    %%t%d =w sub %%t%d, 1\n", si2_d, si_d);
    fprintf(out, "    storew %%t%d, $sp\n", si2_d);
    fprintf(out, "    %%t%d =l extsw %%t%d\n", off_d, si2_d);
    fprintf(out, "    %%t%d =l mul %%t%d, 8\n", off2_d, off_d);
    fprintf(out, "    %%t%d =l add $stack, %%t%d\n", addr_d, off2_d);
    fprintf(out, "    %%t%d =d loadd %%t%d\n", v_d, addr_d);
    fprintf(out, "    ret %%t%d\n}\n\n", v_d);
}

static char used_files[64][512];
static int  used_count = 0;

static int already_used(const char *path) {
    for (int i = 0; i < used_count; i++)
        if (strcmp(used_files[i], path) == 0) return 1;
    return 0;
}

static void mark_used(const char *path) {
    if (used_count < 64) {
        strncpy(used_files[used_count], path, 511);
        used_files[used_count++][511] = '\0';
    }
}

/* Forward */
static void first_pass_tokens(Token *toks, int n, Module *owner);

/* Load a file and parse its word definitions into given module (NULL = global) */
static void load_file_into_module(const char *path, Module *owner) {
    char resolved[512];
    resolve_path(current_dir, path, resolved, sizeof(resolved));
    if (already_used(resolved)) return;
    mark_used(resolved);

    char *src = read_file(resolved);
    if (!src) return;

    /* save/restore current_dir for nested :use */
    char saved_dir[512];
    strncpy(saved_dir, current_dir, sizeof(saved_dir));
    dir_of(resolved, current_dir, sizeof(current_dir));

    Token toks[TOK_MAX];
    int n = tokenize_all(src, toks, TOK_MAX);
    free(src);
    first_pass_tokens(toks, n, owner);

    strncpy(current_dir, saved_dir, sizeof(current_dir));
}

/* ============================================================
   Main compilation flow
   ============================================================ */

/* First pass: collect definitions, handle :use, collect string literals */
static void first_pass_tokens(Token *toks, int n, Module *owner) {
    int i = 0;
    while (i < n) {
        /* :use name 'path' */
        if (toks[i].type == T_PRIM && strcmp(toks[i].text, ":use") == 0) {
            int line = toks[i].line; i++;
            if (i >= n || toks[i].line != line) { i++; continue; }

            if (toks[i].type == T_WORD && i+1 < n && toks[i+1].line == line
                && toks[i+1].type == T_STR) {
                /* Named import */
                char *alias = toks[i].text, *path = toks[i+1].text;
                i += 2;

                Module *m = find_module(alias);
                if (!m) {
                    if (module_count >= MODULE_MAX) {
                        fprintf(stderr, "line %d: too many modules\n", line); exit(1);
                    }
                    m = &modules[module_count++];
                    strcpy(m->name, alias); m->file[0] = '\0';
                }
                load_file_into_module(path, m);
            }
            while (i < n && toks[i].line == line) i++;
            continue;
        }

        /* skip :bind, :struct */
        if (toks[i].type == T_PRIM && (strcmp(toks[i].text, ":bind") == 0 ||
            strcmp(toks[i].text, ":struct") == 0)) {
            int line = toks[i].line;
            while (i < n && toks[i].line == line) i++;
            continue;
        }

        if (toks[i].type == T_SYM && toks[i].text[0] == '@') {
            i++;
            if (i >= n || toks[i].type != T_WORD) {
                fprintf(stderr, "line %d: @ must be followed by word name\n", toks[i-1].line);
                exit(1);
            }
            int slot = dict_count;
            for (int j = 0; j < dict_count; j++)
                if (strcmp(dict[j].name, toks[i].text) == 0) { slot = j; break; }
            Word *w = &dict[slot];
            strncpy(w->name, toks[i].text, 255); w->name[255] = '\0';
            w->module = owner;  /* assign to module */
            i++;

            char sig_str[64]; int si = 0;
            while (i < n && toks[i].type == T_WORD) {
                int ok = 1;
                for (const char *c = toks[i].text; *c; c++)
                    if (!strchr("ldp", *c)) { ok = 0; break; }
                if (!ok) break;
                strcpy(sig_str+si, toks[i].text); si += strlen(toks[i].text);
                i++;
            }
            if (i < n && toks[i].type == T_SYM && toks[i].text[0] == ':') { sig_str[si++] = ':'; i++; }
            else if (i < n && toks[i].type == T_PRIM && strlen(toks[i].text)==2 && toks[i].text[0]==':' && strchr("ldp", toks[i].text[1]))
                { sig_str[si++] = ':'; sig_str[si++] = toks[i].text[1]; i++; }
            else if (i < n && toks[i].type == T_PRIM && toks[i].text[0]==':') {
                /* Handle :ll, :ld, :pll etc — tokenizer combined : + types into one T_PRIM */
                int all_types = 1;
                for (const char *c = toks[i].text+1; *c; c++)
                    if (!strchr("ldp", *c)) { all_types = 0; break; }
                if (all_types) {
                    sig_str[si++] = ':';
                    for (const char *c = toks[i].text+1; *c; c++)
                        sig_str[si++] = *c;
                    i++;
                } else {
                    fprintf(stderr, "line %d: expected : in signature\n", toks[i-1].line);
                    exit(1);
                }
            }
            else { fprintf(stderr, "line %d: expected : in signature\n", toks[i-1].line); exit(1); }
            while (i < n && toks[i].type == T_WORD) {
                int ok = 1;
                for (const char *c = toks[i].text; *c; c++)
                    if (!strchr("ldp", *c)) { ok = 0; break; }
                if (!ok) break;
                strcpy(sig_str+si, toks[i].text); si += strlen(toks[i].text);
                i++;
            }
            sig_str[si] = '\0';
            if (!parse_sig(sig_str, &w->sig)) {
                fprintf(stderr, "line %d: invalid signature '%s'\n", toks[i-1].line, sig_str);
                exit(1);
            }
            w->len = 0;
            while (i < n) {
                if (toks[i].type == T_SYM && toks[i].text[0] == ';') { i++; break; }
                if (w->len >= WORD_BODY_MAX) { fprintf(stderr, "word body too long\n"); exit(1); }
                w->body[w->len++] = toks[i++];
            }
            w->compiled = 0;
            for (int j = 0; j < w->len; j++)
                if (w->body[j].type == T_STR) add_str(w->body[j].text);
            if (slot == dict_count) dict_count++;
            continue;
        }
        /* Collect string literals from loose code */
        if (toks[i].type == T_STR) add_str(toks[i].text);
        i++;
    }
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "Usage: zonac <file.zona> [-o output]\n"); return 1; }

    char resolved[512];
    if (argv[1][0] == '/') strcpy(resolved, argv[1]);
    else { getcwd(resolved, sizeof(resolved)); strcat(resolved, "/"); strcat(resolved, argv[1]); }
    dir_of(resolved, current_dir, sizeof(current_dir));

    char *src = read_file(argv[1]);
    Token toks[TOK_MAX];
    int n = tokenize_all(src, toks, TOK_MAX);
    free(src);

    first_pass_tokens(toks, n, NULL);

    char out_name[512] = "out.ssa";
    char exe_name[512] = "out";
    for (int i = 2; i < argc; i++)
        if (strcmp(argv[i], "-o") == 0 && i+1 < argc) {
            snprintf(exe_name, sizeof(exe_name), "%s", argv[i+1]);
            snprintf(out_name, sizeof(out_name), "%s.ssa", argv[i+1]);
        }

    out = fopen(out_name, "w");
    if (!out) { perror("fopen"); return 1; }

    emit_data_section();
    emit_runtime();

    /* Generate IR for all words */
    for (int i = 0; i < dict_count; i++)
        if (!dict[i].compiled) gen_word(&dict[i]);

    /* Generate main function for loose code execution */
    vsp = 0;  /* reset virtual stack for loose code */
    /* Walk toks again and execute loose (non-@) code */
    int has_main = 0;
    fprintf(out, "export function w $main(w %%argc, l %%argv) {\n@start\n");
    /* Save argc/argv to globals for :argc/:argv primitives */
    fprintf(out, "    storew %%argc, $prog_argc\n");
    fprintf(out, "    storel %%argv, $prog_argv\n");
    int ip = 0;
    int local_line = 1;
    while (ip < n) {
        if (toks[ip].type == T_PRIM && (strcmp(toks[ip].text, ":use") == 0 ||
            strcmp(toks[ip].text, ":bind") == 0 || strcmp(toks[ip].text, ":struct") == 0)) {
            int line = toks[ip].line;
            while (ip < n && toks[ip].line == line) ip++;
            continue;
        }
        if (toks[ip].type == T_SYM && toks[ip].text[0] == '@') {
            /* Skip word definition — already compiled */
            ip += 2; /* skip @, name */
            /* skip signature */
            while (ip < n && !(toks[ip].type == T_SYM && toks[ip].text[0] == ';')) {
                if (toks[ip].type == T_SYM && toks[ip].text[0] == '@') break;
                ip++;
            }
            if (ip < n && toks[ip].type == T_SYM && toks[ip].text[0] == ';') ip++;
            continue;
        }
        /* Loose code token */
        cur_line = toks[ip].line;
        gen_token(&toks[ip]);
        has_main = 1;
        ip++;
    }
    if (has_main) vsync();
    fprintf(out, "    ret 0\n}\n");

    fclose(out);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "qbe -o %s.s %s && cc %s.s -o %s -lm",
             exe_name, out_name, exe_name, exe_name);
    int ret = system(cmd);
    if (ret != 0) { fprintf(stderr, "Compilation failed\n"); return 1; }
    fprintf(stderr, "OK: %s\n", exe_name);
    return 0;
}
