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
	Typename,

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

	bool is_unsigned;
	unsigned byte_size;
};

static constexpr auto String_To_Keyword = sorted_array_of_tuples(
	std::tuple { "&fun"sv,      Keyword_Kind::Function },
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
		if (kind != other.kind) return false;
		switch (kind)
		{
		case Type::Kind::Int: return is_unsigned == other.is_unsigned && byte_size == other.byte_size;
		default:              return true;
		}
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
	bool is_unsigned = false;
	unsigned short byte_size = 0;
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

auto type_name(Type const& type) -> std::string
{
	switch (type.kind) {
	case Type::Kind::Bool: return "bool";
	case Type::Kind::Pointer: return "ptr";
	case Type::Kind::Int:
		{
			if (type.byte_size)
				return fmt::format("{}{}", type.is_unsigned ? 'u' : 'i', 8 * type.byte_size);
			return "u64";
		}
		break;
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

void unexpected_sign(Operation const& op, Type const& expected, Type const& found)
{
	error_fatal(op.token, "expected {} integer, found `{}`"_format(expected.is_unsigned ? "unsigned" : "signed", type_name(found)));
}

void typecheck(std::vector<Operation> &ops)
{
	std::vector<std::tuple<Typestack, Operation::Kind>> blocks;
	Typestack typestack;

	auto const pop = [&typestack]() -> Type { auto retval = std::move(typestack.back()); typestack.pop_back(); return retval; };
	auto const push = [&typestack](Type::Kind kind, Operation const& op) { typestack.push_back({ kind, &op }); };
	auto const top = [&typestack](unsigned offset = 0) -> Type& {	return typestack[typestack.size() - offset - 1]; };

	auto const int_binop = [&typestack](Operation const& op, Type lhs, Type const& rhs, bool emit_value = true)
	{
		if (lhs.kind != Type::Kind::Int) unexpected_type(op, { Type::Kind::Int }, lhs);
		if (rhs.kind != Type::Kind::Int) unexpected_type(op, { Type::Kind::Int }, rhs);

		if (lhs.is_unsigned != rhs.is_unsigned)
			unexpected_sign(op, lhs, rhs);

		if (emit_value) {
			lhs.byte_size = std::max(lhs.byte_size, rhs.byte_size);
			lhs.op = &op;
			typestack.push_back(lhs);
		}
	};

	for (auto const& op : ops) {
		switch (op.kind) {
		case Operation::Kind::Push_Symbol:
			typestack.push_back({ Type::Kind::Pointer, &op });
			break;

		case Operation::Kind::Push_Int:
			{
				auto type = op.type;
				type.op = &op;
				typestack.push_back(type);
			}
			break;

		case Operation::Kind::Cast:
			ensure_enough_arguments(typestack, op, 1);
			pop();
			push(op.type.kind, op);
			break;

		case Operation::Kind::Intrinsic:
			switch (op.intrinsic) {
			case Intrinsic_Kind::Add:
				{
					ensure_enough_arguments(typestack, op, 2);
					auto rhs = pop();
					auto lhs = pop();

					if (lhs.kind == rhs.kind) {
						int_binop(op, lhs, rhs);
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
						if (lhs.kind == Type::Kind::Int)
							int_binop(op, lhs, rhs);
						else if (lhs.kind == Type::Kind::Pointer)
							push(Type::Kind::Int, op);
						else
							unexpected_type(op, Type::Kind::Int, Type::Kind::Pointer, lhs);
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
					if (lhs.kind == Type::Kind::Int && lhs.is_unsigned != rhs.is_unsigned)
						unexpected_sign(op, lhs, rhs);
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
					auto rhs = pop();
					auto lhs = pop();
					int_binop(op, lhs, rhs);
				}
				break;

			case Intrinsic_Kind::Greater:
			case Intrinsic_Kind::Greater_Eq:
			case Intrinsic_Kind::Less:
			case Intrinsic_Kind::Less_Eq:
				{
					ensure_enough_arguments(typestack, op, 2);
					auto const& rhs = pop();
					auto const& lhs = pop();
					if (lhs.kind != rhs.kind || lhs.kind != Type::Kind::Int) {
						unexpected_type(op, lhs, rhs);
					}
					if (lhs.is_unsigned != rhs.is_unsigned)
						return unexpected_sign(op, lhs, rhs);
					push(Type::Kind::Bool, op);
				}
				break;

			case Intrinsic_Kind::Div_Mod:
				{
					ensure_enough_arguments(typestack, op, 2);
					auto &rhs = top();
					auto &lhs = top(1);
					int_binop(op, lhs, rhs, false);
					lhs.op = rhs.op = &op;
					lhs.byte_size = rhs.byte_size = std::max(lhs.byte_size, rhs.byte_size);
				}
				break;

			case Intrinsic_Kind::Drop:
				ensure_enough_arguments(typestack, op, 1);
				typestack.pop_back();
				break;

			case Intrinsic_Kind::Dup:
				ensure_enough_arguments(typestack, op, 1);
				typestack.push_back(top().with_op(&op));
				break;

			case Intrinsic_Kind::Swap:
				ensure_enough_arguments(typestack, op, 2);
				std::swap(top(), top(1));
				break;

			case Intrinsic_Kind::Over:
				ensure_enough_arguments(typestack, op, 2);
				typestack.push_back(top(1).with_op(&op));
				break;

			case Intrinsic_Kind::Rot:
				ensure_enough_arguments(typestack, op, 3);
				std::swap(top(), top(2));
				std::swap(top(1), top(2));
				break;

			case Intrinsic_Kind::Tuck:
				ensure_enough_arguments(typestack, op, 2);
				typestack.push_back(top().with_op(&op));
				std::swap(top(1), top(2));
				break;

			case Intrinsic_Kind::Two_Dup:
				ensure_enough_arguments(typestack, op, 2);
				typestack.push_back(top(1).with_op(&op));
				typestack.push_back(top(1).with_op(&op));
				break;

			case Intrinsic_Kind::Two_Drop:
				ensure_enough_arguments(typestack, op, 2);
				pop();
				pop();
				break;

			case Intrinsic_Kind::Two_Over:
				ensure_enough_arguments(typestack, op, 4);
				typestack.push_back(top(3).with_op(&op));
				typestack.push_back(top(3).with_op(&op));
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
				error_fatal(op.token, "`call` is not supported by typechecking");
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
				{
					Type t;
					t.kind = Type::Kind::Int;
					t.op = &op;
					t.is_unsigned = true;
					t.byte_size = op.intrinsic == Intrinsic_Kind::Random32 ? 4 : 8;
					typestack.push_back(t);
				}
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

		case Operation::Kind::While:
			blocks.push_back({ typestack, Operation::Kind::While });
			break;

		case Operation::Kind::Do:
			ensure_enough_arguments(typestack, op, 1);
			if (top().kind != Type::Kind::Bool) unexpected_type(op, { Type::Kind::Bool }, top());
			pop();
			blocks.push_back({ typestack, Operation::Kind::Do });
			break;

		case Operation::Kind::End:
			{
				assert(!blocks.empty());
				auto [types, opened] = std::move(blocks.back());
				blocks.pop_back();

				switch (opened) {
				case Operation::Kind::If:
					if (auto [e, a] = std::mismatch(std::cbegin(types), std::cend(types), std::cbegin(typestack), std::cend(typestack)); e != std::cend(types) || a != std::cend(typestack)) {
						error(op.token, "`if` without `else` should have the same type stack shape before and after execution");

						if (types.size() != typestack.size()) {
							if (e == std::cend(types)) {
								info(op.token, "there are {} excess values on the stack"_format(std::distance(a, std::cend(typestack))));
								print_typestack_trace(a, std::cend(typestack), "excess");
							} else {
								info(op.token, "there are missing {} values on the stack"_format(std::distance(e, std::cend(types))));
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
					if (auto [e, a] = std::mismatch(std::cbegin(types), std::cend(types), std::cbegin(typestack), std::cend(typestack)); e != std::cend(types) || a != std::cend(typestack)) {
						error(op.token, "`if` ... `else` and `else` ... `end` branches must have matching typestacks");
						if (types.size() != typestack.size()) {
							if (e == std::cend(types)) {
								info(op.token, "there are {} excess values in `else` branch"_format(std::distance(a, std::cend(typestack))));
								print_typestack_trace(a, std::cend(typestack), "excess");
							} else {
								info(op.token, "there are missing {} values in `else` branch"_format(std::distance(e, std::cend(types))));
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

				case Operation::Kind::While:
					assert_msg(false, "unreachable");
					break;

				case Operation::Kind::Do:
					{
						auto [while_types, opened] = std::move(blocks.back());
						blocks.pop_back();
						assert(opened == Operation::Kind::While);

						if (auto [e, a] = std::mismatch(std::cbegin(while_types), std::cend(while_types), std::cbegin(typestack), std::cend(typestack)); e != std::cend(while_types) || a != std::cend(typestack)) {
							error(op.token, "`while ... do` should have the same type stack shape before and after execution");
							if (while_types.size() != typestack.size()) {
								if (e == std::cend(while_types)) {
									info(op.token, "there are {} excess values in loop body"_format(std::distance(a, std::cend(typestack))));
									print_typestack_trace(a, std::cend(typestack), "excess");
								} else {
									info(op.token, "there are missing {} values in loop body"_format(std::distance(e, std::cend(while_types))));
									print_typestack_trace(e, std::cend(while_types), "missing");
								}
							} else {
								// stacks are equal in size, so difference is in types not their amount
								for (; e != std::cend(while_types) && a != std::cend(typestack); ++e, ++a) {
									if (e->kind == a->kind) continue;
									unexpected_type(op, *e, *a);
								}
							}
							exit(1);
						}
					}
					break;

				case Operation::Kind::Call_Symbol:
				case Operation::Kind::Cast:
				case Operation::Kind::End:
				case Operation::Kind::Intrinsic:
				case Operation::Kind::Push_Int:
				case Operation::Kind::Push_Symbol:
				case Operation::Kind::Return:
					assert_msg(false, "unreachable");
				}
			}
			break;


		case Operation::Kind::Call_Symbol:
		case Operation::Kind::Return:
			assert_msg(false, "unimplemented");
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
