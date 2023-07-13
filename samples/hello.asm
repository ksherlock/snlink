;
; hello world
;

_WriteCString macro
	ldx #$200c
	jsl $e10000
	endm

	section code
	mx %00

	pea ^hello
	pea hello
	_WriteCString
	lda #0
	rtl

	section data
hello
	db 'Hello, World!',$0d,$00
