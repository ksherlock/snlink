
	; check if asm-time expression evaluation matches link time expression evaluation.
	; what I learned:
	; comparisions (=, <>, etc) return 0/0xffffffff
	; division/mod use signed math
	; mod returns a positive value.

	; sn-link -Da=1 -Db=2 -Dc=3

	; case sensitive.
	opt c+
	section one
A	equ 1
B	equ 2
C	equ 3

start
	DW A+B*C
	DW (A+B)*C
	DW A+(B*C)
	DW A-B/C
	DW (A-B)/C
	DW A-(B/C)
	DW A<<B+C
	DW (A<<B)+C
	DW A<<(B+C)
	DW A<<B
	DW C>>A
	DW A=B
	DW A<>B
	DW A>B
	DW A>=B
	DW A<B
	DW A<=B
	DW A&B
	DW A^B
	DW A|B

; check if signed/unsigned
	DL (-A)/C
	DL (-A)%C
	DL (-B)%C
	DL (-C)%B
	DL A%C
	DL B%C
	DL C%B
	DL (-A)>>16
stop


; align to 16-byte boundary
	ds 16-((stop-start)&$0f)
	ds 16



	section two
	xref a,b,c

	dw a+b*c
	dw (a+b)*c
	dw a+(b*c)
	dw a-b/c
	dw (a-b)/c
	dw a-(b/c)
	dw a<<b+c
	dw (a<<b)+c
	dw a<<(b+c)
	dw a<<b
	dw c>>a
	dw a=b
	dw a<>b
	dw a>b
	dw a>=b
	dw a<b
	dw a<=b
	dw a&b
	dw a^b
	dw a|b

; check if signed/unsigned
	dl (-a)/c
	dl (-a)%c
	dl (-b)%c
	dl (-c)%b
	dl a%c
	dl b%c
	dl c%b
	dl (-a)>>16


