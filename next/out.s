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

.data
.balign 8
_str0:
	.ascii "hello"
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
_zona_cond:
	stp	x29, x30, [sp, -16]!
	mov	x29, sp
	mov	x0, #0
	ldp	x29, x30, [sp], 16
	ret
/* end function zona_cond */

.text
.balign 4
_zona_sum:
	stp	x29, x30, [sp, -32]!
	mov	x29, sp
	str	x19, [x29, 24]
	cmp	x0, #0
	cset	w1, eq
	sxtw	x1, w1
	cmp	w1, #0
	bne	L10
	mov	x19, x0
	mov	x0, #1
	sub	x0, x19, x0
	bl	_zona_sum
	mov	x1, x0
	mov	x0, x19
	add	x0, x0, x1
L10:
	ldr	x19, [x29, 24]
	ldp	x29, x30, [sp], 32
	ret
/* end function zona_sum */

.text
.balign 4
_zona_fact:
	stp	x29, x30, [sp, -32]!
	mov	x29, sp
	str	x19, [x29, 24]
	mov	x19, x0
	mov	x0, x1
	cmp	x0, #1
	cset	w1, eq
	sxtw	x1, w1
	cmp	w1, #0
	bne	L14
	mov	x1, #1
	sub	x1, x0, x1
	bl	_zona_fact
	mov	x1, x0
	mov	x0, x19
	mul	x0, x0, x1
L14:
	ldr	x19, [x29, 24]
	ldp	x29, x30, [sp], 32
	ret
/* end function zona_fact */

.text
.balign 4
_zona_greet:
	stp	x29, x30, [sp, -16]!
	mov	x29, sp
	mov	x0, #16
	sub	sp, sp, x0
	mov	x0, #8
	add	x1, sp, x0
	adrp	x0, _str0@page
	add	x0, x0, _str0@pageoff
	str	x0, [x1]
	mov	x0, #0
	add	x1, sp, x0
	mov	x0, #5
	str	x0, [x1]
	adrp	x0, _fmt_str@page
	add	x0, x0, _fmt_str@pageoff
	bl	_printf
	mov	x0, #16
	add	sp, sp, x0
	ldp	x29, x30, [sp], 16
	ret
/* end function zona_greet */

