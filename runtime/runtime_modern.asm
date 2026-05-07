public start

includelib	kernel32.lib
includelib	ucrt.lib

extrn	__imp_ExitProcess:proc
extrn	__imp_putchar:proc
extrn	__imp_getchar:proc

.data?
tape	db	30000 dup(?)

.code
start:
	; 我觉得 120 的栈空间绝对够了，32 的影子，8 的 ExitProcess，剩下 80 个字节的临时变量，我真不认为一个基本块内能溢出 80 个
	sub	rsp, 120
	mov	rax, qword ptr __imp_ExitProcess
	mov	rsi, qword ptr __imp_putchar
	mov	rdi, qword ptr __imp_getchar
	mov	qword ptr [rsp + 32], rax
	lea	rbx, tape
	int	3
end
