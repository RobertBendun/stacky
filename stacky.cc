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
#include <tuple>
#include <vector>

#include "stdlib-symbols.cc"

using namespace std::string_view_literals;
namespace fs = std::filesystem;

auto const& Asm_Header = R"asm(BITS 64
segment .bss
	heap: resb 640000

segment .text
)asm"
Stdlib_Functions
R"asm(
global _start
_start:
)asm";

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
		Div_Mod,
		Dup,
		Equal,
		Heap,
		Integer,
		Negate,
		Print,
		Print_CString,
		Read8,
		Swap,
		Write8,

		Last = Write8,
	};

	static constexpr unsigned Wordless_Kinds = 1;

	std::string_view file;
	unsigned column;
	unsigned line;

	Kind kind;
	uint64_t ival;
	std::string sval;
};

// NEEEEEEDS TO BE SORTED !!!!!!!!!!!!!1
constexpr auto Words_To_Kinds = std::array {
	std::tuple { "!"sv, Word::Kind::Negate },
	std::tuple { "+"sv, Word::Kind::Add },
	std::tuple { "."sv, Word::Kind::Print },
	std::tuple { "="sv, Word::Kind::Equal },
	std::tuple { "divmod"sv, Word::Kind::Div_Mod },
	std::tuple { "dup"sv, Word::Kind::Dup },
	std::tuple { "heap"sv, Word::Kind::Heap },
	std::tuple { "peek"sv, Word::Kind::Read8 },
	std::tuple { "poke"sv, Word::Kind::Write8 },
	std::tuple { "print"sv, Word::Kind::Print_CString },
	std::tuple { "swap"sv, Word::Kind::Swap },
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

			if (found == std::cend(Words_To_Kinds) || std::get<0>(*found) != word.sval) {
				std::cerr << "[ERROR] " << word.file << ':' << word.line << ':' << word.column << ": Word " << std::quoted(word.sval) << " does not exists\n";
				return false;
			}
			word.kind = std::get<1>(*found);
		}

		i += word.sval.size();
		column += word.sval.size();
	}

	return true;
}

auto generate_assembly(std::vector<Word> const& words, fs::path const& asm_path)
{
	std::ofstream asm_file(asm_path, std::ios_base::out | std::ios_base::trunc);
	if (!asm_file) {
		std::cerr << "[ERROR] Cannot create ASM file " << asm_path << '\n';
		return false;
	}

	asm_file << Asm_Header;

	for (auto const& word : words) {
		switch (word.kind) {
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

		case Word::Kind::Div_Mod:
			asm_file << "	;; divmod\n";
			asm_file << "	xor rdx, rdx\n";
			asm_file << "	pop rbx\n";
			asm_file << "	pop rax\n";
			asm_file << "	div rbx\n";
			asm_file << "	push rdx\n";
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

		case Word::Kind::Heap:
			asm_file << "	;; heap\n";
			asm_file << "	push heap\n";
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
		}
	}
	asm_file << Asm_Footer;

	return true;
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

	std::cout << "[INFO] Generating assembly into " << asm_path << '\n';
	if (!generate_assembly(words, asm_path))
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
