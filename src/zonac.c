#include "zona.h"

/* ============================================================
   QBE IR emitter for Zona
   ============================================================ */

static FILE *out;
static int tmp_id = 0;
static int lbl_id = 0;
static int str_id = 0;

/* virtual stack: track QBE temp names at compile time */
#define VSTACK_MAX 256
static int vstack[VSTACK_MAX];
static int vsp = 0;

static int newtmp(void) { return tmp_id++; }
static int newlbl(void) { return lbl_id++; }

static void vpush(int t) {
    if (vsp >= VSTACK_MAX) { fprintf(stderr, "compile: vstack overflow\n"); exit(1); }
    vstack[vsp++] = t;
}
static int vpop(void) {
    if (vsp <= 0) { fprintf(stderr, "compile: vstack underflow\n"); exit(1); }
    return vstack[--vsp];
}
static int vpeek(void) {
    if (vsp <= 0) { fprintf(stderr, "compile: vstack underflow\n"); exit(1); }
    return vstack[vsp - 1];
}

/* ============================================================
   Dictionary (compile time)
   ============================================================ */

static Word dict[DICT_MAX];
static int dict_count = 0;

static Word *find_word(const char *name) {
    for (int i = dict_count - 1; i >= 0; i--)
        if (strcmp(dict[i].name, name) == 0) return &dict[i];
    return NULL;
}

/* ============================================================
   String literals collection (emitted as QBE data)
   ============================================================ */

#define STR_MAX 256
static struct { int id; char text[256]; } strs[STR_MAX];
static int str_count = 0;

static int add_str(const char *text) {
    int id = str_id++;
    strncpy(strs[str_count].text, text, 255);
    strs[str_count].id = id;
    str_count++;
    return id;
}

/* ============================================================
   Extern table (for :bind FFI)
   ============================================================ */

#define EXTERN_MAX 256
static struct {
    char zona_name[256];
    char c_name[256];
    char ret_type;
    char param_types[64];
} externs[EXTERN_MAX];
static int extern_count = 0;

static int find_extern(const char *name) {
    for (int i = extern_count - 1; i >= 0; i--)
        if (strcmp(externs[i].zona_name, name) == 0) return i;
    return -1;
}

/* ============================================================
   Emit helpers
   ============================================================ */

static void emit_data_section(void) {
    fprintf(out, "data $fmt_int = { b \"%%ld\\n\", b 0 }\n");
    fprintf(out, "data $fmt_flt = { b \"%%g\\n\", b 0 }\n");
    fprintf(out, "data $fmt_int_nonl = { b \"%%ld \", b 0 }\n");
    fprintf(out, "data $fmt_flt_nonl = { b \"%%g \", b 0 }\n");
    fprintf(out, "data $fmt_stack = { b \"<%%d>\", b 0 }\n");
    fprintf(out, "data $fmt_spc = { b \" %%ld\", b 0 }\n");
    fprintf(out, "data $fmt_spcf = { b \" %%g\", b 0 }\n");
    for (int i = 0; i < str_count; i++) {
        fprintf(out, "data $str%d = { b \"", strs[i].id);
        for (const char *p = strs[i].text; *p; p++) {
            if (*p == '\n') fprintf(out, "\\n");
            else if (*p == '\t') fprintf(out, "\\t");
            else if (*p == '\\') fprintf(out, "\\\\");
            else if (*p == '"') fprintf(out, "\\\"");
            else fputc(*p, out);
        }
        fprintf(out, "\", b 0 }\n");
    }
    fprintf(out, "\n");
}

/* runtime: data stack in memory */
static void emit_runtime(void) {
    fprintf(out, "# --- runtime stack ---\n");
    fprintf(out, "data $stack = { z 2048 }\n");  /* 256 doubles */
    fprintf(out, "data $sp = { w 0 }\n");
    fprintf(out, "# --- memory ---\n");
    fprintf(out, "data $mem = { z 524288 }\n");  /* 65536 doubles */
    fprintf(out, "data $here = { w 0 }\n");
    fprintf(out, "# --- heap (for :alloc/:free) ---\n");
    fprintf(out, "data $heap_ptrs = { z 8192 }\n");   /* 1024 pointers */
    fprintf(out, "data $heap_addrs = { z 4096 }\n");   /* 1024 ints (zona addr) */
    fprintf(out, "data $heap_sizes = { z 4096 }\n");   /* 1024 ints (size) */
    fprintf(out, "data $heap_count = { w 0 }\n");
    fprintf(out, "data $heap_next = { w 65536 }\n");   /* start after static mem */
    fprintf(out, "# --- file handles ---\n");
    fprintf(out, "data $fhandles = { z 128 }\n");      /* 16 FILE* pointers */
    fprintf(out, "data $fhandle_count = { w 0 }\n");
    fprintf(out, "# --- argc/argv ---\n");
    fprintf(out, "data $prog_argc = { w 0 }\n");
    fprintf(out, "data $prog_argv = { l 0 }\n\n");

    /* push: store double to stack[sp++] */
    fprintf(out, "function $zona_push(d %%v) {\n@start\n");
    fprintf(out, "    %%si =w loadw $sp\n");
    fprintf(out, "    %%off =l extsw %%si\n");
    fprintf(out, "    %%off2 =l mul %%off, 8\n");
    fprintf(out, "    %%addr =l add $stack, %%off2\n");
    fprintf(out, "    stored %%v, %%addr\n");
    fprintf(out, "    %%si2 =w add %%si, 1\n");
    fprintf(out, "    storew %%si2, $sp\n");
    fprintf(out, "    ret\n}\n\n");

    /* pop: return stack[--sp] */
    fprintf(out, "function d $zona_pop() {\n@start\n");
    fprintf(out, "    %%si =w loadw $sp\n");
    fprintf(out, "    %%si2 =w sub %%si, 1\n");
    fprintf(out, "    storew %%si2, $sp\n");
    fprintf(out, "    %%off =l extsw %%si2\n");
    fprintf(out, "    %%off2 =l mul %%off, 8\n");
    fprintf(out, "    %%addr =l add $stack, %%off2\n");
    fprintf(out, "    %%v =d loadd %%addr\n");
    fprintf(out, "    ret %%v\n}\n\n");

    /* peek: return stack[sp-1] */
    fprintf(out, "function d $zona_peek() {\n@start\n");
    fprintf(out, "    %%si =w loadw $sp\n");
    fprintf(out, "    %%si2 =w sub %%si, 1\n");
    fprintf(out, "    %%off =l extsw %%si2\n");
    fprintf(out, "    %%off2 =l mul %%off, 8\n");
    fprintf(out, "    %%addr =l add $stack, %%off2\n");
    fprintf(out, "    %%v =d loadd %%addr\n");
    fprintf(out, "    ret %%v\n}\n\n");

    /* print: pop and print (integer if whole, else float) */
    fprintf(out, "function $zona_print() {\n@start\n");
    fprintf(out, "    %%v =d call $zona_pop()\n");
    fprintf(out, "    %%li =l dtosi %%v\n");
    fprintf(out, "    %%back =d sltof %%li\n");
    fprintf(out, "    %%eq =w ceqd %%v, %%back\n");
    fprintf(out, "    jnz %%eq, @isint, @isflt\n");
    fprintf(out, "@isint\n");
    fprintf(out, "    call $printf(l $fmt_int, ..., l %%li)\n");
    fprintf(out, "    ret\n");
    fprintf(out, "@isflt\n");
    fprintf(out, "    call $printf(l $fmt_flt, ..., d %%v)\n");
    fprintf(out, "    ret\n}\n\n");

    /* mem_store: val addr -- (store val at mem[addr], supports heap) */
    fprintf(out, "function $zona_mem_store() {\n@start\n");
    fprintf(out, "    %%addr =d call $zona_pop()\n");
    fprintf(out, "    %%val =d call $zona_pop()\n");
    fprintf(out, "    %%ai =w dtosi %%addr\n");
    fprintf(out, "    %%ptr =l call $zona_mem_at(w %%ai)\n");
    fprintf(out, "    stored %%val, %%ptr\n");
    fprintf(out, "    ret\n}\n\n");

    /* mem_load: addr -- val (supports heap) */
    fprintf(out, "function $zona_mem_load() {\n@start\n");
    fprintf(out, "    %%addr =d call $zona_pop()\n");
    fprintf(out, "    %%ai =w dtosi %%addr\n");
    fprintf(out, "    %%ptr =l call $zona_mem_at(w %%ai)\n");
    fprintf(out, "    %%v =d loadd %%ptr\n");
    fprintf(out, "    call $zona_push(d %%v)\n");
    fprintf(out, "    ret\n}\n\n");

    /* here: push current here value */
    fprintf(out, "function $zona_here() {\n@start\n");
    fprintf(out, "    %%h =w loadw $here\n");
    fprintf(out, "    %%hd =d swtof %%h\n");
    fprintf(out, "    call $zona_push(d %%hd)\n");
    fprintf(out, "    ret\n}\n\n");

    /* allot: pop n, advance here by n */
    fprintf(out, "function $zona_allot() {\n@start\n");
    fprintf(out, "    %%n =d call $zona_pop()\n");
    fprintf(out, "    %%ni =w dtosi %%n\n");
    fprintf(out, "    %%h =w loadw $here\n");
    fprintf(out, "    %%h2 =w add %%h, %%ni\n");
    fprintf(out, "    storew %%h2, $here\n");
    fprintf(out, "    ret\n}\n\n");

    /* type: pop addr len, print chars */
    fprintf(out, "function $zona_type() {\n@start\n");
    fprintf(out, "    %%len =d call $zona_pop()\n");
    fprintf(out, "    %%addr =d call $zona_pop()\n");
    fprintf(out, "    %%li =w dtosi %%len\n");
    fprintf(out, "    %%ai =w dtosi %%addr\n");
    fprintf(out, "    %%i =w copy 0\n");
    fprintf(out, "    jmp @tcond\n");
    fprintf(out, "@tcond\n");
    fprintf(out, "    %%done =w csltw %%i, %%li\n");
    fprintf(out, "    jnz %%done, @tbody, @tend\n");
    fprintf(out, "@tbody\n");
    fprintf(out, "    %%idx =w add %%ai, %%i\n");
    fprintf(out, "    %%off =l extsw %%idx\n");
    fprintf(out, "    %%off2 =l mul %%off, 8\n");
    fprintf(out, "    %%ptr =l add $mem, %%off2\n");
    fprintf(out, "    %%cv =d loadd %%ptr\n");
    fprintf(out, "    %%ci =w dtosi %%cv\n");
    fprintf(out, "    call $putchar(w %%ci)\n");
    fprintf(out, "    %%i =w add %%i, 1\n");
    fprintf(out, "    jmp @tcond\n");
    fprintf(out, "@tend\n");
    fprintf(out, "    ret\n}\n\n");

    /* emit: pop and putchar */
    fprintf(out, "function $zona_emit() {\n@start\n");
    fprintf(out, "    %%v =d call $zona_pop()\n");
    fprintf(out, "    %%ci =w dtosi %%v\n");
    fprintf(out, "    call $putchar(w %%ci)\n");
    fprintf(out, "    ret\n}\n\n");

    /* store_str: store string bytes into mem at here, push addr len */
    fprintf(out, "function $zona_store_str(l %%src, w %%len) {\n@start\n");
    fprintf(out, "    %%h =w loadw $here\n");
    fprintf(out, "    %%i =w copy 0\n");
    fprintf(out, "    jmp @scond\n");
    fprintf(out, "@scond\n");
    fprintf(out, "    %%ok =w csltw %%i, %%len\n");
    fprintf(out, "    jnz %%ok, @sbody, @sdone\n");
    fprintf(out, "@sbody\n");
    fprintf(out, "    %%si =l extsw %%i\n");
    fprintf(out, "    %%sptr =l add %%src, %%si\n");
    fprintf(out, "    %%ch =w loadub %%sptr\n");
    fprintf(out, "    %%idx =w add %%h, %%i\n");
    fprintf(out, "    %%off =l extsw %%idx\n");
    fprintf(out, "    %%off2 =l mul %%off, 8\n");
    fprintf(out, "    %%mptr =l add $mem, %%off2\n");
    fprintf(out, "    %%chd =d swtof %%ch\n");
    fprintf(out, "    stored %%chd, %%mptr\n");
    fprintf(out, "    %%i =w add %%i, 1\n");
    fprintf(out, "    jmp @scond\n");
    fprintf(out, "@sdone\n");
    fprintf(out, "    %%h2 =w add %%h, %%len\n");
    fprintf(out, "    storew %%h2, $here\n");
    fprintf(out, "    %%hd =d swtof %%h\n");
    fprintf(out, "    call $zona_push(d %%hd)\n");
    fprintf(out, "    %%ld =d swtof %%len\n");
    fprintf(out, "    call $zona_push(d %%ld)\n");
    fprintf(out, "    ret\n}\n\n");

    /* mem_at: resolve zona addr to real pointer (static or heap) */
    fprintf(out, "function l $zona_mem_at(w %%addr) {\n@start\n");
    fprintf(out, "    %%lim =w copy 65536\n");
    fprintf(out, "    %%is_static =w csltw %%addr, %%lim\n");
    fprintf(out, "    jnz %%is_static, @static, @heap\n");
    fprintf(out, "@static\n");
    fprintf(out, "    %%off =l extsw %%addr\n");
    fprintf(out, "    %%off2 =l mul %%off, 8\n");
    fprintf(out, "    %%ptr =l add $mem, %%off2\n");
    fprintf(out, "    ret %%ptr\n");
    fprintf(out, "@heap\n");
    fprintf(out, "    %%hc =w loadw $heap_count\n");
    fprintf(out, "    %%i =w copy 0\n");
    fprintf(out, "    jmp @hcond\n");
    fprintf(out, "@hcond\n");
    fprintf(out, "    %%ok =w csltw %%i, %%hc\n");
    fprintf(out, "    jnz %%ok, @hbody, @hfail\n");
    fprintf(out, "@hbody\n");
    fprintf(out, "    %%ai =l extsw %%i\n");
    fprintf(out, "    %%aoff =l mul %%ai, 4\n");
    fprintf(out, "    %%aptr =l add $heap_addrs, %%aoff\n");
    fprintf(out, "    %%ha =w loadw %%aptr\n");
    fprintf(out, "    %%sptr =l add $heap_sizes, %%aoff\n");
    fprintf(out, "    %%hs =w loadw %%sptr\n");
    fprintf(out, "    %%end =w add %%ha, %%hs\n");
    fprintf(out, "    %%ge =w csgew %%addr, %%ha\n");
    fprintf(out, "    %%lt =w csltw %%addr, %%end\n");
    fprintf(out, "    %%match =w and %%ge, %%lt\n");
    fprintf(out, "    jnz %%match, @hfound, @hnext\n");
    fprintf(out, "@hfound\n");
    fprintf(out, "    %%poff =l mul %%ai, 8\n");
    fprintf(out, "    %%ppptr =l add $heap_ptrs, %%poff\n");
    fprintf(out, "    %%base =l loadl %%ppptr\n");
    fprintf(out, "    %%rel =w sub %%addr, %%ha\n");
    fprintf(out, "    %%roff =l extsw %%rel\n");
    fprintf(out, "    %%roff2 =l mul %%roff, 8\n");
    fprintf(out, "    %%rptr =l add %%base, %%roff2\n");
    fprintf(out, "    ret %%rptr\n");
    fprintf(out, "@hnext\n");
    fprintf(out, "    %%i =w add %%i, 1\n");
    fprintf(out, "    jmp @hcond\n");
    fprintf(out, "@hfail\n");
    fprintf(out, "    ret 0\n");
    fprintf(out, "}\n\n");

    /* alloc: pop n, calloc, push zona addr */
    fprintf(out, "function $zona_alloc() {\n@start\n");
    fprintf(out, "    %%n =d call $zona_pop()\n");
    fprintf(out, "    %%ni =w dtosi %%n\n");
    fprintf(out, "    %%nl =l extsw %%ni\n");
    fprintf(out, "    %%p =l call $calloc(l %%nl, l 8)\n");
    fprintf(out, "    %%hc =w loadw $heap_count\n");
    fprintf(out, "    %%hi =l extsw %%hc\n");
    fprintf(out, "    %%poff =l mul %%hi, 8\n");
    fprintf(out, "    %%ppptr =l add $heap_ptrs, %%poff\n");
    fprintf(out, "    storel %%p, %%ppptr\n");
    fprintf(out, "    %%aoff =l mul %%hi, 4\n");
    fprintf(out, "    %%aptr =l add $heap_addrs, %%aoff\n");
    fprintf(out, "    %%hn =w loadw $heap_next\n");
    fprintf(out, "    storew %%hn, %%aptr\n");
    fprintf(out, "    %%sptr =l add $heap_sizes, %%aoff\n");
    fprintf(out, "    storew %%ni, %%sptr\n");
    fprintf(out, "    %%hd =d swtof %%hn\n");
    fprintf(out, "    call $zona_push(d %%hd)\n");
    fprintf(out, "    %%hn2 =w add %%hn, %%ni\n");
    fprintf(out, "    storew %%hn2, $heap_next\n");
    fprintf(out, "    %%hc2 =w add %%hc, 1\n");
    fprintf(out, "    storew %%hc2, $heap_count\n");
    fprintf(out, "    ret\n}\n\n");

    /* free: pop addr, find and free */
    fprintf(out, "function $zona_free() {\n@start\n");
    fprintf(out, "    %%addr =d call $zona_pop()\n");
    fprintf(out, "    %%ai =w dtosi %%addr\n");
    fprintf(out, "    %%hc =w loadw $heap_count\n");
    fprintf(out, "    %%i =w copy 0\n");
    fprintf(out, "    jmp @fcond\n");
    fprintf(out, "@fcond\n");
    fprintf(out, "    %%ok =w csltw %%i, %%hc\n");
    fprintf(out, "    jnz %%ok, @fbody, @fdone\n");
    fprintf(out, "@fbody\n");
    fprintf(out, "    %%il =l extsw %%i\n");
    fprintf(out, "    %%aoff =l mul %%il, 4\n");
    fprintf(out, "    %%aptr =l add $heap_addrs, %%aoff\n");
    fprintf(out, "    %%ha =w loadw %%aptr\n");
    fprintf(out, "    %%eq =w ceqw %%ai, %%ha\n");
    fprintf(out, "    jnz %%eq, @ffound, @fnext\n");
    fprintf(out, "@ffound\n");
    fprintf(out, "    %%poff =l mul %%il, 8\n");
    fprintf(out, "    %%ppptr =l add $heap_ptrs, %%poff\n");
    fprintf(out, "    %%p =l loadl %%ppptr\n");
    fprintf(out, "    call $free(l %%p)\n");
    /* move last entry into this slot */
    fprintf(out, "    %%hc2 =w sub %%hc, 1\n");
    fprintf(out, "    storew %%hc2, $heap_count\n");
    fprintf(out, "    %%ll =l extsw %%hc2\n");
    fprintf(out, "    %%lpoff =l mul %%ll, 8\n");
    fprintf(out, "    %%lpp =l add $heap_ptrs, %%lpoff\n");
    fprintf(out, "    %%lp =l loadl %%lpp\n");
    fprintf(out, "    storel %%lp, %%ppptr\n");
    fprintf(out, "    %%laoff =l mul %%ll, 4\n");
    fprintf(out, "    %%lap =l add $heap_addrs, %%laoff\n");
    fprintf(out, "    %%la =w loadw %%lap\n");
    fprintf(out, "    storew %%la, %%aptr\n");
    fprintf(out, "    %%lsp =l add $heap_sizes, %%laoff\n");
    fprintf(out, "    %%ls =w loadw %%lsp\n");
    fprintf(out, "    %%sptr =l add $heap_sizes, %%aoff\n");
    fprintf(out, "    storew %%ls, %%sptr\n");
    fprintf(out, "    ret\n");
    fprintf(out, "@fnext\n");
    fprintf(out, "    %%i =w add %%i, 1\n");
    fprintf(out, "    jmp @fcond\n");
    fprintf(out, "@fdone\n");
    fprintf(out, "    ret\n}\n\n");

    /* update mem_store and mem_load to use mem_at for heap support */
    /* (already emitted above as simple versions, we'll replace them) */

    /* fopen: pop path_addr path_len mode_addr mode_len, push handle */
    fprintf(out, "function $zona_fopen() {\n@start\n");
    fprintf(out, "    %%ml =d call $zona_pop()\n");
    fprintf(out, "    %%ma =d call $zona_pop()\n");
    fprintf(out, "    %%pl =d call $zona_pop()\n");
    fprintf(out, "    %%pa =d call $zona_pop()\n");
    fprintf(out, "    %%pai =w dtosi %%pa\n");
    fprintf(out, "    %%pli =w dtosi %%pl\n");
    fprintf(out, "    %%mai =w dtosi %%ma\n");
    fprintf(out, "    %%mli =w dtosi %%ml\n");
    /* build path string on stack */
    fprintf(out, "    %%pbl =l extsw %%pli\n");
    fprintf(out, "    %%pbl2 =l add %%pbl, 1\n");
    fprintf(out, "    %%pbuf =l call $calloc(l %%pbl2, l 1)\n");
    fprintf(out, "    %%pi =w copy 0\n");
    fprintf(out, "    jmp @fpcond\n");
    fprintf(out, "@fpcond\n");
    fprintf(out, "    %%pok =w csltw %%pi, %%pli\n");
    fprintf(out, "    jnz %%pok, @fpbody, @fpdone\n");
    fprintf(out, "@fpbody\n");
    fprintf(out, "    %%pidx =w add %%pai, %%pi\n");
    fprintf(out, "    %%pptr =l call $zona_mem_at(w %%pidx)\n");
    fprintf(out, "    %%pv =d loadd %%pptr\n");
    fprintf(out, "    %%pc =w dtosi %%pv\n");
    fprintf(out, "    %%pil =l extsw %%pi\n");
    fprintf(out, "    %%pdst =l add %%pbuf, %%pil\n");
    fprintf(out, "    storeb %%pc, %%pdst\n");
    fprintf(out, "    %%pi =w add %%pi, 1\n");
    fprintf(out, "    jmp @fpcond\n");
    fprintf(out, "@fpdone\n");
    /* build mode string */
    fprintf(out, "    %%mbl =l extsw %%mli\n");
    fprintf(out, "    %%mbl2 =l add %%mbl, 1\n");
    fprintf(out, "    %%mbuf =l call $calloc(l %%mbl2, l 1)\n");
    fprintf(out, "    %%mi =w copy 0\n");
    fprintf(out, "    jmp @fmcond\n");
    fprintf(out, "@fmcond\n");
    fprintf(out, "    %%mok =w csltw %%mi, %%mli\n");
    fprintf(out, "    jnz %%mok, @fmbody, @fmdone\n");
    fprintf(out, "@fmbody\n");
    fprintf(out, "    %%midx =w add %%mai, %%mi\n");
    fprintf(out, "    %%mptr =l call $zona_mem_at(w %%midx)\n");
    fprintf(out, "    %%mv =d loadd %%mptr\n");
    fprintf(out, "    %%mc =w dtosi %%mv\n");
    fprintf(out, "    %%mil =l extsw %%mi\n");
    fprintf(out, "    %%mdst =l add %%mbuf, %%mil\n");
    fprintf(out, "    storeb %%mc, %%mdst\n");
    fprintf(out, "    %%mi =w add %%mi, 1\n");
    fprintf(out, "    jmp @fmcond\n");
    fprintf(out, "@fmdone\n");
    fprintf(out, "    %%fp =l call $fopen(l %%pbuf, l %%mbuf)\n");
    fprintf(out, "    call $free(l %%pbuf)\n");
    fprintf(out, "    call $free(l %%mbuf)\n");
    fprintf(out, "    %%hc =w loadw $fhandle_count\n");
    fprintf(out, "    %%hl =l extsw %%hc\n");
    fprintf(out, "    %%hoff =l mul %%hl, 8\n");
    fprintf(out, "    %%hptr =l add $fhandles, %%hoff\n");
    fprintf(out, "    storel %%fp, %%hptr\n");
    fprintf(out, "    %%hd =d swtof %%hc\n");
    fprintf(out, "    call $zona_push(d %%hd)\n");
    fprintf(out, "    %%hc2 =w add %%hc, 1\n");
    fprintf(out, "    storew %%hc2, $fhandle_count\n");
    fprintf(out, "    ret\n}\n\n");

    /* fclose: pop handle, close */
    fprintf(out, "function $zona_fclose() {\n@start\n");
    fprintf(out, "    %%h =d call $zona_pop()\n");
    fprintf(out, "    %%hi =w dtosi %%h\n");
    fprintf(out, "    %%hl =l extsw %%hi\n");
    fprintf(out, "    %%hoff =l mul %%hl, 8\n");
    fprintf(out, "    %%hptr =l add $fhandles, %%hoff\n");
    fprintf(out, "    %%fp =l loadl %%hptr\n");
    fprintf(out, "    call $fclose(l %%fp)\n");
    fprintf(out, "    storel 0, %%hptr\n");
    fprintf(out, "    ret\n}\n\n");

    /* fread: pop handle, push char (or -1 on EOF) */
    fprintf(out, "function $zona_fread() {\n@start\n");
    fprintf(out, "    %%h =d call $zona_pop()\n");
    fprintf(out, "    %%hi =w dtosi %%h\n");
    fprintf(out, "    %%hl =l extsw %%hi\n");
    fprintf(out, "    %%hoff =l mul %%hl, 8\n");
    fprintf(out, "    %%hptr =l add $fhandles, %%hoff\n");
    fprintf(out, "    %%fp =l loadl %%hptr\n");
    fprintf(out, "    %%c =w call $fgetc(l %%fp)\n");
    fprintf(out, "    %%cd =d swtof %%c\n");
    fprintf(out, "    call $zona_push(d %%cd)\n");
    fprintf(out, "    ret\n}\n\n");

    /* fwrite: pop c handle, write */
    fprintf(out, "function $zona_fwrite() {\n@start\n");
    fprintf(out, "    %%h =d call $zona_pop()\n");
    fprintf(out, "    %%c =d call $zona_pop()\n");
    fprintf(out, "    %%hi =w dtosi %%h\n");
    fprintf(out, "    %%ci =w dtosi %%c\n");
    fprintf(out, "    %%hl =l extsw %%hi\n");
    fprintf(out, "    %%hoff =l mul %%hl, 8\n");
    fprintf(out, "    %%hptr =l add $fhandles, %%hoff\n");
    fprintf(out, "    %%fp =l loadl %%hptr\n");
    fprintf(out, "    call $fputc(w %%ci, l %%fp)\n");
    fprintf(out, "    ret\n}\n\n");

    /* argc: push prog_argc */
    fprintf(out, "function $zona_argc() {\n@start\n");
    fprintf(out, "    %%n =w loadw $prog_argc\n");
    fprintf(out, "    %%nd =d swtof %%n\n");
    fprintf(out, "    call $zona_push(d %%nd)\n");
    fprintf(out, "    ret\n}\n\n");

    /* argv: pop index, store string into mem, push addr len */
    fprintf(out, "function $zona_argv() {\n@start\n");
    fprintf(out, "    %%idx =d call $zona_pop()\n");
    fprintf(out, "    %%ii =w dtosi %%idx\n");
    fprintf(out, "    %%il =l extsw %%ii\n");
    fprintf(out, "    %%aoff =l mul %%il, 8\n");
    fprintf(out, "    %%av =l loadl $prog_argv\n");
    fprintf(out, "    %%aptr =l add %%av, %%aoff\n");
    fprintf(out, "    %%str =l loadl %%aptr\n");
    fprintf(out, "    %%len =w call $strlen(l %%str)\n");
    fprintf(out, "    call $zona_store_str(l %%str, w %%len)\n");
    fprintf(out, "    ret\n}\n\n");

    /* peek/poke: read/write real C memory addresses */
    fprintf(out, "function $zona_peek8() {\n@start\n");
    fprintf(out, "    %%a =d call $zona_pop()\n");
    fprintf(out, "    %%p =l dtosi %%a\n");
    fprintf(out, "    %%v =w loadub %%p\n");
    fprintf(out, "    %%vd =d swtof %%v\n");
    fprintf(out, "    call $zona_push(d %%vd)\n");
    fprintf(out, "    ret\n}\n\n");

    fprintf(out, "function $zona_peek32() {\n@start\n");
    fprintf(out, "    %%a =d call $zona_pop()\n");
    fprintf(out, "    %%p =l dtosi %%a\n");
    fprintf(out, "    %%v =w loadw %%p\n");
    fprintf(out, "    %%vd =d swtof %%v\n");
    fprintf(out, "    call $zona_push(d %%vd)\n");
    fprintf(out, "    ret\n}\n\n");

    fprintf(out, "function $zona_peek64() {\n@start\n");
    fprintf(out, "    %%a =d call $zona_pop()\n");
    fprintf(out, "    %%p =l dtosi %%a\n");
    fprintf(out, "    %%v =l loadl %%p\n");
    fprintf(out, "    %%vd =d sltof %%v\n");
    fprintf(out, "    call $zona_push(d %%vd)\n");
    fprintf(out, "    ret\n}\n\n");

    fprintf(out, "function $zona_peekd() {\n@start\n");
    fprintf(out, "    %%a =d call $zona_pop()\n");
    fprintf(out, "    %%p =l dtosi %%a\n");
    fprintf(out, "    %%v =d loadd %%p\n");
    fprintf(out, "    call $zona_push(d %%v)\n");
    fprintf(out, "    ret\n}\n\n");

    fprintf(out, "function $zona_poke8() {\n@start\n");
    fprintf(out, "    %%a =d call $zona_pop()\n");
    fprintf(out, "    %%val =d call $zona_pop()\n");
    fprintf(out, "    %%p =l dtosi %%a\n");
    fprintf(out, "    %%vi =w dtosi %%val\n");
    fprintf(out, "    storeb %%vi, %%p\n");
    fprintf(out, "    ret\n}\n\n");

    fprintf(out, "function $zona_poke32() {\n@start\n");
    fprintf(out, "    %%a =d call $zona_pop()\n");
    fprintf(out, "    %%val =d call $zona_pop()\n");
    fprintf(out, "    %%p =l dtosi %%a\n");
    fprintf(out, "    %%vi =w dtosi %%val\n");
    fprintf(out, "    storew %%vi, %%p\n");
    fprintf(out, "    ret\n}\n\n");

    fprintf(out, "function $zona_poke64() {\n@start\n");
    fprintf(out, "    %%a =d call $zona_pop()\n");
    fprintf(out, "    %%val =d call $zona_pop()\n");
    fprintf(out, "    %%p =l dtosi %%a\n");
    fprintf(out, "    %%vi =l dtosi %%val\n");
    fprintf(out, "    storel %%vi, %%p\n");
    fprintf(out, "    ret\n}\n\n");

    fprintf(out, "function $zona_poked() {\n@start\n");
    fprintf(out, "    %%a =d call $zona_pop()\n");
    fprintf(out, "    %%val =d call $zona_pop()\n");
    fprintf(out, "    %%p =l dtosi %%a\n");
    fprintf(out, "    stored %%val, %%p\n");
    fprintf(out, "    ret\n}\n\n");

    /* cstr_to_zona: convert C char* to zona string (copy into mem, push addr len) */
    fprintf(out, "function $zona_cstr_to_zona(l %%ptr) {\n@start\n");
    fprintf(out, "    %%len =w call $strlen(l %%ptr)\n");
    fprintf(out, "    call $zona_store_str(l %%ptr, w %%len)\n");
    fprintf(out, "    ret\n}\n\n");

    /* to_cstr: pop addr+len from zona stack, return C string pointer (caller must free) */
    fprintf(out, "function l $zona_to_cstr() {\n@start\n");
    fprintf(out, "    %%len =d call $zona_pop()\n");
    fprintf(out, "    %%addr =d call $zona_pop()\n");
    fprintf(out, "    %%li =w dtosi %%len\n");
    fprintf(out, "    %%ai =w dtosi %%addr\n");
    fprintf(out, "    %%bl =l extsw %%li\n");
    fprintf(out, "    %%bl2 =l add %%bl, 1\n");
    fprintf(out, "    %%buf =l call $calloc(l %%bl2, l 1)\n");
    fprintf(out, "    %%i =w copy 0\n");
    fprintf(out, "    jmp @ccond\n");
    fprintf(out, "@ccond\n");
    fprintf(out, "    %%ok =w csltw %%i, %%li\n");
    fprintf(out, "    jnz %%ok, @cbody, @cdone\n");
    fprintf(out, "@cbody\n");
    fprintf(out, "    %%idx =w add %%ai, %%i\n");
    fprintf(out, "    %%ptr =l call $zona_mem_at(w %%idx)\n");
    fprintf(out, "    %%cv =d loadd %%ptr\n");
    fprintf(out, "    %%ch =w dtosi %%cv\n");
    fprintf(out, "    %%il =l extsw %%i\n");
    fprintf(out, "    %%dst =l add %%buf, %%il\n");
    fprintf(out, "    storeb %%ch, %%dst\n");
    fprintf(out, "    %%i =w add %%i, 1\n");
    fprintf(out, "    jmp @ccond\n");
    fprintf(out, "@cdone\n");
    fprintf(out, "    ret %%buf\n}\n\n");

    /* stack debug: print <sp> val val ... */
    fprintf(out, "function $zona_stack() {\n@start\n");
    fprintf(out, "    %%si =w loadw $sp\n");
    fprintf(out, "    call $printf(l $fmt_stack, ..., w %%si)\n");
    fprintf(out, "    %%i =w copy 0\n");
    fprintf(out, "    jmp @dcond\n");
    fprintf(out, "@dcond\n");
    fprintf(out, "    %%ok =w csltw %%i, %%si\n");
    fprintf(out, "    jnz %%ok, @dbody, @ddone\n");
    fprintf(out, "@dbody\n");
    fprintf(out, "    %%off =l extsw %%i\n");
    fprintf(out, "    %%off2 =l mul %%off, 8\n");
    fprintf(out, "    %%addr =l add $stack, %%off2\n");
    fprintf(out, "    %%v =d loadd %%addr\n");
    fprintf(out, "    %%li =l dtosi %%v\n");
    fprintf(out, "    %%back =d sltof %%li\n");
    fprintf(out, "    %%eq =w ceqd %%v, %%back\n");
    fprintf(out, "    jnz %%eq, @dint, @dflt\n");
    fprintf(out, "@dint\n");
    fprintf(out, "    call $printf(l $fmt_spc, ..., l %%li)\n");
    fprintf(out, "    jmp @dnext\n");
    fprintf(out, "@dflt\n");
    fprintf(out, "    call $printf(l $fmt_spcf, ..., d %%v)\n");
    fprintf(out, "    jmp @dnext\n");
    fprintf(out, "@dnext\n");
    fprintf(out, "    %%i =w add %%i, 1\n");
    fprintf(out, "    jmp @dcond\n");
    fprintf(out, "@ddone\n");
    fprintf(out, "    call $putchar(w 10)\n");
    fprintf(out, "    ret\n}\n\n");
}

/* ============================================================
   Compile a token body into QBE IR (inside a function)
   Uses runtime stack (call $zona_push / $zona_pop)
   ============================================================ */

static void compile_body(Token *toks, int n, int in_word);

static void compile_token(Token *toks, int n, int *ip, int in_word) {
    Token *t = &toks[*ip];
    int a, b, r;

    switch (t->type) {
    case T_NUM:
        fprintf(out, "    call $zona_push(d d_%g)\n", t->num);
        break;

    case T_STR: {
        int sid = add_str(t->text);
        int len = (int)strlen(t->text);
        fprintf(out, "    call $zona_store_str(l $str%d, w %d)\n", sid, len);
        break;
    }

    case T_SYM:
        switch (t->text[0]) {
        case '+': case '-': case '*': case '/': case '%': {
            const char *op;
            switch (t->text[0]) {
                case '+': op = "add"; break;
                case '-': op = "sub"; break;
                case '*': op = "mul"; break;
                case '/': op = "div"; break;
                case '%': default: op = "rem"; break;
            }
            b = newtmp(); a = newtmp(); r = newtmp();
            fprintf(out, "    %%t%d =d call $zona_pop()\n", b);
            fprintf(out, "    %%t%d =d call $zona_pop()\n", a);
            if (t->text[0] == '%') {
                /* fmod: a - floor(a/b)*b */
                int d1 = newtmp(), f1 = newtmp(), f2 = newtmp(), m1 = newtmp();
                fprintf(out, "    %%t%d =d div %%t%d, %%t%d\n", d1, a, b);
                fprintf(out, "    %%t%d =l dtosi %%t%d\n", f1, d1);
                fprintf(out, "    %%t%d =d sltof %%t%d\n", f2, f1);
                fprintf(out, "    %%t%d =d mul %%t%d, %%t%d\n", m1, f2, b);
                fprintf(out, "    %%t%d =d sub %%t%d, %%t%d\n", r, a, m1);
            } else {
                fprintf(out, "    %%t%d =d %s %%t%d, %%t%d\n", r, op, a, b);
            }
            fprintf(out, "    call $zona_push(d %%t%d)\n", r);
            break;
        }
        case '^': {
            b = newtmp(); a = newtmp(); r = newtmp();
            fprintf(out, "    %%t%d =d call $zona_pop()\n", b);
            fprintf(out, "    %%t%d =d call $zona_pop()\n", a);
            fprintf(out, "    %%t%d =d call $pow(d %%t%d, d %%t%d)\n", r, a, b);
            fprintf(out, "    call $zona_push(d %%t%d)\n", r);
            break;
        }
        case '>': case '<': case '=': {
            const char *cmp;
            switch (t->text[0]) {
                case '>': cmp = "cgtd"; break;
                case '<': cmp = "cltd"; break;
                case '=': default: cmp = "ceqd"; break;
            }
            b = newtmp(); a = newtmp(); r = newtmp(); int rd = newtmp();
            fprintf(out, "    %%t%d =d call $zona_pop()\n", b);
            fprintf(out, "    %%t%d =d call $zona_pop()\n", a);
            fprintf(out, "    %%t%d =w %s %%t%d, %%t%d\n", r, cmp, a, b);
            fprintf(out, "    %%t%d =d swtof %%t%d\n", rd, r);
            fprintf(out, "    call $zona_push(d %%t%d)\n", rd);
            break;
        }
        case '.':
            fprintf(out, "    call $zona_print()\n");
            break;
        case '&':
            fprintf(out, "    call $zona_mem_load()\n");
            break;
        case '#':
            fprintf(out, "    call $zona_mem_store()\n");
            break;
        case '?': {
            /* conditional: ? X or ? X ! Y */
            (*ip)++;
            if (*ip >= n) return;
            int has_else = (*ip + 2 <= n &&
                            toks[*ip + 1].type == T_SYM &&
                            toks[*ip + 1].text[0] == '!');
            int cond = newtmp(), cw = newtmp();
            int lt = newlbl(), lf = newlbl(), le = newlbl();
            fprintf(out, "    %%t%d =d call $zona_pop()\n", cond);
            fprintf(out, "    %%t%d =w dtosi %%t%d\n", cw, cond);
            fprintf(out, "    jnz %%t%d, @L%d, @L%d\n", cw, lt, lf);
            fprintf(out, "@L%d\n", lt);
            compile_token(toks, n, ip, in_word);
            fprintf(out, "    jmp @L%d\n", le);
            fprintf(out, "@L%d\n", lf);
            if (has_else) {
                *ip += 1; /* skip ! */
                (*ip)++;  /* move to else token */
                if (*ip < n)
                    compile_token(toks, n, ip, in_word);
            }
            fprintf(out, "    jmp @L%d\n", le);
            fprintf(out, "@L%d\n", le);
            return; /* don't increment ip again */
        }
        case '$': {
            int dead = newlbl();
            fprintf(out, "    ret\n");
            fprintf(out, "@Ldead%d\n", dead);
            break;
        }
        case '~': {
            int dead = newlbl();
            fprintf(out, "    jmp @Lstart\n");
            fprintf(out, "@Ldead%d\n", dead);
            break;
        }
        default: break;
        }
        break;

    case T_PRIM:
        if (strcmp(t->text, ":dup") == 0) {
            r = newtmp();
            fprintf(out, "    %%t%d =d call $zona_peek()\n", r);
            fprintf(out, "    call $zona_push(d %%t%d)\n", r);
        } else if (strcmp(t->text, ":drop") == 0) {
            newtmp();
            fprintf(out, "    %%t%d =d call $zona_pop()\n", tmp_id - 1);
        } else if (strcmp(t->text, ":swap") == 0) {
            a = newtmp(); b = newtmp();
            fprintf(out, "    %%t%d =d call $zona_pop()\n", a);
            fprintf(out, "    %%t%d =d call $zona_pop()\n", b);
            fprintf(out, "    call $zona_push(d %%t%d)\n", a);
            fprintf(out, "    call $zona_push(d %%t%d)\n", b);
        } else if (strcmp(t->text, ":over") == 0) {
            a = newtmp(); b = newtmp();
            fprintf(out, "    %%t%d =d call $zona_pop()\n", a);
            fprintf(out, "    %%t%d =d call $zona_pop()\n", b);
            fprintf(out, "    call $zona_push(d %%t%d)\n", b);
            fprintf(out, "    call $zona_push(d %%t%d)\n", a);
            fprintf(out, "    call $zona_push(d %%t%d)\n", b);
        } else if (strcmp(t->text, ":rot") == 0) {
            int c = newtmp(); b = newtmp(); a = newtmp();
            fprintf(out, "    %%t%d =d call $zona_pop()\n", c);
            fprintf(out, "    %%t%d =d call $zona_pop()\n", b);
            fprintf(out, "    %%t%d =d call $zona_pop()\n", a);
            fprintf(out, "    call $zona_push(d %%t%d)\n", b);
            fprintf(out, "    call $zona_push(d %%t%d)\n", c);
            fprintf(out, "    call $zona_push(d %%t%d)\n", a);
        } else if (strcmp(t->text, ":here") == 0) {
            fprintf(out, "    call $zona_here()\n");
        } else if (strcmp(t->text, ":allot") == 0) {
            fprintf(out, "    call $zona_allot()\n");
        } else if (strcmp(t->text, ":alloc") == 0) {
            fprintf(out, "    call $zona_alloc()\n");
        } else if (strcmp(t->text, ":free") == 0) {
            fprintf(out, "    call $zona_free()\n");
        } else if (strcmp(t->text, ":type") == 0) {
            fprintf(out, "    call $zona_type()\n");
        } else if (strcmp(t->text, ":emit") == 0) {
            fprintf(out, "    call $zona_emit()\n");
        } else if (strcmp(t->text, ":key") == 0) {
            r = newtmp();
            fprintf(out, "    %%t%d =w call $getchar()\n", r);
            int rd = newtmp();
            fprintf(out, "    %%t%d =d swtof %%t%d\n", rd, r);
            fprintf(out, "    call $zona_push(d %%t%d)\n", rd);
        } else if (strcmp(t->text, ":stack") == 0) {
            fprintf(out, "    call $zona_stack()\n");
        } else if (strcmp(t->text, ":peek8") == 0) {
            fprintf(out, "    call $zona_peek8()\n");
        } else if (strcmp(t->text, ":peek32") == 0) {
            fprintf(out, "    call $zona_peek32()\n");
        } else if (strcmp(t->text, ":peek64") == 0) {
            fprintf(out, "    call $zona_peek64()\n");
        } else if (strcmp(t->text, ":peekd") == 0) {
            fprintf(out, "    call $zona_peekd()\n");
        } else if (strcmp(t->text, ":poke8") == 0) {
            fprintf(out, "    call $zona_poke8()\n");
        } else if (strcmp(t->text, ":poke32") == 0) {
            fprintf(out, "    call $zona_poke32()\n");
        } else if (strcmp(t->text, ":poke64") == 0) {
            fprintf(out, "    call $zona_poke64()\n");
        } else if (strcmp(t->text, ":poked") == 0) {
            fprintf(out, "    call $zona_poked()\n");
        } else if (strcmp(t->text, ":fopen") == 0) {
            fprintf(out, "    call $zona_fopen()\n");
        } else if (strcmp(t->text, ":fclose") == 0) {
            fprintf(out, "    call $zona_fclose()\n");
        } else if (strcmp(t->text, ":fread") == 0) {
            fprintf(out, "    call $zona_fread()\n");
        } else if (strcmp(t->text, ":fwrite") == 0) {
            fprintf(out, "    call $zona_fwrite()\n");
        } else if (strcmp(t->text, ":argc") == 0) {
            fprintf(out, "    call $zona_argc()\n");
        } else if (strcmp(t->text, ":argv") == 0) {
            fprintf(out, "    call $zona_argv()\n");
        } else if (strcmp(t->text, ":exit") == 0) {
            r = newtmp(); int ri = newtmp();
            fprintf(out, "    %%t%d =d call $zona_pop()\n", r);
            fprintf(out, "    %%t%d =w dtosi %%t%d\n", ri, r);
            fprintf(out, "    call $exit(w %%t%d)\n", ri);
        } else if (strcmp(t->text, ":time") == 0) {
            r = newtmp(); int rd = newtmp();
            fprintf(out, "    %%t%d =l call $time(l 0)\n", r);
            fprintf(out, "    %%t%d =d sltof %%t%d\n", rd, r);
            fprintf(out, "    call $zona_push(d %%t%d)\n", rd);
        } else if (strcmp(t->text, ":rand") == 0) {
            int m = newtmp(), mi = newtmp(), rv = newtmp(), rm = newtmp(), rd = newtmp();
            fprintf(out, "    %%t%d =d call $zona_pop()\n", m);
            fprintf(out, "    %%t%d =w dtosi %%t%d\n", mi, m);
            fprintf(out, "    %%t%d =w call $rand()\n", rv);
            fprintf(out, "    %%t%d =w rem %%t%d, %%t%d\n", rm, rv, mi);
            fprintf(out, "    %%t%d =d swtof %%t%d\n", rd, rm);
            fprintf(out, "    call $zona_push(d %%t%d)\n", rd);
        } else {
            fprintf(stderr, "compile: unknown primitive: %s\n", t->text);
        }
        break;

    case T_WORD: {
        Word *w = find_word(t->text);
        if (w) {
            fprintf(out, "    call $zona_%s()\n", t->text);
        } else {
            int ei = find_extern(t->text);
            if (ei >= 0) {
                /* FFI call */
                const char *params = externs[ei].param_types;
                int plen = (int)strlen(params);
                /* allocate temps for each param (reverse order pop) */
                int ptmps[64], stmps[64];
                for (int j = plen - 1; j >= 0; j--) {
                    if (params[j] == 's') {
                        /* string: pop addr+len, convert to C string */
                        stmps[j] = newtmp();
                        fprintf(out, "    %%t%d =l call $zona_to_cstr()\n", stmps[j]);
                        ptmps[j] = stmps[j];
                    } else {
                        ptmps[j] = newtmp();
                        fprintf(out, "    %%t%d =d call $zona_pop()\n", ptmps[j]);
                        stmps[j] = -1;
                    }
                }
                /* convert types and build call */
                int ctmps[64]; /* converted temps */
                for (int j = 0; j < plen; j++) {
                    switch (params[j]) {
                    case 'i':
                        ctmps[j] = newtmp();
                        fprintf(out, "    %%t%d =w dtosi %%t%d\n", ctmps[j], ptmps[j]);
                        break;
                    case 'l': case 'p':
                        ctmps[j] = newtmp();
                        { int ti = newtmp();
                        fprintf(out, "    %%t%d =l dtosi %%t%d\n", ti, ptmps[j]);
                        ctmps[j] = ti; }
                        break;
                    case 'f':
                        ctmps[j] = newtmp();
                        fprintf(out, "    %%t%d =s truncd %%t%d\n", ctmps[j], ptmps[j]);
                        break;
                    case 'd':
                        ctmps[j] = ptmps[j]; /* already double */
                        break;
                    case 's':
                        ctmps[j] = ptmps[j]; /* already l from zona_to_cstr */
                        break;
                    }
                }
                /* emit call */
                char rt = externs[ei].ret_type;
                int rettmp = -1;
                if (rt != 'v') {
                    rettmp = newtmp();
                    const char *qrt = (rt == 'd') ? "d" : (rt == 'f') ? "s" : (rt == 'l' || rt == 'p' || rt == 'S') ? "l" : "w";
                    fprintf(out, "    %%t%d =%s call $%s(", rettmp, qrt, externs[ei].c_name);
                } else {
                    fprintf(out, "    call $%s(", externs[ei].c_name);
                }
                for (int j = 0; j < plen; j++) {
                    if (j > 0) fprintf(out, ", ");
                    const char *qt;
                    switch (params[j]) {
                    case 'i': qt = "w"; break;
                    case 'l': case 'p': case 's': qt = "l"; break;
                    case 'f': qt = "s"; break;
                    case 'd': qt = "d"; break;
                    default: qt = "w"; break;
                    }
                    fprintf(out, "%s %%t%d", qt, ctmps[j]);
                }
                fprintf(out, ")\n");
                /* free string temps */
                for (int j = 0; j < plen; j++)
                    if (stmps[j] >= 0)
                        fprintf(out, "    call $free(l %%t%d)\n", stmps[j]);
                /* push return value */
                if (rt != 'v') {
                    int rd = newtmp();
                    if (rt == 'S') {
                        /* C char* → zona string: copy into mem, push addr+len */
                        fprintf(out, "    call $zona_cstr_to_zona(l %%t%d)\n", rettmp);
                    } else if (rt == 'd') {
                        fprintf(out, "    call $zona_push(d %%t%d)\n", rettmp);
                    } else if (rt == 'f') {
                        fprintf(out, "    %%t%d =d exts %%t%d\n", rd, rettmp);
                        fprintf(out, "    call $zona_push(d %%t%d)\n", rd);
                    } else {
                        /* i, l, p -> double */
                        const char *cvt = (rt == 'i') ? "swtof" : "sltof";
                        fprintf(out, "    %%t%d =d %s %%t%d\n", rd, cvt, rettmp);
                        fprintf(out, "    call $zona_push(d %%t%d)\n", rd);
                    }
                }
            } else {
                fprintf(stderr, "compile: unknown word: %s\n", t->text);
            }
        }
        break;
    }
    }
}

static void compile_body(Token *toks, int n, int in_word) {
    int ip = 0;
    while (ip < n) {
        compile_token(toks, n, &ip, in_word);
        ip++;
    }
}

/* ============================================================
   Top-level compilation: parse word defs, :use, and loose code
   ============================================================ */

/* forward decl */
static void compile_file(const char *path);

#define USE_MAX 64
static char used_files[USE_MAX][512];
static int used_count = 0;
static char current_dir[512] = ".";

static int already_used(const char *path) {
    for (int i = 0; i < used_count; i++)
        if (strcmp(used_files[i], path) == 0) return 1;
    return 0;
}

static void resolve_path(const char *base, const char *rel, char *o, int sz) {
    if (rel[0] == '/') { snprintf(o, sz, "%s", rel); return; }
    snprintf(o, sz, "%s/%s", base, rel);
    char *p;
    while ((p = strstr(o, "/./")) != NULL) memmove(p, p + 2, strlen(p + 2) + 1);
    while ((p = strstr(o, "/../")) != NULL) {
        char *prev = p - 1;
        while (prev > o && *prev != '/') prev--;
        if (prev >= o) memmove(prev, p + 3, strlen(p + 3) + 1);
        else break;
    }
}

static void dir_of(const char *path, char *o, int sz) {
    strncpy(o, path, sz - 1); o[sz - 1] = '\0';
    char *last = strrchr(o, '/');
    if (last) *last = '\0'; else strcpy(o, ".");
}

/* first pass: collect all string literals and word definitions */
static int main_seg_count = 0;

typedef struct { Token toks[TOK_MAX]; int n; } Segment;
static Segment main_segs[256];

static void collect_toplevel(Token *toks, int n) {
    int i = 0;
    while (i < n) {
        if (toks[i].type == T_PRIM && strcmp(toks[i].text, ":use") == 0) {
            if (!validate_use(toks, n, i)) return;
            i++;
            char resolved[512];
            resolve_path(current_dir, toks[i].text, resolved, sizeof(resolved));
            if (!already_used(resolved)) {
                if (used_count >= USE_MAX) { fprintf(stderr, "too many :use\n"); return; }
                strncpy(used_files[used_count++], resolved, 511);
                compile_file(resolved);
            }
            i++; continue;
        }
        if (toks[i].type == T_PRIM && strcmp(toks[i].text, ":bind") == 0) {
            if (!validate_bind(toks, n, i)) return;
            if (extern_count >= EXTERN_MAX) { fprintf(stderr, "too many :bind\n"); return; }
            int ei = extern_count++;
            strncpy(externs[ei].zona_name, toks[i+1].text, 255);
            strncpy(externs[ei].c_name, toks[i+2].text, 255);
            externs[ei].ret_type = toks[i+3].text[0];
            int cnt = line_token_count(toks, n, i);
            if (cnt >= 5)
                strncpy(externs[ei].param_types, toks[i+4].text, 63);
            else
                externs[ei].param_types[0] = '\0';
            i += cnt; continue;
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
            dict_count++;
            continue;
        }
        /* loose code: collect until next @ or :bind/:use */
        int start = i;
        while (i < n && !(toks[i].type == T_SYM && toks[i].text[0] == '@')
                      && !(toks[i].type == T_PRIM && strcmp(toks[i].text, ":use") == 0)
                      && !(toks[i].type == T_PRIM && strcmp(toks[i].text, ":bind") == 0))
            i++;
        if (i > start && main_seg_count < 256) {
            Segment *s = &main_segs[main_seg_count++];
            s->n = i - start;
            memcpy(s->toks, &toks[start], s->n * sizeof(Token));
        }
    }
}

static void compile_file(const char *path) {
    char *src = read_file(path);
    if (!src) return;
    char saved[512];
    strncpy(saved, current_dir, sizeof(saved));
    dir_of(path, current_dir, sizeof(current_dir));
    Token toks[TOK_MAX];
    int n = tokenize_all(src, toks, TOK_MAX);
    free(src);
    collect_toplevel(toks, n);
    strncpy(current_dir, saved, sizeof(current_dir));
}

/* second pass: emit QBE IR for all collected words and main segments */
static void emit_words(void) {
    for (int i = 0; i < dict_count; i++) {
        Word *w = &dict[i];
        fprintf(out, "function $zona_%s() {\n@Lentry\n    jmp @Lstart\n@Lstart\n", w->name);
        compile_body(w->body, w->len, 1);
        fprintf(out, "    ret\n}\n\n");
    }
}

static void emit_main(void) {
    fprintf(out, "export function w $main(w %%argc, l %%argv) {\n@Lentry\n");
    /* store argc-1 and argv+8 (skip program name) */
    fprintf(out, "    %%ac =w sub %%argc, 1\n");
    fprintf(out, "    storew %%ac, $prog_argc\n");
    fprintf(out, "    %%av =l add %%argv, 8\n");
    fprintf(out, "    storel %%av, $prog_argv\n");
    fprintf(out, "    jmp @Lstart\n@Lstart\n");
    for (int i = 0; i < main_seg_count; i++)
        compile_body(main_segs[i].toks, main_segs[i].n, 0);
    fprintf(out, "    ret 0\n}\n");
}

/* ============================================================
   Main: zonac input.zona [-o output]
   ============================================================ */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: zonac input.zona [-o output]\n");
        return 1;
    }
    const char *input = argv[1];
    const char *output = "a.out";
    for (int i = 2; i < argc - 1; i++)
        if (strcmp(argv[i], "-o") == 0) output = argv[i + 1];

    /* resolve input path */
    char resolved[512];
    if (input[0] == '/') {
        strncpy(resolved, input, sizeof(resolved));
    } else {
        char cwd[512];
        if (getcwd(cwd, sizeof(cwd)))
            snprintf(resolved, sizeof(resolved), "%s/%s", cwd, input);
        else
            strncpy(resolved, input, sizeof(resolved));
    }

    /* pass 1: collect everything */
    compile_file(resolved);

    /* pass 2: emit QBE IR */
    char ssa_path[512], s_path[512];
    snprintf(ssa_path, sizeof(ssa_path), "%s.ssa", output);
    snprintf(s_path, sizeof(s_path), "%s.s", output);

    out = fopen(ssa_path, "w");
    if (!out) { fprintf(stderr, "cannot create %s\n", ssa_path); return 1; }

    emit_runtime();
    emit_words();
    emit_main();
    emit_data_section();
    fclose(out);

    /* pass 3: qbe -> asm -> executable */
    char cmd[2048];

    /* qbe from PATH */
    snprintf(cmd, sizeof(cmd), "qbe %s > %s", ssa_path, s_path);
    if (system(cmd) != 0) { fprintf(stderr, "qbe failed\n"); return 1; }

    snprintf(cmd, sizeof(cmd), "cc %s -o %s -lm", s_path, output);
    if (system(cmd) != 0) { fprintf(stderr, "cc failed\n"); return 1; }

    /* cleanup intermediate QBE IR, keep .s for FFI linking */
    remove(ssa_path);

    return 0;
}
