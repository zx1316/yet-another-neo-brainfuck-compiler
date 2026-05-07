public start

.code
start:
	sub	rsp, 120    ; 我觉得 120 的栈空间绝对够了，32 的影子，8 的 ExitProcess，剩下 80 个字节的临时变量，我真不认为一个基本块内能溢出 80 个
	mov	rax, qword ptr gs:[96]
	mov	rax, qword ptr [rax + 24]
	add	rax, 48
	ALIGN	16
start_LBB5_1:
	mov	rax, qword ptr [rax]
	mov	rcx, qword ptr [rax + 64]
	cmp	word ptr [rcx + 22], 0
	je	start_LBB5_1
	cmp	word ptr [rcx + 24], 0
	jne	start_LBB5_1
	mov	rsi, qword ptr [rax + 16]
	mov	rax, 4711732171926431047
	mov	qword ptr [rsp + 56], rax
	mov	rax, 126939460428900
	mov	qword ptr [rsp + 64], rax
	movsxd	rax, dword ptr [rsi + 60]
	mov	r8d, dword ptr [rsi + rax + 136]
	mov	eax, dword ptr [rsi + r8 + 24]
	test	rax, rax
	je	start_LBB5_3
	mov	ecx, dword ptr [rsi + r8 + 32]
	add	rcx, rsi
	mov	edx, dword ptr [rsi + r8 + 28]
	add	rdx, rsi
	mov	r8d, dword ptr [rsi + r8 + 36]
	add	r8, rsi
	xor	r15d, r15d
	xor	r9d, r9d
start_LBB5_5:
	mov	r11d, dword ptr [rcx + 4*r9]
	lea	r10, [rsi + r11]
	movzx	ebx, byte ptr [rsi + r11]
	mov	dil, 71
	mov	r11d, 1
	ALIGN	16
start_LBB5_6:
	cmp	bl, dil
	jne	start_LBB5_7
	movzx	ebx, byte ptr [r10 + r11]
	movzx	edi, byte ptr [rsp + r11 + 56]
	inc	r11
	mov	ebp, edi
	or	bpl, bl
	jne	start_LBB5_6
	jmp	start_LBB5_9
	ALIGN	16
start_LBB5_7:
	inc	r9
	cmp	r9, rax
	jne	start_LBB5_5
	jmp	start_LBB5_10
start_LBB5_9:
	movzx	eax, word ptr [r8 + 2*r9]
	mov	r15d, dword ptr [rdx + 4*rax]
	add	r15, rsi
	jmp	start_LBB5_10
start_LBB5_3:
	xor	r15d, r15d
start_LBB5_10:
	mov	rax, 8242266044863967052
	mov	qword ptr [rsp + 56], rax
	mov	qword ptr [rsp + 64], 1098478177
	lea	rbx, [rsp + 56]
	mov	rcx, rsi
	mov	rdx, rbx
	call	r15
	mov	rdi, rax
	mov	rax, 7165071222045767749
	mov	qword ptr [rsp + 56], rax
	mov	dword ptr [rsp + 64], 7566181
	mov	rcx, rsi
	mov	rdx, rbx
	call	r15
	mov	qword ptr [rsp + 32], rax
	mov	rax, 7310293695640789877
	mov	qword ptr [rsp + 56], rax
	mov	qword ptr [rsp + 64], 1819042862
	mov	rcx, rbx
	call	rdi
	mov	rbx, rax
	test	rax, rax
	jne	start_LBB5_12
	mov	rax, 7218835287365284717
	mov	qword ptr [rsp + 56], rax
	mov	dword ptr [rsp + 64], 27756
	lea	rcx, [rsp + 56]
	call	rdi
	mov	rbx, rax
start_LBB5_12:
    ; 获取 putchar 部分
	mov	rax, 32195248318084464
	mov	qword ptr [rsp + 56], rax
	lea	r14, [rsp + 56]
	mov	rcx, rbx
	mov	rdx, r14
	call	r15
	mov	rsi, rax
	; 获取 getchar 部分（可省略）
	mov	rax, 32195248318080359
	mov	qword ptr [rsp + 56], rax
	mov	rcx, rbx
	mov	rdx, r14
	call	r15
	mov	rdi, rax
	; [rsp + 32] -> ExitProcess
	; rsi -> putchar
	; rdi -> getchar
	; [rsp + 40] -> temps

    ; 分配内存部分（可省略）
	mov	rax, 109330211561827
	mov	qword ptr [rsp + 56], rax
	mov	rcx, rbx
	mov	rdx, r14
	call	r15
	; rax -> calloc
	mov	ecx, 1
	mov	edx, 30000  ; 编译时更改该数值
	call	rax
	; mov	rbx, rax    ; 可替换为 lea
	; rbx -> tape ptr

	; 结束程序部分
	mov	rax, qword ptr [rsp + 32]
	xor	ecx, ecx
	call	rax

end
