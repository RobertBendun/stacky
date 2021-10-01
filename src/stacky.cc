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

#include "errors.cc"
#include "utilities.cc"

using namespace std::string_view_literals;
namespace fs = std::filesystem;

// GENERATED FILES
#include "enum-names.cc"

#define Label_Prefix "_Stacky_instr_"
#define Symbol_Prefix "_Stacky_symbol_"
#define String_Prefix "_Stacky_string_"
#define Function_Prefix "_Stacky_fun_"
#define Function_Body_Prefix "_Stacky_funinstr_"
#define Anonymous_Function_Prefix "_Stacky_anonymous_"

struct Arguments
{
	std::vector<fs::path> include_search_paths;
	std::vector<std::string> source_files;
	fs::path compiler;
	fs::path executable;
	fs::path assembly;

	bool warn_redefinitions = true;

	bool run_mode = false;
};

Arguments compiler_arguments;

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
	Include,
	Import,
	Return,

	// Definitions
	Array,
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
		Char,
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
	std::tuple { "[]byte"sv,    Keyword_Kind::Array },
	std::tuple { "[]u16"sv,     Keyword_Kind::Array },
	std::tuple { "[]u32"sv,     Keyword_Kind::Array },
	std::tuple { "[]u64"sv,     Keyword_Kind::Array },
	std::tuple { "[]u8"sv,      Keyword_Kind::Array },
	std::tuple { "[]usize"sv,   Keyword_Kind::Array },
	std::tuple { "constant"sv,  Keyword_Kind::Constant },
	std::tuple { "do"sv,        Keyword_Kind::Do },
	std::tuple { "else"sv,      Keyword_Kind::Else },
	std::tuple { "end"sv,       Keyword_Kind::End },
	std::tuple { "fun"sv,       Keyword_Kind::Function },
	std::tuple { "&fun"sv,      Keyword_Kind::Function },
	std::tuple { "if"sv,        Keyword_Kind::If },
	std::tuple { "include"sv,   Keyword_Kind::Include },
	std::tuple { "import"sv,    Keyword_Kind::Import },
	std::tuple { "return"sv,    Keyword_Kind::Return },
	std::tuple { "while"sv,     Keyword_Kind::While }
);


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
		Random32,
		Random64,
		Right_Shift,
		Subtract,

		// --- STACK ---
		Drop,
		Dup,
		Over,
		Rot,
		Swap,
		Tuck,
		Two_Dup,
		Two_Drop,
		Two_Over,
		Two_Swap,

		// --- MEMORY ---
		Load,  // previously known as Read8, Read16...
	 	Store, // previously known as Write8, Write16, ...
		Top,
		Call,

		// --- STDLIB, OS ---
		Syscall,

		Last = Syscall,
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
		Return,
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

struct Label_Info
{
	std::string_view function;
	unsigned jump;
	auto operator<=>(Label_Info const&) const = default;
};

struct Generation_Info
{
	std::unordered_map<std::string, unsigned> strings;
	std::unordered_map<std::string, Word> words;
	std::vector<Operation> main;

	std::unordered_set<std::string> undefined_words;
	std::set<Label_Info> jump_targets_lookup;
};

#include "unicode.cc"
#include "arguments.cc"
#include "lexer.cc"
#include "parser.cc"
#include "linux-x86_64.cc"

inline void register_intrinsic(Words &words, std::string_view name, Intrinsic_Kind kind)
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

void remove_unused_words_and_strings(
		Generation_Info &geninfo,
		std::vector<Operation> const& function_body,
		std::unordered_set<std::uint64_t> &used_words,
		std::unordered_set<std::uint64_t> &used_strings)
{
	for (auto const& op : function_body) {
		if (op.kind != Operation::Kind::Push_Symbol && op.kind != Operation::Kind::Call_Symbol)
			continue;

		if (op.token.kind == Token::Kind::String) {
			used_strings.insert(op.token.ival);
		} else {
			if (used_words.contains(op.ival))
				continue;

			used_words.insert(op.ival);
			auto const word = std::find_if(std::cbegin(geninfo.words), std::cend(geninfo.words), [word_id = op.ival](auto const &entry)
			{
				return entry.second.id == word_id;
			});
			assert(word != std::cend(geninfo.words));
			if (word->second.kind != Word::Kind::Function)
				continue;
			remove_unused_words_and_strings(geninfo, word->second.function_body, used_words, used_strings);
		}
	}
}

auto remove_unused_words_and_strings(Generation_Info &geninfo) -> bool
{
	std::unordered_set<std::uint64_t> used_words;
	std::unordered_set<std::uint64_t> used_strings;

	remove_unused_words_and_strings(geninfo, geninfo.main, used_words, used_strings);
	auto const removed_words = std::erase_if(geninfo.words, [&](auto const& entry) {
		return (entry.second.kind == Word::Kind::Function || entry.second.kind == Word::Kind::Array) && !used_words.contains(entry.second.id);
	});

	auto const removed_strings = std::erase_if(geninfo.strings, [&](auto const& entry) {
		return !used_strings.contains(entry.second);
	});

	return removed_words + removed_strings;
	// TODO introduce verbose flag
#if 0
	std::cout << "Removed " << removed_words << " functions and arrays\n";
	std::cout << "Removed " << removed_strings << " strings\n";
#endif
}

auto optimize_comptime_known_conditions([[maybe_unused]] Generation_Info &geninfo, std::vector<Operation> &function_body) -> bool
{
	bool done_something = false;

	for (auto i = 1u; i < function_body.size(); ++i) {
		auto const& condition = function_body[i-1];
		auto const& branch = function_body[i];
		if (condition.kind != Operation::Kind::Push_Int || branch.kind != Operation::Kind::Do && branch.kind != Operation::Kind::If)
			continue;
		done_something = true;

		auto removed_operations = 0u;

		switch (branch.kind) {
		case Operation::Kind::Do:
			{
				if (condition.ival != 0) {
					std::cout << "Infinite loop\n";
					// infinite loop case
					removed_operations += 2;
					function_body.erase(std::cbegin(function_body) + branch.jump, std::end(function_body));
					function_body.erase(std::cbegin(function_body) + i - 1, std::cbegin(function_body) + i + 1);
				} else {
					// eliminate loop body, condition and `while`
					removed_operations += branch.jump - i + 2;
					function_body.erase(std::cbegin(function_body) + i - 1, std::cbegin(function_body) + branch.jump);
				}

				auto condition_start_pos = unsigned(i-2);
				while (condition_start_pos < function_body.size() &&
						function_body[condition_start_pos].kind != Operation::Kind::While && function_body[condition_start_pos].jump != i)
					--condition_start_pos;
				function_body.erase(std::cbegin(function_body) + condition_start_pos);
				++removed_operations;
			}
			break;
		case Operation::Kind::If:
			{
				if (condition.ival != 0) {
					// Do `if` has else branch? If yes, remove it. Otherwise remove unnesesary end operation
					if (auto const else_branch = branch.jump-1; function_body[else_branch].kind == Operation::Kind::Else) {
						removed_operations += function_body[else_branch].jump + 1 - else_branch;
						function_body.erase(std::cbegin(function_body) + else_branch, std::cbegin(function_body) + function_body[else_branch].jump + 1);
					} else {
						assert(function_body[else_branch].kind == Operation::Kind::End);
						function_body.erase(std::cbegin(function_body) + else_branch);
						++removed_operations;
					}
					function_body.erase(std::cbegin(function_body) + i - 1, std::cbegin(function_body) + i + 1);
					removed_operations += 2;
				} else {
					// Do `if` has else branch? If yes, remove `end` operation from it
					auto const else_branch = branch.jump-1;
					if (function_body[else_branch].kind == Operation::Kind::Else) {
						assert(function_body[function_body[else_branch].jump].kind == Operation::Kind::End);
						function_body.erase(std::cbegin(function_body) + function_body[else_branch].jump);
						++removed_operations;
					}

					// remove then branch and condition
					removed_operations += else_branch - i + 2;
					function_body.erase(std::cbegin(function_body) + i - 1, std::cbegin(function_body) + else_branch + 1);
				}
			}
			break;
		default:
			;
		}

		for (auto &op : function_body) {
			if (op.jump >= i + removed_operations)
				op.jump -= removed_operations;
		}
	}

	return done_something;
}

auto optimize_comptime_known_conditions(Generation_Info &geninfo) -> bool
{
	return optimize_comptime_known_conditions(geninfo, geninfo.main);
}

auto main(int argc, char **argv) -> int
{
	parse_arguments(argc, argv);

	std::vector<Token> tokens;

	bool compile = true;
	for (auto const& path : compiler_arguments.source_files) {
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
			error_fatal(tokens[offset + 1], "Cannot find file ", included_path);
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
			error(tokens[offset + 1], "File ", path, " cannot be opened");
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

	parser::transform_into_operations(tokens, geninfo.main, geninfo.words);
	if (Compilation_Failed)
		return 1;


	while (remove_unused_words_and_strings(geninfo)
		|| optimize_comptime_known_conditions(geninfo))
	{
	}
	generate_jump_targets_lookup(geninfo);

	linux::x86_64::generate_assembly(geninfo, compiler_arguments.assembly);

	if (Compilation_Failed)
		return 1;

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
		auto const path = fs::absolute(compiler_arguments.executable);
		execl(path.c_str(), compiler_arguments.executable.c_str(), (char*)NULL);
	}
}
