#include "stacky.hh"
#include <algorithm>
#include <numeric>

Type Type::from(Token const& token)
{
	assert(token.kind == Token::Kind::Keyword && token.kval == Keyword_Kind::Typename);
	switch (token.sval[0]) {
	case 'b': return { Type::Kind::Bool };
	case 'p': return { Type::Kind::Pointer };
	case 'u': return { Type::Kind::Int };
	}

	unreachable("unparsable type definition (bug in lexer probably)");
}

auto type_name(Type const& type) -> std::string
{
	switch (type.kind) {
	case Type::Kind::Bool: return "bool";
	case Type::Kind::Pointer: return "ptr";
	case Type::Kind::Int: return "u64";
	case Type::Kind::Any: return "any";
	}
	unreachable("we don't have more type kinds");
}

template <> struct fmt::formatter<Type> : fmt::formatter<std::string> {
  // parse is inherited from formatter<string_view>.
  template <typename FormatContext>
  auto format(Type const& t, FormatContext& ctx) {
    return formatter<std::string>::format(type_name(t), ctx);
  }
};


auto stack_effect_string(auto const& stack_effect)
{
	return "{} -- {}"_format(fmt::join(stack_effect.input, " "), fmt::join(stack_effect.output, " "));
}

auto Stack_Effect::string() const -> std::string
{
	return stack_effect_string(*this);
}


void typecheck(std::vector<Operation> const& ops, Typestack &&typestack, Typestack const& expected);

void typecheck(Word const& word)
{
	auto copy = word.effect.input;

	typecheck(word.function_body, std::move(copy), word.effect.output);
}

void typecheck(std::vector<Operation> const& ops)
{
	typecheck(ops, {}, {});
}

struct State
{
	Typestack      stack;
	Typestack_View output;
	unsigned       ip = 0;
};

void verify_output(State const& s)
{
	while (!s.stack.empty() && !s.output.empty()) {
		unreachable("unimplemented");
	}

	if (s.stack.empty() && s.output.empty())
		return;

	if (s.output.empty()) {
		error("Excess data on stack");
		for (auto it = s.stack.rbegin(); it != s.stack.rend(); ++it) {
			info("value of type `{}`"_format(type_name(*it)));
		}
		exit(1);
	}

	unreachable("unimplemented");
}


void typecheck_stack_effects(State& state, auto const& effects)
{
	struct Error
	{
		unsigned effect_id;
		enum { Missing, Different_Types } kind;
		Type effect;
		Type state = {};
	};

	std::vector<int> matching;
	matching.reserve(effects.size());
	std::vector<Error> deffered_errors;

	for (unsigned effect_id = 0; effect_id < effects.size(); ++effect_id) {
		auto const& effect = effects[effect_id];

		auto const send = state.stack.rend();
		auto const ebeg = effect.input.rbegin();
		auto const eend = effect.input.rend();
		auto [s, e] = std::mismatch(state.stack.rbegin(), send, ebeg, eend);

		auto match = std::distance(ebeg, e);

		if (e == eend) {
			state.stack.erase((state.stack.rbegin() + effect.input.size()).base(), state.stack.end());
			state.stack.insert(state.stack.end(), effect.output.begin(), effect.output.end());
			return;
		}

		if (s == send) {
		missing:
			for (; e != eend; ++e) deffered_errors.push_back({ effect_id, Error::Missing, *e });
			matching.push_back(match);
			continue;
		}

		for (; e != eend && s != send; ++s, ++e) {
			if (*e == *s) { ++match; continue; }
			deffered_errors.push_back({ effect_id, Error::Different_Types, *e, *s });
		}

		if (s == send && e != eend) {
			goto missing;
		} else {
			matching.push_back(match);
		}
	}


	auto best_match_score = *std::max_element(matching.begin(), matching.end());

	// TODO operation name
	error("Invalid stack state of operation");
	unsigned last_effect_id = -1;
	for (auto const& err : deffered_errors) {
		if (matching[err.effect_id] != best_match_score)
			continue;

		if (effects.size() != 1 && last_effect_id != err.effect_id) {
			info("error trying to match: {}"_format(stack_effect_string(effects[err.effect_id])));
			last_effect_id = err.effect_id;
		}

		switch (err.kind) {
		case Error::Missing:
			info("missing value of type `{}`"_format(type_name(err.effect)));
			break;

		case Error::Different_Types:
			info("expected value of type `{}`. Found `{}`"_format(type_name(err.effect), type_name(err.state)));
			break;
		}
	}

	exit(1);
}

namespace Type_DSL
{
	static constexpr auto Empty = std::array<Type, 0>{};
#define Alias(V, K) static constexpr auto V = std::array { Type { Type::Kind::K } }
	Alias(Any,   Any);
	Alias(Bool,  Bool);
	Alias(Int,   Int);
	Alias(Ptr,   Pointer);
#undef Alias
	static_assert(4 == (int)Type::Kind::Count+1, "All types are handled in Type_DSL");

	template<auto N, auto M>
	constexpr auto operator>>(std::array<Type, N> const& lhs, std::array<Type, M> const& rhs)
	{
		return [&]<std::size_t ...I, std::size_t ...J>(std::index_sequence<I...>, std::index_sequence<J...>) {
			return std::array { lhs[I]..., rhs[J]... };
		}(std::make_index_sequence<N>{}, std::make_index_sequence<M>{});
	}

	template<auto N, auto M>
	constexpr auto operator>=(std::array<Type, N> const& lhs, std::array<Type, M> const& rhs)
	{
		struct Stack_Effect
		{
			std::array<Type, N> input;
			std::array<Type, M> output;
		} stack_effect = { lhs, rhs };
		return stack_effect;
	}

	struct Stack_Effect_View
	{
		std::span<Type const> input;
		std::span<Type const> output;
	};

	constexpr auto view(auto const& effects)
	{
		auto const convert = [&](auto const& ...effects) {

			return std::array {
				Stack_Effect_View {
					effects.input,
					effects.output
				} ...
			};
		};


		return std::apply(convert, effects);
	}
}

#define Typecheck_Stack_Effect(s, ...) \
	do { \
		static constexpr auto SE = std::tuple { __VA_ARGS__ }; \
		typecheck_stack_effects(s, view(SE)); \
		++s.ip; \
	} while(0)

void typecheck(std::vector<Operation> const& ops, Typestack &&initial_typestack, Typestack const& expected)
{
	using namespace Type_DSL;

	std::vector<State> states = { State { std::move(initial_typestack), std::span(expected), 0 } };

	while (!states.empty()) {
		auto& s = states.back();

		if (s.ip >= ops.size()) {
			verify_output(s);
			states.pop_back();
			continue;
		}

		switch (auto const &op = ops[s.ip]; op.kind) {
		case Operation::Kind::Push_Int:
			s.stack.push_back(op.type);
			++s.ip;
			break;

		case Operation::Kind::Push_Symbol:
			s.stack.push_back({ Type::Kind::Pointer });
			++s.ip;
			break;

		case Operation::Kind::Cast:
			{
				static constinit auto SE = std::tuple { Any >= Any };
				std::get<0>(SE).output[0] = op.type;
				typecheck_stack_effects(s, view(SE));
				++s.ip;
			}
			break;

		case Operation::Kind::Intrinsic:
			{
				switch (op.intrinsic) {
				case Intrinsic_Kind::Drop:
					Typecheck_Stack_Effect(s, Any >= Empty);
					break;

				case Intrinsic_Kind::Add:
					Typecheck_Stack_Effect(s, Ptr >> Int >= Ptr, Int >> Ptr >= Ptr, Int >> Int >= Int);
					break;

				case Intrinsic_Kind::Subtract:
					Typecheck_Stack_Effect(s, Ptr >> Ptr >= Int, Ptr >> Int >= Ptr, Int >> Int >= Int);
					break;

				case Intrinsic_Kind::Less:
				case Intrinsic_Kind::Less_Eq:
				case Intrinsic_Kind::Greater:
				case Intrinsic_Kind::Greater_Eq:
				case Intrinsic_Kind::Equal:
				case Intrinsic_Kind::Not_Equal:
					Typecheck_Stack_Effect(s, Ptr >> Ptr >= Bool, Int >> Int >= Bool, Bool >> Bool >= Bool);
					break;

				case Intrinsic_Kind::Boolean_And:
				case Intrinsic_Kind::Boolean_Or:
					Typecheck_Stack_Effect(s, Bool >> Bool >= Bool);
					break;

				default:
					unreachable("unimplemented");
				}
			}
			break;

		default:
			unreachable("unimplemented");
		}
	}
}
