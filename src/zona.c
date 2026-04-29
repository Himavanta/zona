#include "zona.h"
#include <readline/readline.h>
#include <readline/history.h>

/* ============================================================
   Data stack
   ============================================================ */

#define STACK_MAX 256
static double stack[STACK_MAX];
static int sp = 0;
static int cur_line = 0;

static void push(double v) {
    if (sp >= STACK_MAX) { fprintf(stderr, "line %d: stack overflow\n", cur_line); return; }
    stack[sp++] = v;
}
static double pop(void) {
    if (sp <= 0) { fprintf(stderr, "line %d: stack underflow\n", cur_line); return 0; }
    return stack[--sp];
}
static double peek(void) {
    if (sp <= 0) { fprintf(stderr, "line %d: stack underflow\n", cur_line); return 0; }
    return stack[sp - 1];
}

/* ============================================================
   Dictionary
   ============================================================ */

static Word dict[DICT_MAX];
static int dict_count = 0;

static Word *find_word(const char *name) {
    for (int i = dict_count - 1; i >= 0; i--)
        if (strcmp(dict[i].name, name) == 0) return &dict[i];
    return NULL;
}

/* ============================================================
   Memory
   ============================================================ */

#define MEM_CELLS 65536
static double mem[MEM_CELLS];
static int here = 0;

#define HEAP_MAX 1024
static struct { double *ptr; int size; int addr; } heap[HEAP_MAX];
static int heap_count = 0;
static int heap_next = MEM_CELLS;

static double *mem_at(int addr) {
    if (addr >= 0 && addr < MEM_CELLS) return &mem[addr];
    for (int i = 0; i < heap_count; i++)
        if (addr >= heap[i].addr && addr < heap[i].addr + heap[i].size)
            return &heap[i].ptr[addr - heap[i].addr];
    return NULL;
}

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

static void mem_to_str(int addr, int len, char *buf, int buf_size) {
    if (len >= buf_size) len = buf_size - 1;
    for (int i = 0; i < len; i++) buf[i] = (char)(int)mem[addr + i];
    buf[len] = '\0';
}

/* ============================================================
   Program arguments & file handles
   ============================================================ */

static int prog_argc = 0;
static char **prog_argv = NULL;

#define FHANDLE_MAX 16
static FILE *fhandles[FHANDLE_MAX];
static int fhandle_count = 0;

/* ============================================================
   Return stack
   ============================================================ */

#define RSTACK_MAX 256
typedef struct { Token *toks; int len; int ip; } Frame;
static Frame rstack[RSTACK_MAX];
static int rsp = 0;

/* ============================================================
   Primitives
   ============================================================ */

enum ExecResult { EX_OK, EX_RETURN, EX_LOOP };
static void exec_body(Token *toks, int n);

static void exec_prim(const char *name) {
    /* stack ops */
    if (strcmp(name, ":dup") == 0) { push(peek()); return; }
    if (strcmp(name, ":drop") == 0) { pop(); return; }
    if (strcmp(name, ":swap") == 0) { double b = pop(), a = pop(); push(b); push(a); return; }
    if (strcmp(name, ":over") == 0) { double b = pop(), a = pop(); push(a); push(b); push(a); return; }
    if (strcmp(name, ":rot") == 0) { double c = pop(), b = pop(), a = pop(); push(b); push(c); push(a); return; }

    /* memory */
    if (strcmp(name, ":here") == 0) { push(here); return; }
    if (strcmp(name, ":allot") == 0) {
        int n = (int)pop();
        if (here + n > MEM_CELLS) fprintf(stderr, "line %d: out of memory\n", cur_line);
        else here += n;
        return;
    }
    if (strcmp(name, ":alloc") == 0) {
        int n = (int)pop();
        if (heap_count >= HEAP_MAX) { fprintf(stderr, "line %d: heap full\n", cur_line); push(0); return; }
        double *p = calloc(n, sizeof(double));
        if (!p) { fprintf(stderr, "line %d: alloc failed\n", cur_line); push(0); return; }
        heap[heap_count] = (typeof(heap[0])){p, n, heap_next};
        push(heap_next);
        heap_next += n;
        heap_count++;
        return;
    }
    if (strcmp(name, ":free") == 0) {
        int addr = (int)pop();
        for (int i = 0; i < heap_count; i++) {
            if (heap[i].addr == addr) {
                free(heap[i].ptr);
                heap[i] = heap[--heap_count];
                return;
            }
        }
        fprintf(stderr, "line %d: bad free: %d\n", cur_line, addr);
        return;
    }

    /* io */
    if (strcmp(name, ":type") == 0) {
        int len = (int)pop(), addr = (int)pop();
        for (int i = 0; i < len; i++) putchar((int)mem[addr + i]);
        return;
    }
    if (strcmp(name, ":emit") == 0) { putchar((int)pop()); return; }
    if (strcmp(name, ":key") == 0) { push((double)getchar()); return; }

    /* file io */
    if (strcmp(name, ":fopen") == 0) {
        int mlen = (int)pop(), maddr = (int)pop();
        int plen = (int)pop(), paddr = (int)pop();
        char path[512], mode[16];
        mem_to_str(paddr, plen, path, sizeof(path));
        mem_to_str(maddr, mlen, mode, sizeof(mode));
        if (fhandle_count >= FHANDLE_MAX) { fprintf(stderr, "line %d: too many open files\n", cur_line); push(-1); return; }
        FILE *fp = fopen(path, mode);
        if (!fp) { fprintf(stderr, "line %d: cannot open: %s\n", cur_line, path); push(-1); return; }
        fhandles[fhandle_count] = fp;
        push(fhandle_count++);
        return;
    }
    if (strcmp(name, ":fclose") == 0) {
        int h = (int)pop();
        if (h >= 0 && h < fhandle_count && fhandles[h]) { fclose(fhandles[h]); fhandles[h] = NULL; }
        else fprintf(stderr, "line %d: bad file handle: %d\n", cur_line, h);
        return;
    }
    if (strcmp(name, ":fread") == 0) {
        int h = (int)pop();
        if (h >= 0 && h < fhandle_count && fhandles[h]) { int c = fgetc(fhandles[h]); push(c == EOF ? -1 : c); }
        else { fprintf(stderr, "line %d: bad file handle: %d\n", cur_line, h); push(-1); }
        return;
    }
    if (strcmp(name, ":fwrite") == 0) {
        int h = (int)pop(), c = (int)pop();
        if (h >= 0 && h < fhandle_count && fhandles[h]) fputc(c, fhandles[h]);
        else fprintf(stderr, "line %d: bad file handle: %d\n", cur_line, h);
        return;
    }

    /* system */
    if (strcmp(name, ":time") == 0) { push((double)time(NULL)); return; }
    if (strcmp(name, ":rand") == 0) { push((double)(rand() % (int)pop())); return; }
    if (strcmp(name, ":exit") == 0) { exit((int)pop()); }
    if (strcmp(name, ":argc") == 0) { push(prog_argc); return; }
    if (strcmp(name, ":argv") == 0) {
        int idx = (int)pop();
        if (idx >= 0 && idx < prog_argc) store_str(prog_argv[idx]);
        else { fprintf(stderr, "line %d: argv index out of range: %d\n", cur_line, idx); push(0); push(0); }
        return;
    }

    /* C memory peek/poke */
    if (strcmp(name, ":peek8") == 0) { unsigned char *p = (unsigned char *)(long)pop(); push(*p); return; }
    if (strcmp(name, ":peek32") == 0) { int *p = (int *)(long)pop(); push(*p); return; }
    if (strcmp(name, ":peek64") == 0) { long *p = (long *)(long)pop(); push(*p); return; }
    if (strcmp(name, ":peekd") == 0) { double *p = (double *)(long)pop(); push(*p); return; }
    if (strcmp(name, ":poke8") == 0) { unsigned char *p = (unsigned char *)(long)pop(); *p = (unsigned char)(int)pop(); return; }
    if (strcmp(name, ":poke32") == 0) { int *p = (int *)(long)pop(); *p = (int)pop(); return; }
    if (strcmp(name, ":poke64") == 0) { long *p = (long *)(long)pop(); *p = (long)pop(); return; }
    if (strcmp(name, ":poked") == 0) { double *p = (double *)(long)pop(); *p = pop(); return; }

    /* debug */
    if (strcmp(name, ":stack") == 0) {
        printf("<%d>", sp);
        for (int i = 0; i < sp; i++) {
            double v = stack[i];
            if (v == (long)v) printf(" %ld", (long)v);
            else printf(" %g", v);
        }
        putchar('\n');
        return;
    }

    fprintf(stderr, "line %d: unknown primitive: %s\n", cur_line, name);
}

/* ============================================================
   Symbol execution
   ============================================================ */

static void exec_sym(char c) {
    double a, b, *p;
    switch (c) {
        case '+': b = pop(); a = pop(); push(a + b); break;
        case '-': b = pop(); a = pop(); push(a - b); break;
        case '*': b = pop(); a = pop(); push(a * b); break;
        case '/': b = pop(); a = pop();
            if (b == 0) { fprintf(stderr, "line %d: division by zero\n", cur_line); break; }
            push(a / b); break;
        case '%': b = pop(); a = pop(); push(fmod(a, b)); break;
        case '^': b = pop(); a = pop(); push(pow(a, b)); break;
        case '>': b = pop(); a = pop(); push(a > b ? 1 : 0); break;
        case '<': b = pop(); a = pop(); push(a < b ? 1 : 0); break;
        case '=': b = pop(); a = pop(); push(a == b ? 1 : 0); break;
        case '.': a = pop();
            if (a == (long)a) printf("%ld\n", (long)a);
            else printf("%g\n", a);
            break;
        case '&': p = mem_at((int)pop());
            if (p) push(*p);
            else { fprintf(stderr, "line %d: bad address\n", cur_line); push(0); }
            break;
        case '#': { int addr = (int)pop(); double val = pop();
            p = mem_at(addr);
            if (p) *p = val;
            else fprintf(stderr, "line %d: bad address\n", cur_line);
            break; }
        default: break;
    }
}

/* ============================================================
   Interpreter
   ============================================================ */

static enum ExecResult exec_one(Token *t) {
    switch (t->type) {
        case T_NUM: push(t->num); return EX_OK;
        case T_STR: store_str(t->text); return EX_OK;
        case T_SYM:
            if (t->text[0] == '$') return EX_RETURN;
            if (t->text[0] == '~') return EX_LOOP;
            exec_sym(t->text[0]);
            return EX_OK;
        case T_PRIM: exec_prim(t->text); return EX_OK;
        case T_WORD: {
            Word *w = find_word(t->text);
            if (w) exec_body(w->body, w->len);
            else fprintf(stderr, "line %d: unknown word: %s\n", cur_line, t->text);
            return EX_OK;
        }
    }
    return EX_OK;
}

static void exec_body(Token *toks, int n) {
    if (rsp >= RSTACK_MAX) { fprintf(stderr, "line %d: return stack overflow\n", cur_line); return; }
    Frame *f = &rstack[rsp++];
    f->toks = toks; f->len = n; f->ip = 0;

    while (f->ip < f->len) {
        Token *t = &f->toks[f->ip];
        cur_line = t->line;

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

        enum ExecResult r = exec_one(t);
        if (r == EX_RETURN) goto done;
        if (r == EX_LOOP) { f->ip = 0; continue; }
        f->ip++;
    }
done:
    rsp--;
}

/* ============================================================
   File loading & :use
   ============================================================ */

#define USE_MAX 64
static char used_files[USE_MAX][512];
static int used_count = 0;
static char current_dir[512] = ".";

static int already_used(const char *path) {
    for (int i = 0; i < used_count; i++)
        if (strcmp(used_files[i], path) == 0) return 1;
    return 0;
}

static void resolve_path(const char *base_dir, const char *rel, char *out, int out_size) {
    if (rel[0] == '/') { snprintf(out, out_size, "%s", rel); return; }
    snprintf(out, out_size, "%s/%s", base_dir, rel);
    char *p;
    while ((p = strstr(out, "/./")) != NULL)
        memmove(p, p + 2, strlen(p + 2) + 1);
    while ((p = strstr(out, "/../")) != NULL) {
        char *prev = p - 1;
        while (prev > out && *prev != '/') prev--;
        if (prev >= out) memmove(prev, p + 3, strlen(p + 3) + 1);
        else break;
    }
}

static void dir_of(const char *path, char *out, int out_size) {
    strncpy(out, path, out_size - 1);
    out[out_size - 1] = '\0';
    char *last = strrchr(out, '/');
    if (last) *last = '\0';
    else strcpy(out, ".");
}

static void run_file_with_dir(const char *path);

/* ============================================================
   Top-level execution
   ============================================================ */

static void exec_line(Token *toks, int n) {
    int i = 0;
    while (i < n) {
        if (toks[i].type == T_PRIM && strcmp(toks[i].text, ":use") == 0) {
            if (!validate_use(toks, n, i)) return;
            i++;
            const char *rel = toks[i].text;
            if (!(rel[0] == '.' && (rel[1] == '/' || (rel[1] == '.' && rel[2] == '/')))) {
                fprintf(stderr, ":use path must start with ./ or ../\n"); return;
            }
            char resolved[512];
            resolve_path(current_dir, rel, resolved, sizeof(resolved));
            if (!already_used(resolved)) {
                if (used_count >= USE_MAX) { fprintf(stderr, "too many :use files\n"); return; }
                strncpy(used_files[used_count++], resolved, 511);
                run_file_with_dir(resolved);
            }
            i++; continue;
        }
        if (toks[i].type == T_PRIM && strcmp(toks[i].text, ":bind") == 0) {
            /* interpreter ignores :bind, skip all tokens on this line */
            int line = toks[i].line;
            while (i < n && toks[i].line == line) i++;
            continue;
        }
        if (toks[i].type == T_SYM && toks[i].text[0] == '@') {
            i++;
            if (i >= n || toks[i].type != T_WORD) {
                fprintf(stderr, "@ must be followed by a word name\n"); return;
            }
            if (dict_count >= DICT_MAX) { fprintf(stderr, "dictionary full\n"); return; }
            Word *w = &dict[dict_count];
            strncpy(w->name, toks[i].text, 255);
            i++; w->len = 0;
            while (i < n) {
                if (toks[i].type == T_SYM && toks[i].text[0] == ';') { i++; break; }
                if (w->len >= WORD_BODY_MAX) { fprintf(stderr, "word body too long\n"); return; }
                w->body[w->len++] = toks[i++];
            }
            check_body_no_directives(w->body, w->len, w->name);
            dict_count++; continue;
        }
        int start = i;
        while (i < n && !(toks[i].type == T_SYM && toks[i].text[0] == '@'))
            i++;
        Word tmp = {.len = i - start};
        memcpy(tmp.body, &toks[start], tmp.len * sizeof(Token));
        exec_body(tmp.body, tmp.len);
    }
}

/* ============================================================
   File execution & REPL
   ============================================================ */

static void run_file_with_dir(const char *path) {
    char *src = read_file(path);
    if (!src) return;

    char saved_dir[512];
    strncpy(saved_dir, current_dir, sizeof(saved_dir));
    dir_of(path, current_dir, sizeof(current_dir));

    Token toks[TOK_MAX];
    int n = tokenize_all(src, toks, TOK_MAX);
    free(src);
    exec_line(toks, n);

    strncpy(current_dir, saved_dir, sizeof(current_dir));
}

static void run_file(const char *path) {
    char resolved[512];
    if (path[0] == '/') {
        strncpy(resolved, path, sizeof(resolved));
    } else {
        char cwd[512];
        if (getcwd(cwd, sizeof(cwd)))
            snprintf(resolved, sizeof(resolved), "%s/%s", cwd, path);
        else
            strncpy(resolved, path, sizeof(resolved));
    }
    run_file_with_dir(resolved);
}

static void repl(void) {
    Token toks[TOK_MAX];
    int line_num = 1;
    char *line;
    while ((line = readline("zona> ")) != NULL) {
        if (*line) add_history(line);
        int n = tokenize(line, toks, TOK_MAX, line_num++);
        exec_line(toks, n);
        free(line);
    }
    printf("\n");
}

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
