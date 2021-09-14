#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <vector>
#include <sstream>

#include "stdlib-symbols.cc"

namespace fs = std::filesystem;

auto const& Asm_Header = R"asm(BITS 64
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
		Integer,
		Print,
		Add,
	};

	std::string_view file;
	unsigned column;
	unsigned line;

	Kind kind;
	int64_t ival;
	std::string sval;
};

auto parse(std::string_view const file, std::string_view const path, std::vector<Word> &words)
{
	unsigned column = 0, line = 0;

	for (unsigned i = 0; i < file.size();) {
		auto ch = file[i];
		for (; i < file.size() && std::isspace(ch); ch = file[++i]) {
			if (ch == '\n') {
				++line;
				column = 0;
			} else {
				++column;
			}
		}

		if (i == file.size())
			return;

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

			if (word.sval == ".")
				word.kind = Word::Kind::Print;
			else if (word.sval == "+")
				word.kind = Word::Kind::Add;
			else
				assert(false);
		}

		i += word.sval.size();
		column += word.sval.size();
	}
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
	for (auto const& path : source_files) {
		std::ifstream file_stream(path);

		if (!file_stream) {
			std::cerr << "[ERROR] File \"" << path << "\" cannot be opened!\n";
			return 1;
		}
		std::string file{std::istreambuf_iterator<char>(file_stream), {}};
		parse(file, path, words);
	}

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
