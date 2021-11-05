#include <array>
#include <cctype>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <source_location>
#include <span>
#include <sstream>
#include <stack>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fmt/core.h>
#include <fmt/format.h>

#include "utilities.cc"

using namespace fmt::literals;
using namespace std::string_view_literals;
namespace fs = std::filesystem;

// GENERATED FILES
#include "enum-names.cc"

#include "arguments.hh"
#include "stacky.hh"

static inline void register_intrinsic(Words &words, std::string_view name, Intrinsic_Kind kind)
{
	auto &i = words[std::string(name)];
	i.kind = Word::Kind::Intrinsic;
	i.intrinsic = kind;
}

void register_intrinsics(Words &words)
{
	words.reserve(words.size() + static_cast<int>(Intrinsic_Kind::Last) + 1);

	register_intrinsic(words, "random32"sv,  Intrinsic_Kind::Random32);
	register_intrinsic(words, "random64"sv,  Intrinsic_Kind::Random64);
	register_intrinsic(words, "!"sv,         Intrinsic_Kind::Boolean_Negate);
	register_intrinsic(words, "!="sv,        Intrinsic_Kind::Not_Equal);
	register_intrinsic(words, "*"sv,         Intrinsic_Kind::Mul);
	register_intrinsic(words, "+"sv,         Intrinsic_Kind::Add);
	register_intrinsic(words, "-"sv,         Intrinsic_Kind::Subtract);
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
	register_intrinsic(words, "or"sv,        Intrinsic_Kind::Boolean_Or);
	register_intrinsic(words, "over"sv,      Intrinsic_Kind::Over);
	register_intrinsic(words, "load16"sv,    Intrinsic_Kind::Load);
	register_intrinsic(words, "load32"sv,    Intrinsic_Kind::Load);
	register_intrinsic(words, "load64"sv,    Intrinsic_Kind::Load);
	register_intrinsic(words, "load8"sv,     Intrinsic_Kind::Load);
	register_intrinsic(words, "rot"sv,       Intrinsic_Kind::Rot);
	register_intrinsic(words, "swap"sv,      Intrinsic_Kind::Swap);
	register_intrinsic(words, "syscall0"sv,  Intrinsic_Kind::Syscall);
	register_intrinsic(words, "syscall1"sv,  Intrinsic_Kind::Syscall);
	register_intrinsic(words, "syscall2"sv,  Intrinsic_Kind::Syscall);
	register_intrinsic(words, "syscall3"sv,  Intrinsic_Kind::Syscall);
	register_intrinsic(words, "syscall4"sv,  Intrinsic_Kind::Syscall);
	register_intrinsic(words, "syscall5"sv,  Intrinsic_Kind::Syscall);
	register_intrinsic(words, "syscall6"sv,  Intrinsic_Kind::Syscall);
	register_intrinsic(words, "top"sv,       Intrinsic_Kind::Top);
	register_intrinsic(words, "tuck"sv,      Intrinsic_Kind::Tuck);
	register_intrinsic(words, "store16"sv,   Intrinsic_Kind::Store);
	register_intrinsic(words, "store32"sv,   Intrinsic_Kind::Store);
	register_intrinsic(words, "store64"sv,   Intrinsic_Kind::Store);
	register_intrinsic(words, "store8"sv,    Intrinsic_Kind::Store);
}

auto search_include_path(fs::path includer_path, fs::path include_path) -> std::optional<fs::path>
{
	if (include_path.has_parent_path()) {
		if (auto local = includer_path / include_path; fs::exists(local) && !fs::is_directory(local)) {
			return { local };
		}
	}

	for (auto const& parent : compiler_arguments.include_search_paths) {
		if (auto p = parent / include_path; fs::exists(p) && !fs::is_directory(p)) {
			return { p };
		}
	}

	return std::nullopt;
}

void generate_jump_targets_lookup(Generation_Info &geninfo, std::vector<Operation> const& ops, std::string_view name = {})
{
	unsigned i = 0;
	for (auto const& op : ops) {
		switch (op.kind) {
		case Operation::Kind::End:
		case Operation::Kind::If:
		case Operation::Kind::Else:
		case Operation::Kind::Do:
			{
				assert(op.jump != Operation::Empty_Jump);
				geninfo.jump_targets_lookup.insert({ name, op.jump });
			}
			break;
		default:
			;
		}
		++i;
	}
}

void generate_jump_targets_lookup(Generation_Info &geninfo)
{
	generate_jump_targets_lookup(geninfo, geninfo.main);
	for (auto const& [name, def] : geninfo.words) {
		if (def.kind != Word::Kind::Function) continue;
		generate_jump_targets_lookup(geninfo, def.function_body, name);
	}
}

auto main(int argc, char **argv) -> int
{
	compiler_arguments.parse(argc, argv);

	std::vector<Token> tokens;

	bool compile = true;
	for (auto const& path : compiler_arguments.source_files) {
		std::ifstream file_stream(path);

		if (!file_stream) {
			error("Source file '{}' cannot be opened"_format(path));
			return 1;
		}
		std::string file{std::istreambuf_iterator<char>(file_stream), {}};
		compile &= lex(file, path, tokens);
	}

	if (!compile)
		return 1;


	std::unordered_set<std::string> already_imported;

	// make lifetime of included paths the lifetime of a program
	// to have ability to print filename path in any moment for
	// error reporting reasons
	std::vector<std::string> included_paths;

	for (;;) {
		auto maybe_include = parser::extract_include_or_import(tokens);
		if (!maybe_include)
			break;

		auto [kind, includer_path, included_path, offset] = *maybe_include;
		auto const pos = tokens.begin() + offset;

		if (kind == Keyword_Kind::Import) {
			included_path += ".stacky";
		}
		auto maybe_included = search_include_path(includer_path, included_path);

		if (!maybe_included) {
			error_fatal(tokens[offset + 1], "Cannot find file {}"_format(included_path.c_str()));
			continue;
		}

		auto path = *std::move(maybe_included);

		if (kind == Keyword_Kind::Import) {
			if (auto full = fs::canonical(path); already_imported.contains(full)) {
				tokens.erase(pos, pos + 2);
				continue;
			} else {
				already_imported.insert(full);
			}
		}

		std::ifstream file_stream(path);
		if (!file_stream) {
			error(tokens[offset + 1], "File {} cannot be opened"_format(path.c_str()));
			return 1;
		}

		std::string file{std::istreambuf_iterator<char>(file_stream), {}};

		std::vector<Token> included_file_tokens;
		compile &= lex(file, included_paths.emplace_back(path.string()), included_file_tokens);

		tokens.erase(pos, pos + 2);

		if (included_file_tokens.empty())
			continue;

		tokens.reserve(tokens.capacity() + included_file_tokens.size());
		tokens.insert(tokens.begin() + offset,
				std::make_move_iterator(included_file_tokens.begin()),
				std::make_move_iterator(included_file_tokens.end()));
	}

	Generation_Info geninfo;

	parser::extract_strings(tokens, geninfo.strings);

	register_intrinsics(geninfo.words);
	parser::register_definitions(tokens, geninfo.words);

	parser::into_operations(tokens, geninfo.main, geninfo.words);
	if (Compilation_Failed)
		return 1;

	if (compiler_arguments.dump_words_effects) {
		for (auto const& [name, word] : geninfo.words) {
			if (!word.has_effect) continue;
			fmt::print("`{}`: {}\n", name, word.effect.string());
		}
	}

	if (compiler_arguments.typecheck) {
		for (auto const& [name, word] : geninfo.words) {
			if (word.kind != Word::Kind::Function)
				continue;

			if (!word.has_effect) {
				warning("function `{}` without type signature"_format(name));
				continue;
			}

			typecheck(word);
		}
		typecheck(geninfo.main);
	}

	optimizer::optimize(geninfo);
	generate_jump_targets_lookup(geninfo);
	linux::x86_64::generate_assembly(geninfo, compiler_arguments.assembly);

	if (Compilation_Failed)
		return 1;

	if (compiler_arguments.control_flow_graph)
		generate_control_flow_graph(geninfo, compiler_arguments.control_flow, compiler_arguments.control_flow_function);

	{
		std::stringstream ss;
		ss << "nasm -felf64 " << compiler_arguments.assembly;
		system(ss.str().c_str());
	}

	auto obj_path = compiler_arguments.executable;
	obj_path += ".o";
	{
		std::stringstream ss;
		ss << "ld -o " << compiler_arguments.executable << " " << obj_path;
		system(ss.str().c_str());
	}

	if (compiler_arguments.run_mode) {
		// TODO pass arguments from compiler
		auto const path = fs::absolute(compiler_arguments.executable);
		execl(path.c_str(), compiler_arguments.executable.c_str(), (char*)NULL);
	}
}
