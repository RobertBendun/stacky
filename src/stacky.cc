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

struct Arguments
{
	std::vector<fs::path> include_search_paths;
	std::vector<std::string> source_files;
	fs::path compiler;
	fs::path executable;
	fs::path assembly;

	bool warn_redefinitions = true;
	bool verbose = false;
	bool typecheck = false;

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
	Bool,

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
	std::tuple { "&fun"sv,      Keyword_Kind::Function },
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
	std::tuple { "false"sv,     Keyword_Kind::Bool },
	std::tuple { "fun"sv,       Keyword_Kind::Function },
	std::tuple { "if"sv,        Keyword_Kind::If },
	std::tuple { "import"sv,    Keyword_Kind::Import },
	std::tuple { "include"sv,   Keyword_Kind::Include },
	std::tuple { "return"sv,    Keyword_Kind::Return },
	std::tuple { "true"sv,      Keyword_Kind::Bool },
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

struct Type
{
	enum class Kind
	{
		Int,
		Bool,
		Pointer,
	};

	Kind kind;
	struct Operation const* op = nullptr;
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

	Type::Kind type;
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

#include "errors.cc"
#include "unicode.cc"
#include "arguments.cc"
#include "lexer.cc"
#include "parser.cc"
#include "linux-x86_64.cc"
#include "optimizer.cc"


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


using Typestack = std::vector<Type>;

constexpr auto type_name(Type const& type) -> std::string_view
{
	switch (type.kind) {
	case Type::Kind::Int: return "u64";
	case Type::Kind::Bool: return "bool";
	case Type::Kind::Pointer: return "ptr";
	}
	return {};
}

void print_typestack_trace(Typestack& typestack, std::string_view verb="unhandled")
{
	for (auto i = typestack.size() - 1u; i < typestack.size(); --i) {
		auto const& t = typestack[i];
		info(t.op->token, "{} value of type `{}` was defined here"_format(verb, type_name(t)));
	}
}

void print_typestack_trace(auto begin, auto end, std::string_view verb="unhandled")
{
	for (end--; end != begin-1; --end) {
		auto const& t = *end;
		info(t.op->token, "{} value of type `{}` was defined here"_format(verb, type_name(t)));
	}
}

void ensure_enough_arguments(Typestack& typestack, Operation const& op, unsigned argc)
{
	if (typestack.size() < argc)
		error_fatal("`{}` requires {} arguments on stack"_format(op.token.sval, argc));
}

void unexpected_type(Operation const& op, Type const& expected, Type const& found)
{
	error_fatal(op.token, "expected type `{}` but found `{}` for `{}`"_format(type_name(expected), type_name(found), op.token.sval));
}

void unexpected_type(Operation const& op, Type::Kind exp1, Type::Kind exp2, Type const& found)
{
	error_fatal(op.token, "expected type `{}` or `{}` but found `{}` for `{}`"_format(type_name({ exp1 }), type_name({ exp2 }), type_name(found), op.token.sval));
}

void typecheck(std::vector<Operation> &ops)
{
	std::vector<std::tuple<Typestack, Operation::Kind>> blocks;
	Typestack typestack;

	auto const pop = [&typestack]() -> Type { auto retval = std::move(typestack.back()); typestack.pop_back(); return retval; };
	auto const push = [&typestack](Type::Kind kind, Operation const& op) { typestack.push_back({ kind, &op }); };
	auto const top = [&typestack](unsigned offset = 0) -> Type& {	return typestack[typestack.size() - offset - 1]; };

	for (auto const& op : ops) {
		switch (op.kind) {
		case Operation::Kind::Push_Symbol:
			typestack.push_back({ Type::Kind::Pointer, &op });
			break;

		case Operation::Kind::Push_Int:
			typestack.push_back({ op.type, &op });
			break;

		case Operation::Kind::Intrinsic:
			switch (op.intrinsic) {
			case Intrinsic_Kind::Add:
				{
					ensure_enough_arguments(typestack, op, 2);
					auto const& rhs = pop();
					auto const& lhs = pop();

					if (lhs.kind == rhs.kind) {
						if (lhs.kind != Type::Kind::Int) unexpected_type(op, { Type::Kind::Int }, lhs);
						if (rhs.kind != Type::Kind::Int) unexpected_type(op, { Type::Kind::Int }, rhs);
						push(lhs.kind, op);
					} else if (lhs.kind == Type::Kind::Int) {
						if (rhs.kind != Type::Kind::Pointer) unexpected_type(op, Type::Kind::Int, Type::Kind::Pointer, rhs);
						push(Type::Kind::Pointer, op);
					} else if (lhs.kind == Type::Kind::Pointer) {
						if (rhs.kind != Type::Kind::Int) unexpected_type(op, { Type::Kind::Int }, rhs);
						push(Type::Kind::Pointer, op);
					} else {
						unexpected_type(op, Type::Kind::Int, Type::Kind::Pointer, lhs);
					}
				}
				break;

			case Intrinsic_Kind::Subtract:
				{
					ensure_enough_arguments(typestack, op, 2);
					auto const& rhs = pop();
					auto const& lhs = pop();

					if (lhs.kind == rhs.kind) {
						if (lhs.kind != Type::Kind::Int && lhs.kind != Type::Kind::Pointer)
							unexpected_type(op, Type::Kind::Int, Type::Kind::Pointer, lhs);
						push(lhs.kind, op);
					} else if (lhs.kind == Type::Kind::Pointer) {
						if (rhs.kind != Type::Kind::Int) unexpected_type(op, { Type::Kind::Int }, rhs);
						push(Type::Kind::Pointer, op);
					} else if (rhs.kind == Type::Kind::Pointer) {
						unexpected_type(op, { Type::Kind::Int }, rhs);
					} else {
						unexpected_type(op, Type::Kind::Int, Type::Kind::Pointer, lhs);
					}
				}
				break;

			case Intrinsic_Kind::Equal:
			case Intrinsic_Kind::Not_Equal:
				{
					ensure_enough_arguments(typestack, op, 2);
					auto const& rhs = pop();
					auto const& lhs = pop();
					if (lhs.kind != rhs.kind) {
						unexpected_type(op, lhs, rhs);
					}
					push(Type::Kind::Bool, op);
				}
				break;

			case Intrinsic_Kind::Boolean_And:
			case Intrinsic_Kind::Boolean_Or:
				{
					ensure_enough_arguments(typestack, op, 2);
					auto const& rhs = pop();
					auto const& lhs = pop();
					if (lhs.kind != Type::Kind::Bool) unexpected_type(op, { Type::Kind::Bool }, lhs);
					if (rhs.kind != Type::Kind::Bool) unexpected_type(op, { Type::Kind::Bool }, rhs);
					push(Type::Kind::Bool, op);
				}
				break;

			case Intrinsic_Kind::Boolean_Negate:
				{
					ensure_enough_arguments(typestack, op, 1);
					if (top().kind != Type::Kind::Bool) unexpected_type(op, { Type::Kind::Bool }, top());
					top().op = &op;
				}
				break;

			case Intrinsic_Kind::Greater:
			case Intrinsic_Kind::Greater_Eq:
			case Intrinsic_Kind::Less:
			case Intrinsic_Kind::Less_Eq:
			case Intrinsic_Kind::Mul:
			case Intrinsic_Kind::Mod:
			case Intrinsic_Kind::Div:
			case Intrinsic_Kind::Min:
			case Intrinsic_Kind::Max:
			case Intrinsic_Kind::Bitwise_And:
			case Intrinsic_Kind::Bitwise_Or:
			case Intrinsic_Kind::Bitwise_Xor:
			case Intrinsic_Kind::Left_Shift:
			case Intrinsic_Kind::Right_Shift:
				{
					ensure_enough_arguments(typestack, op, 2);
					auto const& rhs = pop();
					auto const& lhs = pop();
					if (lhs.kind != rhs.kind || lhs.kind != Type::Kind::Int) {
						unexpected_type(op, lhs, rhs);
					}
					push(Type::Kind::Int, op);
				}
				break;

			case Intrinsic_Kind::Div_Mod:
				{
					ensure_enough_arguments(typestack, op, 2);
					auto const& rhs = top();
					auto const& lhs = top(1);
					if (lhs.kind != Type::Kind::Int) unexpected_type(op, { Type::Kind::Int }, lhs);
					if (rhs.kind != Type::Kind::Int) unexpected_type(op, { Type::Kind::Int }, rhs);
					top(1).op = top().op = &op;
				}
				break;

			case Intrinsic_Kind::Drop:
				ensure_enough_arguments(typestack, op, 1);
				typestack.pop_back();
				break;

			case Intrinsic_Kind::Dup:
				ensure_enough_arguments(typestack, op, 1);
				push(top().kind, op);
				break;

			case Intrinsic_Kind::Swap:
				ensure_enough_arguments(typestack, op, 2);
				std::swap(top(), top(1));
				break;

			case Intrinsic_Kind::Over:
				ensure_enough_arguments(typestack, op, 2);
				push(top(1).kind, op);
				break;

			case Intrinsic_Kind::Rot:
				ensure_enough_arguments(typestack, op, 3);
				std::swap(top(), top(2));
				std::swap(top(1), top(2));
				break;

			case Intrinsic_Kind::Tuck:
				ensure_enough_arguments(typestack, op, 2);
				push(top(0).kind, op);
				std::swap(top(1), top(2));
				break;

			case Intrinsic_Kind::Two_Dup:
				ensure_enough_arguments(typestack, op, 2);
				push(top(1).kind, op);
				push(top(1).kind, op);
				break;

			case Intrinsic_Kind::Two_Drop:
				ensure_enough_arguments(typestack, op, 2);
				pop();
				pop();
				break;

			case Intrinsic_Kind::Two_Over:
				ensure_enough_arguments(typestack, op, 4);
				push(top(3).kind, op);
				push(top(3).kind, op);
				break;

			case Intrinsic_Kind::Two_Swap:
				ensure_enough_arguments(typestack, op, 4);
				std::swap(top(3), top(1));
				std::swap(top(2), top());
				break;

			case Intrinsic_Kind::Load:
				ensure_enough_arguments(typestack, op, 1);
				if (auto t = pop(); t.kind != Type::Kind::Pointer) unexpected_type(op, { Type::Kind::Pointer }, t);
				push(Type::Kind::Int, op);
				break;

			case Intrinsic_Kind::Store:
				{
					ensure_enough_arguments(typestack, op, 2);
					auto const addr = pop();
					auto const val = pop();
					if (addr.kind != Type::Kind::Pointer) unexpected_type(op, { Type::Kind::Pointer }, addr);
					if (val.kind != Type::Kind::Int) unexpected_type(op, { Type::Kind::Int }, addr);
				}
				break;

			case Intrinsic_Kind::Top:
				push(Type::Kind::Pointer, op);
				break;

			case Intrinsic_Kind::Call:
				error(op.token, "`call` is not supported by typechecking");
				break;

			case Intrinsic_Kind::Syscall:
				{
					assert(op.token.sval[7] >= '0' && op.token.sval[7] <= '6');
					unsigned syscall_count = op.token.sval[7] - '0';
					ensure_enough_arguments(typestack, op, syscall_count + 1);
					if (auto t = pop(); t.kind != Type::Kind::Int) unexpected_type(op, { Type::Kind::Int }, t);
					for (; syscall_count > 0; --syscall_count) pop();
					push(Type::Kind::Int, op);
				}
				break;

			case Intrinsic_Kind::Random32:
			case Intrinsic_Kind::Random64:
				push(Type::Kind::Int, op);
				break;
			}
			break;

		case Operation::Kind::If:
			ensure_enough_arguments(typestack, op, 1);
			if (top().kind != Type::Kind::Bool) unexpected_type(op, { Type::Kind::Bool }, top());
			pop();
			blocks.push_back({ typestack, Operation::Kind::If });
			break;

		case Operation::Kind::Else:
			{
				// move onto blocks then branch result stack
				// reset typestack into pre-if form
				auto [before_if, if_kind] = std::move(blocks.back());
				blocks.pop_back();
				assert(if_kind == Operation::Kind::If);
				blocks.push_back({ typestack, Operation::Kind::Else });
				typestack = std::move(before_if);
			}
			break;


		case Operation::Kind::End:
			{
				assert(!blocks.empty());
				auto const& [types, opened] = blocks.back();

				switch (opened) {
				case Operation::Kind::If:
					if (auto [e, a] = std::mismatch(std::cbegin(types), std::cend(types), std::cbegin(typestack), std::cend(typestack), [](auto const &expected, auto const& actual) {
								return expected.kind == actual.kind; }); e != std::cend(types) || a != std::cend(typestack)) {
						error(op.token, "`if` without `else` should have the same type stack shape before and after execution");

						if (types.size() != typestack.size()) {
							if (e == std::cend(types)) {
								info("there are {} excess values on the stack"_format(std::distance(a, std::cend(typestack))));
								print_typestack_trace(a, std::cend(typestack), "excess");
							} else {
								info("there are missing {} values on the stack"_format(std::distance(e, std::cend(types))));
								print_typestack_trace(e, std::cend(types), "missing");
							}
						} else {
							// stacks are equal in size, so difference is in types not their amount
							for (; e != std::cend(types) && a != std::cend(typestack); ++e, ++a) {
								if (e->kind == a->kind) continue;
								unexpected_type(op, *e, *a);
							}
						}
						exit(1);
					}
					break;

				case Operation::Kind::Else:
					if (auto [e, a] = std::mismatch(std::cbegin(types), std::cend(types), std::cbegin(typestack), std::cend(typestack), [](auto const &expected, auto const& actual) {
								return expected.kind == actual.kind; }); e != std::cend(types) || a != std::cend(typestack)) {
						error(op.token, "`if` ... `else` and `else` ... `end` branches must have matching typestacks");
						if (types.size() != typestack.size()) {
							if (e == std::cend(types)) {
								info("there are {} excess values in `else` branch"_format(std::distance(a, std::cend(typestack))));
								print_typestack_trace(a, std::cend(typestack), "excess");
							} else {
								info("there are missing {} values in `else` branch"_format(std::distance(e, std::cend(types))));
								print_typestack_trace(e, std::cend(types), "missing");
							}
						} else {
							// stacks are equal in size, so difference is in types not their amount
							for (; e != std::cend(types) && a != std::cend(typestack); ++e, ++a) {
								if (e->kind == a->kind) continue;
								unexpected_type(op, *e, *a);
							}
						}
						exit(1);
					}
					break;

				default:
					assert_msg(false, "unimplemented");
				}
			}
			break;

#if 1
		default:
			assert_msg(false, "unimplemented");
			;
#endif
		}
	}

	if (typestack.empty())
		return;

	error(ops.back().token, "{} values has been left on the stack"_format(typestack.size()));
	print_typestack_trace(typestack);
	exit(1);
}

auto main(int argc, char **argv) -> int
{
	parse_arguments(argc, argv);

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

	parser::transform_into_operations(tokens, geninfo.main, geninfo.words);
	if (Compilation_Failed)
		return 1;

	if (compiler_arguments.typecheck)
		typecheck(geninfo.main);

	optimizer::optimize(geninfo);
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
