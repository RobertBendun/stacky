#define Impl_Math(Op_Kind, Name, Implementation) \
	case Intrinsic_Kind::Op_Kind: \
		asm_file << "	;;" Name "\n"; \
		asm_file << "	pop rbx\n"; \
		asm_file << "	pop rax\n"; \
		asm_file << (Implementation); \
		asm_file << "	push rax\n"; \
		break

#define Impl_Compare(Op, Name, Suffix) \
	case Intrinsic_Kind::Op: \
		asm_file << "	;; " Name "\n"; \
		asm_file << "	xor rax, rax\n"; \
		asm_file << "	pop rbx\n"; \
		asm_file << "	pop rcx\n"; \
		asm_file << "	cmp rcx, rbx\n"; \
		asm_file << "	set" Suffix " al\n"; \
		asm_file << "	push rax\n"; \
		break

#define Impl_Div(Op, Name, End) \
	case Intrinsic_Kind::Op: \
		asm_file << "	;; " Name "\n"; \
		asm_file << "	xor rdx, rdx\n"; \
		asm_file << "	pop rbx\n"; \
		asm_file << "	pop rax\n"; \
		asm_file << "	div rbx\n" End; \
		break

namespace linux::x86_64 {

	auto asm_header(std::ostream &asm_file, Words &words, std::unordered_map<std::string, unsigned> const& strings)
	{
		asm_file << "BITS 64\n";

		auto const label = [&](auto &v) -> auto& { return asm_file << '\t' << Symbol_Prefix << v.id << ": "; };

		asm_file << "segment .bss\n";
		asm_file << "	_stacky_callstack: resq 1024\n";
		asm_file << "	_stacky_callptr:   resq 1\n";
		for (auto const& [key, value] : words) {
			switch (value.kind) {
			case Word::Kind::Array: label(value) << "resb " << value.byte_size << '\n'; break;
			default:
				;
			}
		}

		asm_file << "segment .rodata\n";
		for (auto const& [key, value] : strings) {
			asm_file << String_Prefix << value << ": db ";
			for (auto c : key) {
				asm_file << int(c) << ',';
			}
			asm_file << "0\n";
		}

		asm_file << "segment .text\n";
	}

	auto emit_return(std::ostream& asm_file)
	{
		asm_file << "	sub qword [_stacky_callptr], 1\n";
		asm_file << "	mov rbx, [_stacky_callptr]\n";
		asm_file << "	mov rax, [_stacky_callstack+rbx*8]\n";
		asm_file << "	push rax\n";
		asm_file << "	ret\n";
	}

	auto emit_intrinsic(Operation const& op, std::ostream& asm_file)
	{
		static char const* const Register_B_By_Size[] = { "bl", "bx", "ebx", "rbx" };

		assert(op.kind == Operation::Kind::Intrinsic);
		switch (op.intrinsic) {
		case Intrinsic_Kind::Random32:
			asm_file << "	;; random32\n";
			asm_file << "	xor rax, rax\n";
			asm_file << "	rdrand eax\n";
			asm_file << "	push rax\n";
			break;

		case Intrinsic_Kind::Random64:
			asm_file << "	;; random64\n";
			asm_file << "	rdrand rax\n";
			asm_file << "	push rax\n";
			break;

		case Intrinsic_Kind::Call:
			asm_file << "	;; stack call\n";
			asm_file << "	pop rax\n";
			asm_file << "	call rax\n";
			break;

		Impl_Math(Add,          "add",          "add rax, rbx\n");
		Impl_Math(Bitwise_And,  "bitwise and",  "and rax, rbx\n");
		Impl_Math(Bitwise_Or,   "bitwise or",   "or rax, rbx\n");
		Impl_Math(Bitwise_Xor,  "bitwise xor",  "xor rax, rbx\n");
		Impl_Math(Left_Shift,   "left shift",   "mov rcx, rbx\nsal rax, cl\n");
		Impl_Math(Mul,          "multiply",     "imul rax, rbx\n");
		Impl_Math(Right_Shift,  "right shift",  "mov rcx, rbx\nsar rax, cl\n");
		Impl_Math(Subtract,     "subtract",     "sub rax, rbx\n");
		Impl_Math(Min,          "min",          "cmp rax, rbx\ncmova rax, rbx\n");
		Impl_Math(Max,          "max",          "cmp rax, rbx\ncmovb rax, rbx\n");
		Impl_Math(Boolean_Or,  "or",
				"xor rcx, rcx\n"
				"or rax, rbx\n"
				"setne cl\n"
				"mov rax, rcx\n");
		Impl_Math(Boolean_And, "and",
				"xor rcx, rcx\n"
				"and rax, rbx\n"
				"setne cl\n"
				"mov rax, rcx\n");

		Impl_Div(Div,      "div",     "push rax\n");
		Impl_Div(Div_Mod,  "divmod",  "push rdx\npush rax\n");
		Impl_Div(Mod,      "mod",     "push rdx\n");

		case Intrinsic_Kind::Top:
			asm_file << "	;; top\n";
			asm_file << "	push rsp\n";
			break;

		case Intrinsic_Kind::Drop:
			asm_file << "	;; drop\n";
			asm_file << "	add rsp, 8\n";
			break;

		case Intrinsic_Kind::Two_Drop:
			asm_file << "	;; 2drop\n";
			asm_file << "	add rsp, 16\n";
			break;

		case Intrinsic_Kind::Dup:
			asm_file << "	;; dup\n";
			asm_file << "	push qword [rsp]\n";
			break;

		case Intrinsic_Kind::Two_Dup:
			asm_file << "	;; 2dup\n";
			asm_file << "	push qword [rsp+8]\n";
			asm_file << "	push qword [rsp+8]\n";
			break;

		case Intrinsic_Kind::Over:
			asm_file << "	;; over\n";
			asm_file << "	push qword [rsp+8]\n";
			break;

		case Intrinsic_Kind::Two_Over:
			asm_file << "	;; 2over\n";
			asm_file << "	push qword [rsp+24]\n";
			asm_file << "	push qword [rsp+24]\n";
			break;

		case Intrinsic_Kind::Tuck:
			asm_file << "	;; tuck\n";
			asm_file << "	pop rax\n";
			asm_file << "	pop rbx\n";
			asm_file << "	push rax\n";
			asm_file << "	push rbx\n";
			asm_file << "	push rax\n";
			break;

		case Intrinsic_Kind::Rot:
			asm_file << "	;; rot\n";
			asm_file << "	movdqu xmm0, [rsp]\n";
			asm_file << "	mov rcx, [rsp+16]\n";
			asm_file << "	mov [rsp], rcx\n";
			asm_file << "	movups [rsp+8], xmm0\n";
			break;

		case Intrinsic_Kind::Swap:
			asm_file << "	;; swap\n";
			asm_file << "	pop rax\n";
			asm_file << "	pop rbx\n";
			asm_file << "	push rax\n";
			asm_file << "	push rbx\n";
			break;

		case Intrinsic_Kind::Two_Swap:
			asm_file << "	;; 2swap\n";
			asm_file << "	movdqu xmm0, [rsp]\n";
			asm_file << "	mov rax, [rsp+16]\n";
			asm_file << "	mov [rsp], rax\n";
			asm_file << "	mov rax, [rsp+24]\n";
			asm_file << "	mov [rsp+8], rax\n";
			asm_file << "	movups [rsp+16], xmm0\n";
			break;

		case Intrinsic_Kind::Boolean_Negate:
			asm_file << "	;; negate\n";
			asm_file << "	pop rbx\n";
			asm_file << "	xor rax, rax\n";
			asm_file << "	test rbx, rbx\n";
			asm_file << "	sete al\n";
			asm_file << "	push rax\n";
			break;

		Impl_Compare(Equal,       "equal",             "e");
		Impl_Compare(Greater,     "greater",           "a");
		Impl_Compare(Greater_Eq,  "greater or equal",  "nb");
		Impl_Compare(Less,        "less",              "b");
		Impl_Compare(Less_Eq,     "less or equal",     "be");
		Impl_Compare(Not_Equal,   "not equal",         "ne");

		case Intrinsic_Kind::Read8:
		case Intrinsic_Kind::Read16:
		case Intrinsic_Kind::Read32:
		case Intrinsic_Kind::Read64:
			{
				static_assert(linear(1,
					Intrinsic_Kind::Read8, Intrinsic_Kind::Read16,
					Intrinsic_Kind::Read32, Intrinsic_Kind::Read64));
				auto const offset = int(op.intrinsic) - int(Intrinsic_Kind::Read8);
				asm_file << "	;; read" << (8 << offset) << "\n";
				asm_file << "	pop rax\n";
				asm_file << "	xor rbx, rbx\n";
				asm_file << "	mov " << Register_B_By_Size[offset] << ", [rax]\n";
				asm_file << "	push rbx\n";
			}
			break;

		case Intrinsic_Kind::Write8:
		case Intrinsic_Kind::Write16:
		case Intrinsic_Kind::Write32:
		case Intrinsic_Kind::Write64:
			{
				static_assert(linear(1,
					Intrinsic_Kind::Write8, Intrinsic_Kind::Write16,
					Intrinsic_Kind::Write32, Intrinsic_Kind::Write64));
				auto const offset = int(op.intrinsic) - int(Intrinsic_Kind::Write8);
				asm_file << "	;; write" << (8 << offset) << "\n";
				asm_file << "	pop rbx\n";
				asm_file << "	pop rax\n";
				asm_file << "	mov [rax], " << Register_B_By_Size[offset] << "\n";

			}
			break;

		case Intrinsic_Kind::Syscall:
			{
				assert(op.token.sval[7] >= '0' && op.token.sval[7] <= '6');
				unsigned const syscall_count = op.token.sval[7] - '0';
				static char const* regs[] = { "rax", "rdi", "rsi", "rdx", "r10", "r8", "r9" };

				asm_file << "	;; syscall" << syscall_count << '\n';
				for (unsigned i = 0; i <= syscall_count; ++i)
					asm_file << "	pop " << regs[i] << '\n';
				asm_file << "	syscall\n";
				asm_file << "	push rax\n";
			}
			break;
		}
	}

	auto generate_instructions(
			std::vector<Operation> const& ops,
			std::ostream& asm_file,
			[[maybe_unused]] Words &words,
			[[maybe_unused]] std::unordered_set<std::string> &undefined_words,
			std::string_view instr_prefix) -> void
	{

		unsigned i = 0;
		for (auto ops_it = std::cbegin(ops); ops_it != std::cend(ops); ++ops_it, ++i) {
			auto const& op = *ops_it;
			asm_file << instr_prefix << i << ":\n";

			switch (op.kind) {
			case Operation::Kind::Intrinsic:
				emit_intrinsic(op, asm_file);
				break;
			case Operation::Kind::Call_Symbol:
				asm_file << "	;; call symbol\n";
				asm_file << "	call " << Function_Prefix << op.ival << '\n';
				break;
			case Operation::Kind::Push_Symbol:
				asm_file << "	;; push symbol\n";
				asm_file << "	push " << op.symbol_prefix << op.ival << '\n';
				break;
			case Operation::Kind::Push_Int:
				asm_file << "	;; push int\n";
				asm_file << "	mov rax, " << op.ival << '\n';
				asm_file << "	push rax\n";
				break;
			case Operation::Kind::Return:
				asm_file << "	;; return\n";
				asm_file << "	jmp " << instr_prefix << ops.size() << '\n';
				break;
			case Operation::Kind::End:
				asm_file << "	;; end\n";
				if (i + 1 != op.jump)
					asm_file << "	jmp " << instr_prefix << op.jump << '\n';
				break;
			case Operation::Kind::Do:
			case Operation::Kind::If:
				asm_file << "	;; if | do\n";
				asm_file << "	pop rax\n";
				asm_file << "	test rax, rax\n";
				asm_file << "	jz " << instr_prefix << op.jump << '\n';
				break;
			case Operation::Kind::Else:
				assert(op.jump != Operation::Empty_Jump);
				asm_file << "	;; else\n";
				asm_file << "	jmp " << instr_prefix << op.jump << '\n';
				break;
			case Operation::Kind::While:
				asm_file << "	;; while\n";
				break;
			}
		}

		asm_file << instr_prefix << ops.size() << ":";
	}


	auto generate_assembly(std::vector<Operation> const& ops, fs::path const& asm_path, Words &words, std::unordered_map<std::string, unsigned> const& strings)
	{
		std::unordered_set<std::string> undefined_words;

		std::ofstream asm_file(asm_path, std::ios_base::out | std::ios_base::trunc);
		if (!asm_file) {
			error("Cannot generate ASM file ", asm_path);
			return;
		}

		asm_header(asm_file, words, strings);

		char buffer[sizeof(Function_Body_Prefix) + 20];
		for (auto& [name, def] : words) {
			if (def.kind != Word::Kind::Function)
				continue;

			asm_file << ";; fun " << name << '\n';
			asm_file << Function_Prefix << def.id << ":\n";
			asm_file << "	pop rax\n";
			asm_file << "	mov rbx, [_stacky_callptr]\n";
			asm_file << "	mov [_stacky_callstack+rbx*8], rax\n";
			asm_file << "	add qword [_stacky_callptr], 1\n";

			std::sprintf(buffer, Function_Body_Prefix "%lu_", def.id);
			generate_instructions(def.function_body, asm_file, words, undefined_words, buffer);
			asm_file << '\n';
			emit_return(asm_file);
		}

		asm_file << "global _start\n";
		asm_file << "_start:\n";
		generate_instructions(ops, asm_file, words, undefined_words, Label_Prefix);

		asm_file << R"asm(
	;; exit syscall
	mov rax, 60
	mov rdi, 0
	syscall
)asm";
	}
}

#undef Impl_Compare
#undef Impl_Div
#undef Impl_Math
