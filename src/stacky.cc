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

#define Label_Prefix "_Stacky_instr_"
#define Symbol_Prefix "_Stacky_symbol_"
#define String_Prefix "_Stacky_string_"
#define Function_Prefix "_Stacky_fun_"
#define Function_Body_Prefix "_Stacky_funinstr_"
#define Anonymous_Function_Prefix "_Stacky_anonymous_"

#include "arguments.hh"

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
	Bool,

	// Type definitions
	Typename,
	Stack_Effect_Definition,
	Stack_Effect_Divider,

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

	unsigned byte_size;
};

static constexpr auto String_To_Keyword = sorted_array_of_tuples(
	std::tuple { "&fun"sv,      Keyword_Kind::Function },
	std::tuple { "--"sv,        Keyword_Kind::Stack_Effect_Divider },
	std::tuple { "is"sv,        Keyword_Kind::Stack_Effect_Definition },
	std::tuple { "[]byte"sv,    Keyword_Kind::Array },
	std::tuple { "[]u16"sv,     Keyword_Kind::Array },
	std::tuple { "[]u32"sv,     Keyword_Kind::Array },
	std::tuple { "[]u64"sv,     Keyword_Kind::Array },
	std::tuple { "[]u8"sv,      Keyword_Kind::Array },
	std::tuple { "[]usize"sv,   Keyword_Kind::Array },
	std::tuple { "bool"sv,      Keyword_Kind::Typename },
	std::tuple { "constant"sv,  Keyword_Kind::Constant },
	std::tuple { "do"sv,        Keyword_Kind::Do },
	std::tuple { "else"sv,      Keyword_Kind::Else },
	std::tuple { "end"sv,       Keyword_Kind::End },
	std::tuple { "false"sv,     Keyword_Kind::Bool },
	std::tuple { "fun"sv,       Keyword_Kind::Function },
	std::tuple { "i16"sv,       Keyword_Kind::Typename },
	std::tuple { "i32"sv,       Keyword_Kind::Typename },
	std::tuple { "i64"sv,       Keyword_Kind::Typename },
	std::tuple { "i8"sv,        Keyword_Kind::Typename },
	std::tuple { "if"sv,        Keyword_Kind::If },
	std::tuple { "import"sv,    Keyword_Kind::Import },
	std::tuple { "include"sv,   Keyword_Kind::Include },
	std::tuple { "ptr"sv,       Keyword_Kind::Typename },
	std::tuple { "return"sv,    Keyword_Kind::Return },
	std::tuple { "true"sv,      Keyword_Kind::Bool },
	std::tuple { "u16"sv,       Keyword_Kind::Typename },
	std::tuple { "u32"sv,       Keyword_Kind::Typename },
	std::tuple { "u64"sv,       Keyword_Kind::Typename },
	std::tuple { "u64"sv,       Keyword_Kind::Typename },
	std::tuple { "u8"sv,        Keyword_Kind::Typename },
	std::tuple { "while"sv,     Keyword_Kind::While }
);

// This value represents number of keywords inside enumeration
// Since one keyword kind may represents several symbols,
// we relay on number of kinds defined
static_assert(int(Keyword_Kind::Last)+1 == 15, "Exhaustive definition of keywords lookup");


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


struct Type
{
	enum class Kind
	{
		Int,
		Bool,
		Pointer,
	};

	auto& operator=(Type::Kind k) { kind = k; return *this; }

	auto operator==(Type const& other) const
	{
		return kind == other.kind;
	}

	auto operator!=(Type const& other) const { return !this->operator==(other); }

	auto with_op(struct Operation const* op) const
	{
		auto copy = *this;
		copy.op = op;
		return copy;
	}

	Kind kind;
	struct Operation const* op = nullptr;

	static Type from(Token const& token);
};

using Typestack = std::vector<Type>;

struct Stack_Effect
{
	Typestack input;
	Typestack output;

	auto& operator[](bool is_input) { return is_input ? input : output; }

	auto string() const -> std::string
	{
		return "{} -- {}"_format(fmt::join(input, " "), fmt::join(output, " "));
	}
};

struct Operation
{
	enum class Kind
	{
		Intrinsic,
		Push_Symbol,
		Push_Int,
		Call_Symbol,
		Cast,
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
	struct Word *word = nullptr;

	static constexpr unsigned Empty_Jump = -1;
	unsigned jump = Empty_Jump;

	std::string_view symbol_prefix;

	Type type;
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

	bool has_effect = false;
	Stack_Effect effect;
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

#include "errors.cc"
#include "unicode.cc"
#include "lexer.cc"
#include "parser.cc"
#include "linux-x86_64.cc"
#include "optimizer.cc"
#include "debug.cc"
#include "types.cc"


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
		auto const path = fs::absolute(compiler_arguments.executable);
		execl(path.c_str(), compiler_arguments.executable.c_str(), (char*)NULL);
	}
}
