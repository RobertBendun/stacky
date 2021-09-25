#include <array>
#include <cctype>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <source_location>
#include <span>
#include <sstream>
#include <stack>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "errors.cc"
#include "utilities.cc"

using namespace std::string_view_literals;
namespace fs = std::filesystem;

// GENERATED FILES
#include "stdlib-symbols.cc"
#include "enum-names.cc"

#define Label_Prefix "_stacky_instr_"
#define Symbol_Prefix "_stacky_symbol_"
#define String_Prefix "_stacky_string_"
#define Function_Prefix "_stacky_fun_"
#define Function_Body_Prefix "_stacky_funinstr_"

auto const& Asm_Footer = R"asm(
	;; exit syscall
	mov rax, 60
	mov rdi, 0
	syscall
)asm";

struct Location
{
	std::string_view file;
	unsigned column;
	unsigned line;
};

enum class Keyword_Kind
{
	End,
	If,
	Else,
	While,
	Do,

	// Definitions
	Byte_Array,
	Constant,
	Function,

	Last = Function
};

struct Token
{
	enum class Kind
	{
		Word,
		Integer,
		String,
		Keyword,
		Address_Of,
	};

	Location location;

	Kind kind;
	// TODO put these into union
	std::string  sval;
	uint64_t     ival = -1;
	Keyword_Kind kval;
};

static constexpr auto String_To_Keyword = sorted_array_of_tuples(
	std::tuple { "[]byte"sv,    Keyword_Kind::Byte_Array },
	std::tuple { "constant"sv,  Keyword_Kind::Constant },
	std::tuple { "do"sv,        Keyword_Kind::Do },
	std::tuple { "else"sv,      Keyword_Kind::Else },
	std::tuple { "end"sv,       Keyword_Kind::End },
	std::tuple { "fun"sv,       Keyword_Kind::Function },
	std::tuple { "if"sv,        Keyword_Kind::If },
	std::tuple { "while"sv,     Keyword_Kind::While }
);

static_assert(String_To_Keyword.size() == static_cast<size_t>(Keyword_Kind::Last) + 1, "Exhaustive definition of String_To_Keyword. All kinds must be in that list");

auto lex(std::string_view const file, std::string_view const path, std::vector<Token> &tokens)
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

		auto &token = tokens.emplace_back(Location{path, column, line});

		if (ch == '"') {
			token.kind = Token::Kind::String;
			auto const str_end = std::adjacent_find(std::cbegin(file) + i + 1, std::cend(file), [](auto const& prev, auto const& current) {
				return prev != '\\' && current == '\"';
			});

			if (str_end == std::cend(file))
				error(token, "Missing terminating \" character");

			token.sval = { std::cbegin(file) + i, str_end + 2 };
		} else {
			auto const start = std::cbegin(file) + i;
			auto const first_ws = std::find_if(start, std::cend(file), static_cast<int(*)(int)>(std::isspace));
			token.sval = { start, first_ws };

			// is token a keyword?
			if (auto found = std::lower_bound(std::cbegin(String_To_Keyword), std::cend(String_To_Keyword), token.sval, [](auto const& lhs, auto const& rhs)
						{ return std::get<0>(lhs) < rhs; }); found != std::cend(String_To_Keyword) && std::get<0>(*found) == token.sval) {
				token.kind = Token::Kind::Keyword;
				token.kval = std::get<1>(*found);
			} else if (std::all_of(std::cbegin(token.sval), std::cend(token.sval), [](auto c) { return std::isdigit(c) || c == '_'; })) {
				token.kind = Token::Kind::Integer;
				auto [ptr, ec] = std::from_chars(token.sval.data(), token.sval.data() + token.sval.size(), token.ival);
				assert(ptr == token.sval.data() + token.sval.size());
			} else if (token.sval.front() == '&') {
				token.kind = Token::Kind::Address_Of;
			} else {
				token.kind = Token::Kind::Word;
			}
		}

		i += token.sval.size();
		column += token.sval.size();
	}

	return true;
}

enum class Intrinsic_Kind
{
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
		Rot,
		Swap,
		Tuck,
		Two_Dup,
		Two_Drop,
		Two_Over,
		Two_Swap,

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
		Call,

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

struct Operation
{
	enum class Kind
	{
		Intrinsic,
		Push_Symbol,
		Push_Int,
		Call_Symbol,
		End,
		If,
		Else,
		While,
		Do,
	};

	Kind kind;
	Token token;
	uint64_t ival;
	std::string sval;
	Intrinsic_Kind intrinsic;

	static constexpr unsigned Empty_Jump = -1;
	unsigned jump = Empty_Jump;

	std::string_view symbol_prefix;
};

struct Word
{
	enum class Kind
	{
		Intrinsic,
		Integer,
		Array,
		Function,
	};

	Kind kind;
	uint64_t ival;

	Intrinsic_Kind intrinsic;

	uint64_t byte_size;

	static inline uint64_t word_count = 0;
	uint64_t id;

	std::vector<Operation> function_body = {};
	Word *relevant_word = nullptr;
};

using Words = std::unordered_map<std::string, Word>;

inline void register_intrinsic(Words &words, std::string_view name, Intrinsic_Kind kind)
{
	auto &i = words[std::string(name)];
	i.kind = Word::Kind::Intrinsic;
	i.intrinsic = kind;
}

void register_intrinsics(Words &words)
{
	words.reserve(words.size() + static_cast<int>(Intrinsic_Kind::Last) + 1);

	register_intrinsic(words, "!"sv,         Intrinsic_Kind::Boolean_Negate);
	register_intrinsic(words, "!="sv,        Intrinsic_Kind::Not_Equal);
	register_intrinsic(words, "*"sv,         Intrinsic_Kind::Mul);
	register_intrinsic(words, "+"sv,         Intrinsic_Kind::Add);
	register_intrinsic(words, "-"sv,         Intrinsic_Kind::Subtract);
	register_intrinsic(words, "."sv,         Intrinsic_Kind::Print);
	register_intrinsic(words, "2drop"sv,     Intrinsic_Kind::Two_Drop);
	register_intrinsic(words, "2dup"sv,      Intrinsic_Kind::Two_Dup);
	register_intrinsic(words, "2over"sv,     Intrinsic_Kind::Two_Over);
	register_intrinsic(words, "2swap"sv,     Intrinsic_Kind::Two_Swap);
	register_intrinsic(words, "<"sv,         Intrinsic_Kind::Less);
	register_intrinsic(words, "<<"sv,        Intrinsic_Kind::Left_Shift);
	register_intrinsic(words, "<="sv,        Intrinsic_Kind::Less_Eq);
	register_intrinsic(words, "="sv,         Intrinsic_Kind::Equal);
	register_intrinsic(words, ">"sv,         Intrinsic_Kind::Greater);
	register_intrinsic(words, ">="sv,        Intrinsic_Kind::Greater_Eq);
	register_intrinsic(words, ">>"sv,        Intrinsic_Kind::Right_Shift);
	register_intrinsic(words, "and"sv,       Intrinsic_Kind::Boolean_And);
	register_intrinsic(words, "bit-and"sv,   Intrinsic_Kind::Bitwise_And);
	register_intrinsic(words, "bit-or"sv,    Intrinsic_Kind::Bitwise_Or);
	register_intrinsic(words, "bit-xor"sv,   Intrinsic_Kind::Bitwise_Xor);
	register_intrinsic(words, "call"sv,      Intrinsic_Kind::Call);
	register_intrinsic(words, "div"sv,       Intrinsic_Kind::Div);
	register_intrinsic(words, "divmod"sv,    Intrinsic_Kind::Div_Mod);
	register_intrinsic(words, "drop"sv,      Intrinsic_Kind::Drop);
	register_intrinsic(words, "dup"sv,       Intrinsic_Kind::Dup);
	register_intrinsic(words, "max"sv,       Intrinsic_Kind::Max);
	register_intrinsic(words, "min"sv,       Intrinsic_Kind::Min);
	register_intrinsic(words, "mod"sv,       Intrinsic_Kind::Mod);
	register_intrinsic(words, "nl"sv,        Intrinsic_Kind::Newline);
	register_intrinsic(words, "or"sv,        Intrinsic_Kind::Boolean_Or);
	register_intrinsic(words, "over"sv,      Intrinsic_Kind::Over);
	register_intrinsic(words, "print"sv,     Intrinsic_Kind::Print_CString);
	register_intrinsic(words, "read16"sv,    Intrinsic_Kind::Read16);
	register_intrinsic(words, "read32"sv,    Intrinsic_Kind::Read32);
	register_intrinsic(words, "read64"sv,    Intrinsic_Kind::Read64);
	register_intrinsic(words, "read8"sv,     Intrinsic_Kind::Read8);
	register_intrinsic(words, "rot"sv,       Intrinsic_Kind::Rot);
	register_intrinsic(words, "swap"sv,      Intrinsic_Kind::Swap);
	register_intrinsic(words, "syscall0"sv,  Intrinsic_Kind::Syscall0);
	register_intrinsic(words, "syscall1"sv,  Intrinsic_Kind::Syscall1);
	register_intrinsic(words, "syscall2"sv,  Intrinsic_Kind::Syscall2);
	register_intrinsic(words, "syscall3"sv,  Intrinsic_Kind::Syscall3);
	register_intrinsic(words, "syscall4"sv,  Intrinsic_Kind::Syscall4);
	register_intrinsic(words, "syscall5"sv,  Intrinsic_Kind::Syscall5);
	register_intrinsic(words, "syscall6"sv,  Intrinsic_Kind::Syscall6);
	register_intrinsic(words, "top"sv,       Intrinsic_Kind::Top);
	register_intrinsic(words, "tuck"sv,      Intrinsic_Kind::Tuck);
	register_intrinsic(words, "write16"sv,   Intrinsic_Kind::Write16);
	register_intrinsic(words, "write32"sv,   Intrinsic_Kind::Write32);
	register_intrinsic(words, "write64"sv,   Intrinsic_Kind::Write64);
	register_intrinsic(words, "write8"sv,    Intrinsic_Kind::Write8);
}

auto extract_strings(std::vector<Token> &tokens, std::unordered_map<std::string, unsigned> &strings)
{
	static unsigned next_string_id = 0;

	for (auto& token : tokens) {
		if (token.kind != Token::Kind::String)
			continue;

		// TODO insert based not on how string is written in source code
		// but how it's on result basis. Strings like "Hi world" and "Hi\x20world"
		// should resolve into equal pointers
		if (auto [it, inserted] = strings.try_emplace(token.sval, next_string_id); inserted) {
			token.ival = next_string_id++;
		} else {
			token.ival = it->second;
		}
	}
}

auto register_definitions(std::vector<Token> const& tokens, Words &words)
{
	for (unsigned i = 0; i < tokens.size(); ++i) {
		auto token = tokens[i];
		if (token.kind != Token::Kind::Keyword)
			continue;

		switch (token.kval) {
		case Keyword_Kind::End:
		case Keyword_Kind::If:
		case Keyword_Kind::Else:
		case Keyword_Kind::While:
		case Keyword_Kind::Do:
			break;

		case Keyword_Kind::Function:
			{
				ensure(i >= 1 && tokens[i-1].kind == Token::Kind::Word, token, "Function should be preceeded by an identifier");
				auto const& fname = tokens[i-1].sval;
				auto &word = words[fname];
				word.kind = Word::Kind::Function;
				word.id = Word::word_count++;
			}
			break;

		case Keyword_Kind::Constant:
			{
				ensure(i >= 2 && tokens[i-2].kind == Token::Kind::Word, token, "constant must be preceeded by an identifier");
				ensure(i >= 1 && tokens[i-1].kind == Token::Kind::Integer, token, "constant must be preceeded by an integer");
				auto &word = words[tokens[i-2].sval];
				word.kind  = Word::Kind::Integer;
				word.id    = Word::word_count++;
				word.ival  = tokens[i-1].ival;
			}
			break;

		case Keyword_Kind::Byte_Array:
			{
				ensure(i >= 2 && tokens[i-2].kind == Token::Kind::Word, token, "[]byte should be preceeded by an identifier");

				unsigned size = 0;
				switch (auto &t = tokens[i-1]; t.kind) {
				case Token::Kind::Integer:
					size = t.ival;
					break;
				case Token::Kind::Word:
					if (auto it = words.find(t.sval); it != std::end(words) && it->second.kind == Word::Kind::Integer) {
						size = it->second.ival;
						break;
					}
					[[fallthrough]];
				default:
					error(token, "[]byte should be preceeded by an integer");
				}

				auto &word     = words[tokens[i-2].sval];
				word.kind      = Word::Kind::Array;
				word.byte_size = size;
				word.id        = Word::word_count++;
			}
			break;
		}
	}
}

auto crossreference(std::vector<Operation> &ops)
{
	std::stack<unsigned> stack;

	for (auto i = 0u; i < ops.size(); ++i) {
		auto &op = ops[i];
		switch (op.kind) {
		case Operation::Kind::Do:
			op.jump = stack.top();
			stack.pop();
			stack.push(i);
			break;

		case Operation::Kind::While:
		case Operation::Kind::If:
			stack.push(i);
			break;

		case Operation::Kind::Else:
			// TODO add ensure of if existance
			ops[stack.top()].jump = i + 1;
			stack.pop();
			stack.push(i);
			break;

		case Operation::Kind::End:
			{
				assert(!stack.empty());
				switch (ops[stack.top()].kind) {
				case Operation::Kind::If:
				case Operation::Kind::Else:
					ops[stack.top()].jump = i;
					stack.pop();
					ops[i].jump = i + 1;
					break;
				case Operation::Kind::Do:
					ops[i].jump = ops[stack.top()].jump;
					ops[stack.top()].jump = i + 1;
					stack.pop();
					break;
				default:
					// TODO vvvvvvvvvvvvvvvvvvvvv
					// error(op, "End can only close do and if blocks");
					return false;
				}
			}
			break;

		default:
			;
		}
	}

	return true;
}

void transform_into_operations(std::span<Token> const& tokens, std::vector<Operation> &body, Words& words)
{
	for (unsigned i = tokens.size() - 1; i < tokens.size(); --i) {
		auto &token = tokens[i];
		switch (token.kind) {
		case Token::Kind::Address_Of:
			{
				auto &op = body.emplace_back(Operation::Kind::Push_Symbol);
				op.symbol_prefix = Function_Prefix;
				op.ival = words.at(token.sval.substr(1)).id;
				op.token = token;
			}
			break;

		case Token::Kind::Integer:
			{
				auto &op = body.emplace_back(Operation::Kind::Push_Int);
				op.ival = token.ival;
				op.token = token;
			}
			break;

		case Token::Kind::String:
			{
				auto &op = body.emplace_back(Operation::Kind::Push_Symbol);
				op.symbol_prefix = String_Prefix;
				op.token = token;
				op.ival = token.ival;
			}
			break;

		case Token::Kind::Word:
			{
				auto word_it = words.find(token.sval);
				assert(word_it != std::end(words));
				switch (auto word = word_it->second; word.kind) {
				case Word::Kind::Intrinsic:
					{
						auto &op = body.emplace_back(Operation::Kind::Intrinsic);
						op.intrinsic = word.intrinsic;
					}
					break;
				case Word::Kind::Integer:
					{
						auto &op = body.emplace_back(Operation::Kind::Push_Int);
						op.ival = word.ival;
					}
					break;
				case Word::Kind::Array:
					{
						auto &op = body.emplace_back(Operation::Kind::Push_Symbol);
						op.symbol_prefix = Symbol_Prefix;
						op.ival = word.id;
					}
					break;
				case Word::Kind::Function:
					{
						auto &op = body.emplace_back(Operation::Kind::Call_Symbol);
						op.sval = token.sval;
						op.symbol_prefix = Function_Prefix;
						op.ival = word.id;
					}
					break;
				}
			}
			break;

		case Token::Kind::Keyword:
			{
				switch (token.kval) {
				case Keyword_Kind::End:
					{
						unsigned j, end_stack = 1;
						assert(i >= 1);
						for (j = i-1; j < tokens.size() && end_stack > 0; --j) {
							if (auto &t = tokens[j]; t.kind == Token::Kind::Keyword)
								switch (t.kval) {
								case Keyword_Kind::End:      ++end_stack; break;
								case Keyword_Kind::Function: --end_stack; break;
								case Keyword_Kind::If:       --end_stack; break;
								case Keyword_Kind::While:    --end_stack; break;
								default:
									;
								}
						}

						assert(end_stack == 0);
						++j;

						if (tokens[j].kval == Keyword_Kind::Function) {
							auto &func = words.at(tokens[j-1].sval);
							transform_into_operations({ tokens.begin() + j + 1, i - j - 1 }, func.function_body, words);
							i = j-1;
						} else {
							body.emplace_back(Operation::Kind::End);
						}
					}
					break;

					case Keyword_Kind::Byte_Array:  i -= 2; break;
					case Keyword_Kind::Constant:    i -= 2; break;
					case Keyword_Kind::Function:    i -= 1; break;

					case Keyword_Kind::Do:          body.emplace_back(Operation::Kind::Do);     break;
					case Keyword_Kind::Else:        body.emplace_back(Operation::Kind::Else);   break;
					case Keyword_Kind::If:          body.emplace_back(Operation::Kind::If);     break;
					case Keyword_Kind::While:       body.emplace_back(Operation::Kind::While);  break;
				}
			}
			break;
		}
	}

	std::reverse(std::begin(body), std::end(body));
	crossreference(body);
}

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
		asm_file << String_Prefix << value << ": db `" << std::string_view(key).substr(1, key.size() - 2) << "`, 0\n";
	}

	asm_file << "segment .text\n" Stdlib_Functions;
}

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

auto emit_return(std::ostream& asm_file)
{
	asm_file << "	sub qword [_stacky_callptr], 1\n";
	asm_file << "	mov rbx, [_stacky_callptr]\n";
	asm_file << "	mov rax, [_stacky_callstack+rbx*8]\n";
	asm_file << "	push rax\n";
	asm_file << "	ret\n";
}

auto generate_instructions(
		std::vector<Operation> const& ops,
		std::ostream& asm_file,
		Words &words,
		std::unordered_set<std::string> &undefined_words,
		std::string_view instr_prefix) -> void;

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

	asm_file << Asm_Footer;
}

auto emit_intrinsic(Operation const& op, std::ostream& asm_file)
{
	static char const* const Register_B_By_Size[] = { "bl", "bx", "ebx", "rbx" };

	assert(op.kind == Operation::Kind::Intrinsic);
	switch (op.intrinsic) {
	case Intrinsic_Kind::Call:
		asm_file << "	;; stack call\n";
		asm_file << "	pop rax\n";
		asm_file << "	call rax\n";
		break;

	case Intrinsic_Kind::Print:
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

	case Intrinsic_Kind::Print_CString:
		asm_file << "	;; print cstring\n";
		asm_file << "	pop rdi\n";
		asm_file << "	call _stacky_print_cstr\n";
		break;

	case Intrinsic_Kind::Newline:
		asm_file << "	;; newline\n";
		asm_file << "	call _stacky_newline\n";
		break;

	case Intrinsic_Kind::Syscall0:
	case Intrinsic_Kind::Syscall1:
	case Intrinsic_Kind::Syscall2:
	case Intrinsic_Kind::Syscall3:
	case Intrinsic_Kind::Syscall4:
	case Intrinsic_Kind::Syscall5:
	case Intrinsic_Kind::Syscall6:
		{
			static_assert(linear(1,
						Intrinsic_Kind::Syscall0, Intrinsic_Kind::Syscall1, Intrinsic_Kind::Syscall2, Intrinsic_Kind::Syscall3,
						Intrinsic_Kind::Syscall4, Intrinsic_Kind::Syscall5, Intrinsic_Kind::Syscall6));

			unsigned const syscall_count = unsigned(op.intrinsic) - unsigned(Intrinsic_Kind::Syscall0);
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

auto main(int argc, char **argv) -> int
{
	auto args = std::span(argv + 1, argc - 1);

	std::vector<std::string> source_files;
	for (std::string_view arg : args) {
		source_files.emplace_back(arg);
	}

	std::vector<Token> tokens;

	bool compile = true;
	for (auto const& path : source_files) {
		std::ifstream file_stream(path);

		if (!file_stream) {
			error("Source file ", std::quoted(path), " cannot be opened");
			return 1;
		}
		std::string file{std::istreambuf_iterator<char>(file_stream), {}};
		compile &= lex(file, path, tokens);
	}

	if (!compile)
		return 1;

	std::unordered_map<std::string, unsigned> strings;
	extract_strings(tokens, strings);

	std::unordered_map<std::string, Word> words;
	register_intrinsics(words);
	register_definitions(tokens, words);

	auto src_path = fs::path(source_files[0]);
	auto target_path = src_path.parent_path();
	target_path /= src_path.stem();

	auto asm_path = target_path;
	asm_path += ".asm";

	std::vector<Operation> main;
	transform_into_operations(tokens, main, words);

	generate_assembly(main, asm_path, words, strings);
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
