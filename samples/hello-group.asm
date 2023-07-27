;
; hello
;

_WriteCString macro
	ldx #$200c
	jsl $e10000
	endm

code	group
data	group

	section .code,code
	mx %00

	pea ^hello
	pea hello
	_WriteCString
	lda #0
	rtl

	section .data,data

hello
	db 'Hello, World!',$0d,$00
