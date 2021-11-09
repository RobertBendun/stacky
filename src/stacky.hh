#pragma once

#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

#define  Label_Prefix               "_Stacky_instr_"
#define  Symbol_Prefix              "_Stacky_symbol_"
#define  String_Prefix              "_Stacky_string_"
#define  Function_Prefix            "_Stacky_fun_"
#define  Function_Body_Prefix       "_Stacky_funinstr_"
#define  Anonymous_Function_Prefix  "_Stacky_anonymous_"

#include "errors.hh"
#include "arguments.hh"

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
	Load,
	Store,
	Top,
	Call,

	// --- STDLIB, OS ---
	Syscall,

	Last = Syscall,
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

	struct Location
	{
		std::string_view file;
		unsigned column;
		unsigned line;
	};

	Location location;
	Kind kind;

	std::string sval;
	uint64_t ival = -1;
	Keyword_Kind kval;

	unsigned byte_size;
};

struct Type
{
	enum class Kind
	{
		Int,
		Bool,
		Pointer,
		Any,

		Count = Any
	};

	auto& operator=(Type::Kind k) { kind = k; return *this; }

	auto operator==(Type const& other) const
	{
		return (kind == Kind::Any || other.kind == Kind::Any) || kind == other.kind;
	}

	auto operator!=(Type const& other) const { return !this->operator==(other); }

	Kind kind;
	static Type from(Token const& token);
};

using Typestack = std::vector<Type>;
using Typestack_View = std::span<Type const>;

struct Stack_Effect
{
	Typestack input;
	Typestack output;

	inline auto& operator[](bool is_input) { return is_input ? input : output; }

	auto string() const -> std::string;
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

// Unicode support
namespace utf8
{
	std::string encode_rune(uint32_t r);
}

// Lexer
bool lex(std::string_view const file, std::string_view const path, std::vector<Token> &tokens);

// Parser
namespace parser
{
	auto extract_include_or_import(std::vector<Token> &tokens) -> std::optional<std::tuple<Keyword_Kind, fs::path, fs::path, unsigned>>;

	void extract_strings(std::vector<Token> &tokens, std::unordered_map<std::string, unsigned> &strings);
	void register_definitions(std::vector<Token> &tokens, Words &words);
	void into_operations(std::span<Token> const& tokens, std::vector<Operation> &body, Words &words);
}

// Type checking
void typecheck(std::vector<Operation> const& ops);
void typecheck(Word const& word);

// Optimization
namespace optimizer
{
	void optimize(Generation_Info &geninfo);
}

// Platform dependent code generation
namespace linux::x86_64
{
	void generate_assembly(Generation_Info &geninfo, fs::path const& asm_path);
}

// Compiler data visualisation & debugging
void generate_control_flow_graph(Generation_Info const& geninfo, fs::path dot_path, std::string const& function);
