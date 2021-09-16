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
		Div,
		Div_Mod,
		Equal,
		Greater,
		Greater_Eq,
		Left_Shift,
		Less,
		Less_Eq,
		Mod,
		Mul,
		Negate,
		Not_Equal,
		Right_Shift,
		Subtract,

		// --- STACK ---
		Dup,
		Print_CString,
		Push_Symbol,
		Swap,
		Two_Dup,

		// --- LITERALS ---
		Identifier,
		Integer,
		String,

		// --- COMPILE TIME DEFINITIONS ---
		Define_Bytes,
		Define_Constant,

		// --- CONTROL FLOW ---
		Do,
		Else,
		End,
		If,
		While,

		// --- MEMORY ---
		Read8,
		Write8,
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

	static constexpr unsigned Data_Announcing_Kinds = count_args(Kind::Define_Bytes, Kind::String);

	static constexpr unsigned Wordless_Kinds = count_args(
		Kind::Identifier,
		Kind::Integer,
		Kind::Push_Symbol,
		Kind::String
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
	std::tuple { "!"sv,                Word::Kind::Negate },
	std::tuple { "!="sv,               Word::Kind::Not_Equal },
	std::tuple { "*"sv,                Word::Kind::Mul },
	std::tuple { "+"sv,                Word::Kind::Add },
	std::tuple { "-"sv,                Word::Kind::Subtract },
	std::tuple { "."sv,                Word::Kind::Print },
	std::tuple { "2dup"sv,             Word::Kind::Two_Dup },
	std::tuple { "<"sv,                Word::Kind::Less },
	std::tuple { "<<"sv,               Word::Kind::Left_Shift },
	std::tuple { "<="sv,               Word::Kind::Less_Eq },
	std::tuple { "="sv,                Word::Kind::Equal },
	std::tuple { ">"sv,                Word::Kind::Greater },
	std::tuple { ">="sv,               Word::Kind::Greater_Eq },
	std::tuple { ">>"sv,               Word::Kind::Right_Shift },
	std::tuple { "define-bytes"sv,     Word::Kind::Define_Bytes },
	std::tuple { "define-constant"sv,  Word::Kind::Define_Constant },
	std::tuple { "div"sv,              Word::Kind::Div },
	std::tuple { "divmod"sv,           Word::Kind::Div_Mod },
	std::tuple { "do"sv,               Word::Kind::Do },
	std::tuple { "dup"sv,              Word::Kind::Dup },
	std::tuple { "else"sv,             Word::Kind::Else },
	std::tuple { "end"sv,              Word::Kind::End },
	std::tuple { "if"sv,               Word::Kind::If },
	std::tuple { "mod"sv,              Word::Kind::Mod },
	std::tuple { "nl"sv,               Word::Kind::Newline },
	std::tuple { "peek"sv,             Word::Kind::Read8 },
	std::tuple { "poke"sv,             Word::Kind::Write8 },
	std::tuple { "print"sv,            Word::Kind::Print_CString },
	std::tuple { "swap"sv,             Word::Kind::Swap },
	std::tuple { "syscall0"sv,         Word::Kind::Syscall0 },
	std::tuple { "syscall1"sv,         Word::Kind::Syscall1 },
	std::tuple { "syscall2"sv,         Word::Kind::Syscall2 },
	std::tuple { "syscall3"sv,         Word::Kind::Syscall3 },
	std::tuple { "syscall4"sv,         Word::Kind::Syscall4 },
	std::tuple { "syscall5"sv,         Word::Kind::Syscall5 },
	std::tuple { "syscall6"sv,         Word::Kind::Syscall6 },
	std::tuple { "top"sv,              Word::Kind::Top },
	std::tuple { "while"sv,            Word::Kind::While }
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
		String
	};

	Word word;
	Kind kind;
	uint64_t byte_size;

	static inline unsigned definitions_count = 0;
	unsigned id;
};

using Definitions = std::unordered_map<std::string, Definition>;

auto define_words(std::vector<Word> &words, Definitions &user_defined_words)
{
	for (unsigned i = 0; i < words.size(); ++i) {
		auto &word = words[i];
		switch (word.kind) {
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
			ensure(i >= 1, word,                                    "define-constant requires compile time integer");
			ensure(words[i-1].kind == Word::Kind::Identifier, word, "define-bytes should be preceeded by an indentifier, e.g. `42 meaning-of-life define-constant`");
			ensure(words[i-2].kind == Word::Kind::Integer, word,    "define-constant should be precedded by an integer, e.g. `42 meaning-of-life define-constant`");

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

		case Word::Kind::Define_Bytes: {
			ensure(i >= 2, word,                                    "define-bytes requires two compile time arguments!");
			ensure(words[i-1].kind == Word::Kind::Identifier, word, "define-bytes should be preceded by an identifier, e.g. `10 foo define-bytes`");
			ensure(words[i-2].kind == Word::Kind::Integer, word,    "define-bytes should be precedded by an integer, e.g. `10 foo define-bytes`");

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
			// TODO turn into error message
			ensure(words[stack.top()].kind == Word::Kind::If, word, "`else` without previous `if`");
			words[stack.top()].jump = i + 1;
			stack.pop();
			stack.push(i);
			break;

		case Word::Kind::End:
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
	for (auto const& [key, value] : definitions) {
		switch (value.word.kind) {
		case Word::Kind::Define_Bytes: label(value) << "resb " << value.byte_size << '\n'; break;
		default:
			;
		}
	}

	asm_file << "segment .data\n";
	for (auto const& [key, value] : definitions) {
		switch (value.word.kind) {
		case Word::Kind::String: label(value) << "db " << value.word.sval << ", 0\n"; break;
		default:
			;
		}
	}

	asm_file << "segment .text\n" Stdlib_Functions;
	asm_file << "global _start\n";
	asm_file << "_start:\n";
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

	auto const word_has_been_defined = [&](auto &word){
		if (!definitions.contains(word.sval) && !undefined_words.contains(word.sval)) {
			error(word, "Word", std::quoted(word.sval), " has not been defined.");
			undefined_words.insert(word.sval);
			return false;
		}
		return true;
	};

	unsigned i = 0;
	for (auto words_it = std::cbegin(words); words_it != std::cend(words); ++words_it, ++i) {
		auto const& word = *words_it;
		asm_file << Label_Prefix << i << ":\n";

		switch (word.kind) {
		case Word::Kind::String:
			assert_msg(false, "define_words should eliminate all string words");
			break;

		case Word::Kind::Define_Bytes:
		case Word::Kind::Define_Constant:
			break;

		case Word::Kind::Identifier:
			word_has_been_defined(word);
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

		case Word::Kind::Add:
			asm_file << "	;; add\n";
			asm_file << "	pop rax\n";
			asm_file << "	pop rbx\n";
			asm_file << "	add rax, rbx\n";
			asm_file << "	push rax\n";
			break;

		case Word::Kind::Subtract:
			asm_file << "	;; subtract\n";
			asm_file << "	pop rbx\n";
			asm_file << "	pop rax\n";
			asm_file << "	sub rax, rbx\n";
			asm_file << "	push rax\n";
			break;

		case Word::Kind::Mul:
			asm_file << "	;; mul\n";
			asm_file << "	pop rax\n";
			asm_file << "	pop rbx\n";
			asm_file << "	imul rax, rbx\n";
			asm_file << "	push rax\n";
			break;

		case Word::Kind::Left_Shift:
			asm_file << "	;; left shift\n";
			asm_file << "	pop rcx\n";
			asm_file << "	pop rbx\n";
			asm_file << "	sal rbx, cl\n";
			asm_file << "	push rbx\n";
			break;

		case Word::Kind::Right_Shift:
			asm_file << "	;; right shift\n";
			asm_file << "	pop rcx\n";
			asm_file << "	pop rbx\n";
			asm_file << "	shr rbx, cl\n";
			asm_file << "	push rbx\n";
			break;

		case Word::Kind::Top:
			asm_file << "	;; top\n";
			asm_file << "	push rsp\n";
			break;

		case Word::Kind::Dup:
			asm_file << "	;; dup\n";
			asm_file << "	pop rax\n";
			asm_file << "	push rax\n";
			asm_file << "	push rax\n";
			break;

		case Word::Kind::Two_Dup:
			asm_file << "	;; 2dup\n";
			asm_file << "	pop rbx\n";
			asm_file << "	pop rax\n";
			for (int i = 0; i < 2; ++i) {
				asm_file << "	push rax\n";
				asm_file << "	push rbx\n";
			}
			break;

		case Word::Kind::Swap:
			asm_file << "	;; swap\n";
			asm_file << "	pop rax\n";
			asm_file << "	pop rbx\n";
			asm_file << "	push rax\n";
			asm_file << "	push rbx\n";
			break;

		case Word::Kind::Div:
			asm_file << "	;; div\n";
			goto divmod_start;
		case Word::Kind::Mod:
			asm_file << "	;; mod\n";
			goto divmod_start;
		case Word::Kind::Div_Mod:
			asm_file << "	;; divmod\n";
divmod_start:
			asm_file << "	xor rdx, rdx\n";
			asm_file << "	pop rbx\n";
			asm_file << "	pop rax\n";
			asm_file << "	div rbx\n";
			if (word.kind == Word::Kind::Mod || word.kind == Word::Kind::Div_Mod)
				asm_file << "	push rdx\n";
			if (word.kind == Word::Kind::Div || word.kind == Word::Kind::Div_Mod)
				asm_file << "	push rax\n";
			break;

		case Word::Kind::Negate:
			asm_file << "	;; negate\n";
			asm_file << "	pop rbx\n";
			asm_file << "	xor rax, rax\n";
			asm_file << "	test rbx, rbx\n";
			asm_file << "	sete al\n";
			asm_file << "	push rax\n";
			break;

		case Word::Kind::Equal:
		case Word::Kind::Not_Equal:
		case Word::Kind::Greater:
		case Word::Kind::Greater_Eq:
		case Word::Kind::Less:
		case Word::Kind::Less_Eq:
			asm_file << "	;; equal\n";
			asm_file << "	xor rax, rax\n";
			asm_file << "	pop rbx\n";
			asm_file << "	pop rcx\n";
			asm_file << "	cmp rcx, rbx\n";
			asm_file << "	set" << (
					word.kind == Word::Kind::Equal      ? "e"  :
					word.kind == Word::Kind::Greater    ? "a"  :
					word.kind == Word::Kind::Greater_Eq ? "nb" :
					word.kind == Word::Kind::Less       ? "b"  :
					word.kind == Word::Kind::Less_Eq    ? "be" :
					word.kind == Word::Kind::Not_Equal  ? "ne" :
						(assert(false), nullptr)
				) << " al\n";
			asm_file << " push rax\n";
			break;

		case Word::Kind::Push_Symbol:
			if (word_has_been_defined(word)) {
				asm_file << "	;; push symbol\n";
				asm_file << "	push " Symbol_Prefix << word.ival << '\n';
			}
			break;

		case Word::Kind::Read8:
			asm_file << "	;; read8\n";
			asm_file << "	pop rax\n";
			asm_file << "	xor rbx, rbx\n";
			asm_file << "	mov bl, [rax]\n";
			asm_file << "	push rbx\n";
			break;

		case Word::Kind::Write8:
			asm_file << "	;; write8\n";
			asm_file << "	pop rbx\n";
			asm_file << "	pop rax\n";
			asm_file << "	mov [rax], bl\n";
			break;

		case Word::Kind::Print_CString:
			asm_file << "	;; print cstring\n";
			asm_file << "	pop rdi\n";
			asm_file << "	call _stacky_print_cstr\n";
			break;

		case Word::Kind::Else:
			assert_msg(word.jump != Word::Empty_Jump, "Call crossreference on words first");
			asm_file << "	;; else\n";
			asm_file << "	jmp " Label_Prefix << word.jump << '\n';
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
			asm_file << "	jz " Label_Prefix << word.jump << '\n';
			break;

		case Word::Kind::End:
			asm_file << "	;; end\n";
			assert_msg(word.jump != Word::Empty_Jump, "Call crossreference on words first");
			if (i + 1 != word.jump)
				asm_file << "	jmp " Label_Prefix << word.jump << '\n';
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
			// TODO make sure that all are in sequence, one after other
			static_assert(Word::Kind::Syscall6 > Word::Kind::Syscall0);
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

	asm_file << Label_Prefix << words.size() << ":" << Asm_Footer;
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
