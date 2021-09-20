#include <array>
#include <cctype>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <source_location>
#include <span>
#include <sstream>
#include <stack>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "errors.cc"
#include "stdlib-symbols.cc"
#include "utilities.cc"

using namespace std::string_view_literals;
namespace fs = std::filesystem;

#define Label_Prefix "_stacky_instr_"
#define Symbol_Prefix "_stacky_symbol_"
#define Function_Prefix "_stacky_fun_"
#define Function_Body_Prefix "_stacky_funinstr_"

auto const& Asm_Footer = R"asm(
	;; exit syscall
	mov rax, 60
	mov rdi, 0
	syscall
)asm";

struct Word
{
	enum class Kind
	{
		// --- MATH ---
		Add,
		Bitwise_And,
		Bitwise_Or,
		Bitwise_Xor,
		Boolean_And,
		Boolean_Negate,
		Boolean_Or,
		Div,
		Div_Mod,
		Equal,
		Greater,
		Greater_Eq,
		Left_Shift,
		Less,
		Less_Eq,
		Max,
		Min,
		Mod,
		Mul,
		Not_Equal,
		Right_Shift,
		Subtract,

		// --- STACK ---
		Drop,
		Dup,
		Over,
		Print_CString,
		Push_Symbol,
		Call_Symbol,
		Rot,
		Swap,
		Tuck,
		Two_Dup,
		Two_Drop,
		Two_Over,
		Two_Swap,

		// --- LITERALS ---
		Identifier,
		Integer,
		String,

		// --- COMPILE TIME DEFINITIONS ---
		Define_Byte_Array,
		Define_Constant,
		Define_Function,

		// --- CONTROL FLOW ---
		Do,
		Else,
		End,
		If,
		While,

		// --- MEMORY ---
		Read8,
		Read16,
		Read32,
		Read64,
		Write8,
		Write16,
		Write32,
		Write64,
		Top,

		// --- STDLIB, OS ---
		Newline,
		Print,
		Syscall0,
		Syscall1,
		Syscall2,
		Syscall3,
		Syscall4,
		Syscall5,
		Syscall6,

		Last = Syscall6,
	};

	static constexpr unsigned Data_Announcing_Kinds = count_args(Kind::Define_Byte_Array, Kind::String);

	static constexpr unsigned Wordless_Kinds = count_args(
		Kind::Identifier,
		Kind::Integer,
		Kind::Push_Symbol,
		Kind::String,
		Kind::Call_Symbol
	);

	std::string_view file;
	unsigned column;
	unsigned line;

	Kind kind;
	uint64_t ival;
	std::string sval;

	// for if and while
	static constexpr unsigned Empty_Jump = -1;
	unsigned jump = Empty_Jump;
};

constexpr auto Words_To_Kinds = sorted_array_of_tuples(
	std::tuple { "!"sv,         Word::Kind::Boolean_Negate },
	std::tuple { "!="sv,        Word::Kind::Not_Equal },
	std::tuple { "*"sv,         Word::Kind::Mul },
	std::tuple { "+"sv,         Word::Kind::Add },
	std::tuple { "-"sv,         Word::Kind::Subtract },
	std::tuple { "."sv,         Word::Kind::Print },
	std::tuple { "2drop"sv,     Word::Kind::Two_Drop },
	std::tuple { "2dup"sv,      Word::Kind::Two_Dup },
	std::tuple { "2over"sv,     Word::Kind::Two_Over },
	std::tuple { "2swap"sv,     Word::Kind::Two_Swap },
	std::tuple { "<"sv,         Word::Kind::Less },
	std::tuple { "<<"sv,        Word::Kind::Left_Shift },
	std::tuple { "<="sv,        Word::Kind::Less_Eq },
	std::tuple { "="sv,         Word::Kind::Equal },
	std::tuple { ">"sv,         Word::Kind::Greater },
	std::tuple { ">="sv,        Word::Kind::Greater_Eq },
	std::tuple { ">>"sv,        Word::Kind::Right_Shift },
	std::tuple { "min"sv,       Word::Kind::Min },
	std::tuple { "max"sv,       Word::Kind::Max },
	std::tuple { "[]byte"sv,    Word::Kind::Define_Byte_Array },
	std::tuple { "and"sv,       Word::Kind::Boolean_And },
	std::tuple { "bit-and"sv,   Word::Kind::Bitwise_And },
	std::tuple { "bit-or"sv,    Word::Kind::Bitwise_Or },
	std::tuple { "bit-xor"sv,   Word::Kind::Bitwise_Xor },
	std::tuple { "constant"sv,  Word::Kind::Define_Constant },
	std::tuple { "div"sv,       Word::Kind::Div },
	std::tuple { "divmod"sv,    Word::Kind::Div_Mod },
	std::tuple { "do"sv,        Word::Kind::Do },
	std::tuple { "drop"sv,      Word::Kind::Drop },
	std::tuple { "dup"sv,       Word::Kind::Dup },
	std::tuple { "else"sv,      Word::Kind::Else },
	std::tuple { "end"sv,       Word::Kind::End },
	std::tuple { "fun"sv,       Word::Kind::Define_Function },
	std::tuple { "if"sv,        Word::Kind::If },
	std::tuple { "mod"sv,       Word::Kind::Mod },
	std::tuple { "nl"sv,        Word::Kind::Newline },
	std::tuple { "or"sv,        Word::Kind::Boolean_Or },
	std::tuple { "over"sv,      Word::Kind::Over },
	std::tuple { "read8"sv,     Word::Kind::Read8 },
	std::tuple { "read16"sv,    Word::Kind::Read16 },
	std::tuple { "read32"sv,    Word::Kind::Read32 },
	std::tuple { "read64"sv,    Word::Kind::Read64 },
	std::tuple { "write8"sv,    Word::Kind::Write8 },
	std::tuple { "write16"sv,   Word::Kind::Write16 },
	std::tuple { "write32"sv,   Word::Kind::Write32 },
	std::tuple { "write64"sv,   Word::Kind::Write64 },
	std::tuple { "print"sv,     Word::Kind::Print_CString },
	std::tuple { "rot"sv,       Word::Kind::Rot },
	std::tuple { "swap"sv,      Word::Kind::Swap },
	std::tuple { "syscall0"sv,  Word::Kind::Syscall0 },
	std::tuple { "syscall1"sv,  Word::Kind::Syscall1 },
	std::tuple { "syscall2"sv,  Word::Kind::Syscall2 },
	std::tuple { "syscall3"sv,  Word::Kind::Syscall3 },
	std::tuple { "syscall4"sv,  Word::Kind::Syscall4 },
	std::tuple { "syscall5"sv,  Word::Kind::Syscall5 },
	std::tuple { "syscall6"sv,  Word::Kind::Syscall6 },
	std::tuple { "top"sv,       Word::Kind::Top },
	std::tuple { "tuck"sv,      Word::Kind::Tuck },
	std::tuple { "while"sv,     Word::Kind::While }
);

static_assert(Words_To_Kinds.size() == static_cast<int>(Word::Kind::Last) + 1 - Word::Wordless_Kinds, "Words_To_Kinds should cover all possible kinds!");

auto parse(std::string_view const file, std::string_view const path, std::vector<Word> &words)
{
	unsigned column = 1, line = 1;

	for (unsigned i = 0; i < file.size();) {
		auto ch = file[i];

		for (;;) {
			bool done_sth = false;

			for (; i < file.size() && std::isspace(ch); ch = file[++i]) {
				done_sth = true;
				if (ch == '\n') {
					++line;
					column = 1;
				} else {
					++column;
				}
			}

			if (i < file.size() && ch == '#') {
				done_sth = true;
				auto after_comment = std::find(std::cbegin(file) + i, std::cend(file), '\n');
				i = after_comment - std::cbegin(file) + 1;
				ch = file[i];
				column = 1;
				line++;
			}

			if (!done_sth)
				break;
		}

		if (i == file.size())
			break;

		auto &word = words.emplace_back(path, column, line);

		if (ch == '"') {
			word.kind = Word::Kind::String;
			auto str_end = std::find(std::cbegin(file) + i + 1, std::cend(file), '"');
			if (str_end == std::cend(file))
				error(word, "Missing terminating \" character");

			word.sval = { std::cbegin(file) + i, str_end + 1 };
		} else {
			auto const start = std::cbegin(file) + i;
			auto const first_ws = std::find_if(start, std::cend(file), static_cast<int(*)(int)>(std::isspace));
			word.sval = { start, first_ws };

			auto found = std::lower_bound(std::cbegin(Words_To_Kinds), std::cend(Words_To_Kinds), word.sval, [](auto const& lhs, auto const& rhs)
					{ return std::get<0>(lhs) < rhs; });

			word.kind = found != std::cend(Words_To_Kinds) && std::get<0>(*found) == word.sval
					? std::get<1>(*found)
					: Word::Kind::Identifier;

			if (found != std::cend(Words_To_Kinds) && std::get<0>(*found) == word.sval) {
				word.kind = std::get<1>(*found);
			} else if (word.sval[0] >= '0' && word.sval[0] <= '9') {
				auto [ptr, ec] = std::from_chars(word.sval.data(), word.sval.data() + word.sval.size(), word.ival);
				if (ptr == word.sval.data() + word.sval.size())
					word.kind = Word::Kind::Integer;
				else
					assert_msg(ptr != file.data() + file.size() ? *ptr != '.' : true, "Floating point parsing is not implemented yet");
			}
		}

		i += word.sval.size();
		column += word.sval.size();
	}

	return true;
}

struct Definition
{
	enum class Kind
	{
		Array,
		Constant,
		Function,
		String,
	};

	Word word;
	Kind kind;
	uint64_t byte_size;

	static inline unsigned definitions_count = 0;
	unsigned id;

	std::vector<Word> function_body = {};
};

using Definitions = std::unordered_map<std::string, Definition>;

auto define_words(std::vector<Word> &words, Definitions &user_defined_words)
{
	for (unsigned i = 0; i < words.size(); ++i) {
		auto &word = words[i];
		switch (word.kind) {
		case Word::Kind::Define_Function: {
			ensure(i >= 1 && words[i-1].kind == Word::Kind::Identifier, word, "function shoud be preceeded by an identifier");
			auto fname = words[i-1].sval;
			auto &def = user_defined_words[fname] = {
				word,
				Definition::Kind::Function,
				(uint64_t)-1,
				Definition::definitions_count++
			};

			unsigned ends_count = 1, end_pos = 0;
			for (; end_pos < words.size() && ends_count > 0; ++end_pos)
				if (auto &other = words[end_pos]; other.kind == Word::Kind::If || other.kind == Word::Kind::While)
					++ends_count;
				else if (other.kind == Word::Kind::End)
					--ends_count;

			ensure(ends_count == 0, word, "fun without end");
			for (unsigned j = 0; j < words.size(); ++j) {
				auto &word = words[j];
				if (word.kind == Word::Kind::Identifier && word.sval == fname) {
					word.kind = Word::Kind::Call_Symbol;
					word.ival = def.id;
				}
			}
			def.function_body.assign(std::cbegin(words) + i + 1, std::cbegin(words) + end_pos - 1);
			i -= 1;
			words.erase(std::cbegin(words) + i, std::cbegin(words) + end_pos);

		} break;
		case Word::Kind::String: {
			auto &def = user_defined_words[word.sval] = {
				word,
				Definition::Kind::String,
				(uint64_t)-1, // TODO calculate size
				Definition::definitions_count++
			};

			for (unsigned j = 0; j < words.size(); ++j) {
				auto &other = words[j];
				if (other.kind == Word::Kind::String && other.sval == word.sval) {
					other.kind = Word::Kind::Push_Symbol;
					other.ival = def.id;
				}
			}
		} break;

		case Word::Kind::Define_Constant: {
			ensure(i >= 2, word,                                    "constant requires compile time integer");
			ensure(words[i-1].kind == Word::Kind::Identifier, word, "constant should be preceeded by an indentifier, e.g. `42 meaning-of-life constant`");
			ensure(words[i-2].kind == Word::Kind::Integer, word,    "constant should be precedded by an integer, e.g. `42 meaning-of-life define-constant`");

			user_defined_words[words[i-1].sval] = {
				word,
				Definition::Kind::Constant,
				0,
				Definition::definitions_count++
			};

			for (unsigned j = 0; j < words.size(); ++j) {
				auto &other = words[j];
				if (other.kind == Word::Kind::Identifier && other.sval == words[i-1].sval && j != i-1) {
					other.kind = Word::Kind::Integer;
					other.ival = words[i-2].ival;
				}
			}
		} break;

		case Word::Kind::Define_Byte_Array: {
			ensure(i >= 2, word,                                    "[]byte requires two compile time arguments!");
			ensure(words[i-1].kind == Word::Kind::Identifier, word, "[]byte should be preceded by an identifier, e.g. `10 foo define-bytes`");
			ensure(words[i-2].kind == Word::Kind::Integer, word,    "[]byte should be precedded by an integer, e.g. `10 foo define-bytes`");

			auto &def = user_defined_words[words[i-1].sval] = {
				word,
				Definition::Kind::Array,
				words[i-2].ival,
				Definition::definitions_count++
			};

			for (unsigned j = 0; j < words.size(); ++j) {
				auto &other = words[j];
				if (other.kind == Word::Kind::Identifier && other.sval == words[i-1].sval && j != i-1) {
					other.kind = Word::Kind::Push_Symbol;
					other.ival = def.id;
				}
			}
			} break;
		default:
			;
		}
	}
}

auto crossreference(std::vector<Word> &words)
{
	std::stack<unsigned> stack;

	for (auto i = 0u; i < words.size(); ++i) {
		auto & word = words[i];
		switch (word.kind) {
		case Word::Kind::Do:
			word.jump = stack.top();
			stack.pop();
			stack.push(i);
			break;

		case Word::Kind::While:
		case Word::Kind::If:
			stack.push(i);
			break;

		case Word::Kind::Else:
			ensure(words[stack.top()].kind == Word::Kind::If, word, "`else` without previous `if`");
			words[stack.top()].jump = i + 1;
			stack.pop();
			stack.push(i);
			break;

		case Word::Kind::End:
			assert(!stack.empty());
			switch (words[stack.top()].kind) {
			case Word::Kind::If:
			case Word::Kind::Else:
				words[stack.top()].jump = i;
				stack.pop();
				words[i].jump = i + 1;
				break;
			case Word::Kind::Do:
				words[i].jump = words[stack.top()].jump;
				words[stack.top()].jump = i + 1;
				break;
			default:
				error(word, "End can only close do and if blocks");
				return false;
			}
			break;

		default:
			;
		}
	}

	return true;
}

auto asm_header(std::ostream &asm_file, Definitions &definitions)
{
	asm_file << "BITS 64\n";

	auto const label = [&](auto &v) -> auto& { return asm_file << '\t' << Symbol_Prefix << v.id << ": "; };

	static_assert(Word::Data_Announcing_Kinds == 2, "Data annoucment not implemented for some words");

	asm_file << "segment .bss\n";
	asm_file << "	_stacky_callstack: resq 1024\n";
	asm_file << "	_stacky_callptr:   resq 1\n";
	for (auto const& [key, value] : definitions) {
		switch (value.word.kind) {
		case Word::Kind::Define_Byte_Array: label(value) << "resb " << value.byte_size << '\n'; break;
		default:
			;
		}
	}

	asm_file << "segment .rodata\n";
	for (auto const& [key, value] : definitions) {
		switch (value.word.kind) {
		case Word::Kind::String: label(value) << "db " << value.word.sval << ", 0\n"; break;
		default:
			;
		}
	}

	asm_file << "segment .text\n" Stdlib_Functions;
}

#define Impl_Math(Op_Kind, Name, Implementation) \
	case Word::Kind::Op_Kind: \
		asm_file << "	;;" Name "\n"; \
		asm_file << "	pop rbx\n"; \
		asm_file << "	pop rax\n"; \
		asm_file << (Implementation); \
		asm_file << "	push rax\n"; \
		break

#define Impl_Compare(Op, Name, Suffix) \
	case Word::Kind::Op: \
		asm_file << "	;; " Name "\n"; \
		asm_file << "	xor rax, rax\n"; \
		asm_file << "	pop rbx\n"; \
		asm_file << "	pop rcx\n"; \
		asm_file << "	cmp rcx, rbx\n"; \
		asm_file << "	set" Suffix " al\n"; \
		asm_file << "	push rax\n"; \
		break

#define Impl_Div(Op, Name, End) \
	case Word::Kind::Op: \
		asm_file << "	;; " Name "\n"; \
		asm_file << "	xor rdx, rdx\n"; \
		asm_file << "	pop rbx\n"; \
		asm_file << "	pop rax\n"; \
		asm_file << "	div rbx\n" End; \
		break

auto generate_instructions(
		std::vector<Word> const& words,
		std::ostream& asm_file,
		Definitions &definitions,
		std::unordered_set<std::string> &undefined_words,
		std::string_view instr_prefix) -> void;

auto emit_return(std::ostream& asm_file)
{
	asm_file << "	sub qword [_stacky_callptr], 1\n";
	asm_file << "	mov rbx, [_stacky_callptr]\n";
	asm_file << "	mov rax, [_stacky_callstack+rbx*8]\n";
	asm_file << "	push rax\n";
	asm_file << "	ret\n";
}

auto generate_assembly(std::vector<Word> const& words, fs::path const& asm_path, Definitions &definitions)
{
	std::unordered_set<std::string> undefined_words;

	std::ofstream asm_file(asm_path, std::ios_base::out | std::ios_base::trunc);
	if (!asm_file) {
		error("Cannot generate ASM file ", asm_path);
		return;
	}

	asm_header(asm_file, definitions);

	char buffer[sizeof(Function_Body_Prefix) + 20];
	for (auto& [name, def] : definitions) {
		if (def.kind != Definition::Kind::Function)
			continue;

		crossreference(def.function_body);
		asm_file << ";; fun " << name << '\n';
		asm_file << Function_Prefix << def.id << ":\n";
		asm_file << "	pop rax\n";
		asm_file << "	mov rbx, [_stacky_callptr]\n";
		asm_file << "	mov [_stacky_callstack+rbx*8], rax\n";
		asm_file << "	add qword [_stacky_callptr], 1\n";

		std::sprintf(buffer, Function_Body_Prefix "%u_", def.id);
		generate_instructions(def.function_body, asm_file, definitions, undefined_words, buffer);
		asm_file << '\n';
		emit_return(asm_file);
	}

	asm_file << "global _start\n";
	asm_file << "_start:\n";
	generate_instructions(words, asm_file, definitions, undefined_words, Label_Prefix);

	asm_file << Asm_Footer;
}

auto generate_instructions(
		std::vector<Word> const& words,
		std::ostream& asm_file,
		Definitions &definitions,
		std::unordered_set<std::string> &undefined_words,
		std::string_view instr_prefix) -> void
{

	auto const word_has_been_defined = [&](auto &word) {
		if (!definitions.contains(word.sval) && !undefined_words.contains(word.sval)) {
			error(word, "Word", std::quoted(word.sval), " has not been defined.");
			undefined_words.insert(word.sval);
			return false;
		}
		return true;
	};

	char const* const Register_B_By_Size[] = { "bl", "bx", "ebx", "rbx" };

	unsigned i = 0;
	for (auto words_it = std::cbegin(words); words_it != std::cend(words); ++words_it, ++i) {
		auto const& word = *words_it;
		asm_file << instr_prefix << i << ":\n";

		switch (word.kind) {
		case Word::Kind::String:
			assert_msg(false, "define_words should eliminate all string words");
			break;

		case Word::Kind::Define_Byte_Array:
		case Word::Kind::Define_Constant:
		case Word::Kind::Define_Function:
			break;

		case Word::Kind::Identifier:
			word_has_been_defined(word);
			break;

		case Word::Kind::Call_Symbol:
			asm_file << "	;; call " << word.sval << '\n';
			asm_file << "	call " Function_Prefix << word.ival << '\n';
			break;

		case Word::Kind::Integer:
			asm_file << "	;; push int " << word.sval << '\n';
			asm_file << "	mov rax, " << word.ival << '\n';
			asm_file << "	push rax\n";
			break;

		case Word::Kind::Print:
			asm_file << "	;; print\n";
			asm_file << "	pop rdi\n";
			asm_file << "	call _stacky_print_u64\n";
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

		case Word::Kind::Top:
			asm_file << "	;; top\n";
			asm_file << "	push rsp\n";
			break;

		case Word::Kind::Drop:
			asm_file << "	;; drop\n";
			asm_file << "	add rsp, 8\n";
			break;

		case Word::Kind::Two_Drop:
			asm_file << "	;; 2drop\n";
			asm_file << "	add rsp, 16\n";
			break;

		case Word::Kind::Dup:
			asm_file << "	;; dup\n";
			asm_file << "	push qword [rsp]\n";
			break;

		case Word::Kind::Two_Dup:
			asm_file << "	;; 2dup\n";
			asm_file << "	push qword [rsp+8]\n";
			asm_file << "	push qword [rsp+8]\n";
			break;

		case Word::Kind::Over:
			asm_file << "	;; over\n";
			asm_file << "	push qword [rsp+8]\n";
			break;

		case Word::Kind::Two_Over:
			asm_file << "	;; 2over\n";
			asm_file << "	push qword [rsp+24]\n";
			asm_file << "	push qword [rsp+24]\n";
			break;

		case Word::Kind::Tuck:
			asm_file << "	;; tuck\n";
			asm_file << "	pop rax\n";
			asm_file << "	pop rbx\n";
			asm_file << "	push rax\n";
			asm_file << "	push rbx\n";
			asm_file << "	push rax\n";
			break;

		case Word::Kind::Rot:
			asm_file << "	;; rot\n";
			asm_file << "	movdqu xmm0, [rsp]\n";
			asm_file << "	mov rcx, [rsp+16]\n";
			asm_file << "	mov [rsp], rcx\n";
			asm_file << "	movups [rsp+8], xmm0\n";
			break;

		case Word::Kind::Swap:
			asm_file << "	;; swap\n";
			asm_file << "	pop rax\n";
			asm_file << "	pop rbx\n";
			asm_file << "	push rax\n";
			asm_file << "	push rbx\n";
			break;

		case Word::Kind::Two_Swap:
			asm_file << "	;; 2swap\n";
			asm_file << "	movdqu xmm0, [rsp]\n";
			asm_file << "	mov rax, [rsp+16]\n";
			asm_file << "	mov [rsp], rax\n";
			asm_file << "	mov rax, [rsp+24]\n";
			asm_file << "	mov [rsp+8], rax\n";
			asm_file << "	movups [rsp+16], xmm0\n";
			break;

		case Word::Kind::Boolean_Negate:
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

		case Word::Kind::Push_Symbol:
			if (word_has_been_defined(word)) {
				asm_file << "	;; push symbol\n";
				asm_file << "	push " Symbol_Prefix << word.ival << '\n';
			}
			break;

		case Word::Kind::Read8:
		case Word::Kind::Read16:
		case Word::Kind::Read32:
		case Word::Kind::Read64: {
			static_assert(linear(1,
				Word::Kind::Read8, Word::Kind::Read16,
				Word::Kind::Read32, Word::Kind::Read64));
			auto const offset = int(word.kind) - int(Word::Kind::Read8);
			asm_file << "	;; read" << (8 << offset) << "\n";
			asm_file << "	pop rax\n";
			asm_file << "	xor rbx, rbx\n";
			asm_file << "	mov " << Register_B_By_Size[offset] << ", [rax]\n";
			asm_file << "	push rbx\n";
		} break;

		case Word::Kind::Write8:
		case Word::Kind::Write16:
		case Word::Kind::Write32:
		case Word::Kind::Write64: {
			static_assert(linear(1,
				Word::Kind::Write8, Word::Kind::Write16,
				Word::Kind::Write32, Word::Kind::Write64));
			auto const offset = int(word.kind) - int(Word::Kind::Write8);
			asm_file << "	;; write" << (8 << offset) << "\n";
			asm_file << "	pop rbx\n";
			asm_file << "	pop rax\n";
			asm_file << "	mov [rax], " << Register_B_By_Size[offset] << "\n";
		} break;

		case Word::Kind::Print_CString:
			asm_file << "	;; print cstring\n";
			asm_file << "	pop rdi\n";
			asm_file << "	call _stacky_print_cstr\n";
			break;

		case Word::Kind::Else:
			assert_msg(word.jump != Word::Empty_Jump, "Call crossreference on words first");
			asm_file << "	;; else\n";
			asm_file << "	jmp " << instr_prefix << word.jump << '\n';
			break;

		case Word::Kind::Do:
			asm_file << "	;; do\n";
			goto ifdo_start;
		case Word::Kind::If:
			asm_file << "	;; if\n";
		ifdo_start:
			assert_msg(word.jump != Word::Empty_Jump, "Call crossreference on words first");
			asm_file << "	pop rax\n";
			asm_file << "	test rax, rax\n";
			asm_file << "	jz " << instr_prefix << word.jump << '\n';
			break;

		case Word::Kind::End:
			asm_file << "	;; end\n";
			assert_msg(word.jump != Word::Empty_Jump, "Call crossreference on words first");
			if (i + 1 != word.jump)
				asm_file << "	jmp " << instr_prefix << word.jump << '\n';
			break;

		case Word::Kind::While:
			asm_file << "	;; while\n";
			break;

		case Word::Kind::Newline:
			asm_file << "	;; newline\n";
			asm_file << "	call _stacky_newline\n";
			break;

		case Word::Kind::Syscall0:
		case Word::Kind::Syscall1:
		case Word::Kind::Syscall2:
		case Word::Kind::Syscall3:
		case Word::Kind::Syscall4:
		case Word::Kind::Syscall5:
		case Word::Kind::Syscall6:
		{
			static_assert(linear(1,
						Word::Kind::Syscall0, Word::Kind::Syscall1, Word::Kind::Syscall2, Word::Kind::Syscall3,
						Word::Kind::Syscall4, Word::Kind::Syscall5, Word::Kind::Syscall6));

			unsigned const syscall_count = unsigned(word.kind) - unsigned(Word::Kind::Syscall0);
			static char const* regs[] = { "rax", "rdi", "rsi", "rdx", "r10", "r8", "r9" };

			asm_file << "	;; syscall" << syscall_count << '\n';
			for (unsigned i = 0; i <= syscall_count; ++i)
				asm_file << "	pop " << regs[i] << '\n';
			asm_file << "	syscall\n";
			asm_file << "	push rax\n";
		} break;

		}
	}

	asm_file << instr_prefix << words.size() << ":";
}

auto main(int argc, char **argv) -> int
{
	auto args = std::span(argv + 1, argc - 1);

	std::vector<std::string> source_files;
	for (std::string_view arg : args) {
		source_files.emplace_back(arg);
	}

	std::vector<Word> words;

	bool compile = true;
	for (auto const& path : source_files) {
		std::ifstream file_stream(path);

		if (!file_stream) {
			error("Source file ", std::quoted(path), " cannot be opened");
			return 1;
		}
		std::string file{std::istreambuf_iterator<char>(file_stream), {}};
		compile &= parse(file, path, words);
	}

	if (!compile)
		return 1;

	auto src_path = fs::path(source_files[0]);
	auto target_path = src_path.parent_path();
	target_path /= src_path.stem();

	auto asm_path = target_path;
	asm_path += ".asm";

	Definitions definitions;
	define_words(words, definitions);

	if (!crossreference(words))
		return 1;

	generate_assembly(words, asm_path, definitions);
	if (Compilation_Failed)
		return 1;

	{
		std::stringstream ss;
		ss << "nasm -felf64 " << asm_path;
		system(ss.str().c_str());
	}

	auto obj_path = target_path;
	obj_path += ".o";
	{
		std::stringstream ss;
		ss << "ld -o " << target_path << " " << obj_path << " stdlib.o";
		system(ss.str().c_str());
	}
}
