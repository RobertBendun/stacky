#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <span>
#include <sstream>
#include <stack>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "stdlib-symbols.cc"

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
		Add,
		Define_Bytes,
		Div_Mod,
		Do,
		Dup,
		Else,
		End,
		Equal,
		Identifier,
		If,
		Integer,
		Mod,
		Negate,
		Newline,
		Print,
		Print_CString,
		Push_Symbol,
		Read8,
		Swap,
		While,
		Write8,

		Last = Write8,
	};

	static constexpr unsigned Wordless_Kinds = 3;

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

// NEEEEEEDS TO BE SORTED !!!!!!!!!!!!!1
constexpr auto Words_To_Kinds = std::array {
	std::tuple { "!"sv,             Word::Kind::Negate },
	std::tuple { "+"sv,             Word::Kind::Add },
	std::tuple { "."sv,             Word::Kind::Print },
	std::tuple { "="sv,             Word::Kind::Equal },
	std::tuple { "define-bytes"sv,  Word::Kind::Define_Bytes },
	std::tuple { "divmod"sv,        Word::Kind::Div_Mod },
	std::tuple { "do"sv,            Word::Kind::Do },
	std::tuple { "dup"sv,           Word::Kind::Dup },
	std::tuple { "else"sv,          Word::Kind::Else },
	std::tuple { "end"sv,           Word::Kind::End },
	std::tuple { "if"sv,            Word::Kind::If },
	std::tuple { "mod"sv,           Word::Kind::Mod },
	std::tuple { "nl"sv,            Word::Kind::Newline },
	std::tuple { "peek"sv,          Word::Kind::Read8 },
	std::tuple { "poke"sv,          Word::Kind::Write8 },
	std::tuple { "print"sv,         Word::Kind::Print_CString },
	std::tuple { "swap"sv,          Word::Kind::Swap },
	std::tuple { "while"sv,         Word::Kind::While },
};

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

		if (ch >= '0' && ch <= '9') {
			word.kind = Word::Kind::Integer;

			auto p = file.data() + i;
			auto [ptr, ec] = std::from_chars(p, file.data() + file.size(), word.ival);
			// if *ptr == '.' then we should try parsing a floating point
			assert(ec == std::errc{});
			word.sval = { p, ptr };
		} else {
			auto const start = std::cbegin(file) + i;
			auto const first_ws = std::find_if(start, std::cend(file), static_cast<int(*)(int)>(std::isspace));
			word.sval = { start, first_ws };

			auto found = std::lower_bound(std::cbegin(Words_To_Kinds), std::cend(Words_To_Kinds), word.sval, [](auto const& lhs, auto const& rhs)
					{ return std::get<0>(lhs) < rhs; });

			word.kind = found != std::cend(Words_To_Kinds) && std::get<0>(*found) == word.sval
					? std::get<1>(*found)
					: Word::Kind::Identifier;
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
		Array
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
		case Word::Kind::Define_Bytes: {
			assert(i >= 2);
			assert(words[i-1].kind == Word::Kind::Identifier);
			assert(words[i-2].kind == Word::Kind::Integer);
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
			stack.push(i);
			break;

		case Word::Kind::If:
			stack.push(i);
			break;

		case Word::Kind::Else:
			// TODO turn into error message
			assert(words[stack.top()].kind == Word::Kind::If);
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
				std::cerr << "[ERROR] " << word.file << ':' << word.line << ':' << word.column << " End only can close do and if blocks now\n";
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
	asm_file << "segment .bss\n";

	for (auto const& [key, value] : definitions) {
		asm_file << '\t' << Symbol_Prefix << value.id << ": resb " << value.byte_size << '\n';
	}

	asm_file << "segment .text\n" Stdlib_Functions;
	asm_file << "global _start\n";
	asm_file << "_start:\n";
}

auto generate_assembly(std::vector<Word> const& words, fs::path const& asm_path, Definitions &definitions)
{
	std::unordered_set<std::string> undefined_words;
	bool compilation_failed = false;

	std::ofstream asm_file(asm_path, std::ios_base::out | std::ios_base::trunc);
	if (!asm_file) {
		std::cerr << "[ERROR] Cannot create ASM file " << asm_path << '\n';
		return false;
	}

	asm_header(asm_file, definitions);

	auto const word_has_been_defined = [&](auto &word){
		if (!definitions.contains(word.sval) && !undefined_words.contains(word.sval)) {
			compilation_failed = true;
			std::cerr << "[ERROR] " << word.file << ':' << word.line << ':' << word.column << ": Word " << std::quoted(word.sval) << " has not been defined.\n";
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
		case Word::Kind::Define_Bytes:
			break;

		case Word::Kind::Identifier:
			word_has_been_defined(word);
			break;

		case Word::Kind::Integer:
			asm_file << "	;; push int " << word.sval << '\n';
			asm_file << "	push " << word.ival << '\n';
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

		case Word::Kind::Dup:
			asm_file << "	;; dup\n";
			asm_file << "	pop rax\n";
			asm_file << "	push rax\n";
			asm_file << "	push rax\n";
			break;

		case Word::Kind::Swap:
			asm_file << "	;; swap\n";
			asm_file << "	pop rax\n";
			asm_file << "	pop rbx\n";
			asm_file << "	push rax\n";
			asm_file << "	push rbx\n";
			break;

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
			if (word.kind == Word::Kind::Div_Mod)
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
			asm_file << "	;; equal\n";
			asm_file << "	xor rax, rax\n";
			asm_file << "	pop rcx\n";
			asm_file << "	pop rbx\n";
			asm_file << "	cmp rcx, rbx\n";
			asm_file << "	sete al\n";
			asm_file << " push rax\n";
			break;

		case Word::Kind::Push_Symbol:
			word_has_been_defined(word);
			asm_file << "	;; push symbol\n";
			asm_file << "	push " Symbol_Prefix << word.ival << '\n';
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
			asm_file << "	;; else\n";
			asm_file << "	jmp " Label_Prefix << word.jump << '\n';
			break;

		case Word::Kind::Do:
		case Word::Kind::If:
			assert(word.jump != Word::Empty_Jump); // Call crossreference on words first
			asm_file << (word.kind == Word::Kind::If ? "	;; if\n" : "	;; do\n");
			asm_file << "	pop rax\n";
			asm_file << "	test rax, rax\n";
			asm_file << "	jz " Label_Prefix << word.jump << '\n';
			break;

		case Word::Kind::End:
			asm_file << "	;; end\n";
			assert(word.jump != Word::Empty_Jump); // Call crossreference on words first
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
		}
	}

	asm_file << Label_Prefix << words.size() << ":\n" << Asm_Footer;

	return !compilation_failed;
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
			std::cerr << "[ERROR] File \"" << path << "\" cannot be opened!\n";
			return 1;
		}
		std::string file{std::istreambuf_iterator<char>(file_stream), {}};
		compile &= parse(file, path, words);
	}

	if (!compile)
		return 1;

	auto const target_path =
		(source_files.size() == 1
			? fs::path(source_files[0])
			: fs::absolute(fs::path("."))
		).stem();

	auto asm_path = target_path;
	asm_path += ".asm";

	Definitions definitions;
	define_words(words, definitions);

	if (!crossreference(words))
		return 1;

	std::cout << "[INFO] Generating assembly into " << asm_path << '\n';
	if (!generate_assembly(words, asm_path, definitions))
		return 1;

	{
		std::cout << "[INFO] Assembling " << asm_path << '\n';
		std::stringstream ss;
		ss << "nasm -felf64 " << asm_path;
		system(ss.str().c_str());
	}

	auto obj_path = target_path;
	obj_path += ".o";
	{
		std::cout << "[INFO] Linking...\n";
		std::stringstream ss;
		ss << "ld -o " << target_path << " " << obj_path << " stdlib.o";
		system(ss.str().c_str());
	}
}
