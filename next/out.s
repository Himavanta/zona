.data
.balign 8
_fmt_int:
	.ascii "%ld\n"
	.byte 0
/* end data */

.data
.balign 8
_fmt_flt:
	.ascii "%g\n"
	.byte 0
/* end data */

.data
.balign 8
_fmt_str:
	.ascii "%.*s"
	.byte 0
/* end data */

.bss
.balign 8
_stack:
	.fill 2048,1,0
/* end data */

.data
.balign 8
_sp:
	.int 0
/* end data */

.data
.balign 8
_prog_argc:
	.int 0
/* end data */

.data
.balign 8
_prog_argv:
	.quad 0
/* end data */

.text
.balign 4
_zona_pop_l:
	stp	x29, x30, [sp, -16]!
	mov	x29, sp
	adrp	x0, _sp@page
	add	x0, x0, _sp@pageoff
	ldr	w0, [x0]
	mov	w1, #1
	sub	w0, w0, w1
	adrp	x1, _sp@page
	add	x1, x1, _sp@pageoff
	str	w0, [x1]
	sxtw	x0, w0
	mov	x1, #8
	mul	x1, x0, x1
	adrp	x0, _stack@page
	add	x0, x0, _stack@pageoff
	add	x0, x0, x1
	ldr	d0, [x0]
	fcvtzs	x0, d0
	ldp	x29, x30, [sp], 16
	ret
/* end function zona_pop_l */

.text
.balign 4
_zona_pop_d:
	stp	x29, x30, [sp, -16]!
	mov	x29, sp
	adrp	x0, _sp@page
	add	x0, x0, _sp@pageoff
	ldr	w0, [x0]
	mov	w1, #1
	sub	w0, w0, w1
	adrp	x1, _sp@page
	add	x1, x1, _sp@pageoff
	str	w0, [x1]
	sxtw	x0, w0
	mov	x1, #8
	mul	x1, x0, x1
	adrp	x0, _stack@page
	add	x0, x0, _stack@pageoff
	add	x0, x0, x1
	ldr	d0, [x0]
	ldp	x29, x30, [sp], 16
	ret
/* end function zona_pop_d */

.text
.balign 4
.globl _main
_main:
	stp	x29, x30, [sp, -16]!
	mov	x29, sp
	adrp	x2, _prog_argc@page
	add	x2, x2, _prog_argc@pageoff
	str	w0, [x2]
	adrp	x0, _prog_argv@page
	add	x0, x0, _prog_argv@pageoff
	str	x1, [x0]
	mov	x0, #16
	sub	sp, sp, x0
	mov	x0, #0
	add	x1, sp, x0
	mov	x0, #42
	str	x0, [x1]
	adrp	x0, _fmt_int@page
	add	x0, x0, _fmt_int@pageoff
	bl	_printf
	mov	x0, #16
	add	sp, sp, x0
	adrp	x0, _sp@page
	add	x0, x0, _sp@pageoff
	ldr	w0, [x0]
	sxtw	x1, w0
	mov	x2, #8
	mul	x2, x1, x2
	adrp	x1, _stack@page
	add	x1, x1, _stack@pageoff
	add	x1, x1, x2
	adrp	x2, "Lfp0"@page
	add	x2, x2, "Lfp0"@pageoff
	ldr	d0, [x2]
	str	d0, [x1]
	mov	w1, #1
	add	w0, w0, w1
	adrp	x1, _sp@page
	add	x1, x1, _sp@pageoff
	str	w0, [x1]
	mov	w1, #1
	sub	w0, w0, w1
	adrp	x1, _sp@page
	add	x1, x1, _sp@pageoff
	str	w0, [x1]
	sxtw	x0, w0
	mov	x1, #8
	mul	x1, x0, x1
	adrp	x0, _stack@page
	add	x0, x0, _stack@pageoff
	add	x0, x0, x1
	ldr	d0, [x0]
	fcvtzs	x0, d0
	mov	x1, #16
	sub	sp, sp, x1
	mov	x1, #0
	add	x1, sp, x1
	str	x0, [x1]
	adrp	x0, _fmt_int@page
	add	x0, x0, _fmt_int@pageoff
	bl	_printf
	mov	x0, #16
	add	sp, sp, x0
	adrp	x0, _sp@page
	add	x0, x0, _sp@pageoff
	ldr	w0, [x0]
	mov	w1, #1
	sub	w0, w0, w1
	adrp	x1, _sp@page
	add	x1, x1, _sp@pageoff
	str	w0, [x1]
	sxtw	x0, w0
	mov	x1, #8
	mul	x1, x0, x1
	adrp	x0, _stack@page
	add	x0, x0, _stack@pageoff
	add	x0, x0, x1
	ldr	d0, [x0]
	mov	x0, #16
	sub	sp, sp, x0
	mov	x0, #0
	add	x0, sp, x0
	str	d0, [x0]
	adrp	x0, _fmt_flt@page
	add	x0, x0, _fmt_flt@pageoff
	bl	_printf
	mov	x0, #16
	add	sp, sp, x0
	mov	w0, #0
	ldp	x29, x30, [sp], 16
	ret
/* end function main */

/* floating point constants */
.section __TEXT,__literal8,8byte_literals
.p2align 3
Lfp0:
	.int 0
	.int 1079558144 /* 99.000000 */

