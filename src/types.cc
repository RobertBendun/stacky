#include "stacky.hh"
#include <algorithm>
#include <numeric>
#include <concepts>
#include <ranges>

Type Type::from(Token const& token)
{
	assert(token.kind == Token::Kind::Keyword && token.kval == Keyword_Kind::Typename);
	switch (token.sval[0]) {
	case 'b': return { Type::Kind::Bool };
	case 'p': return { Type::Kind::Pointer };
	case 'u': return { Type::Kind::Int };
	case 'a': return { Type::Kind::Any };
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
	case Type::Kind::Variable: return "$" + std::to_string(type.var);
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


void typecheck(Generation_Info &geninfo, std::vector<Operation> const& ops, Typestack &&typestack, Typestack const& expected);

void typecheck(Generation_Info &geninfo, Word const& word)
{
	auto copy = word.effect.input;
	typecheck(geninfo, word.function_body, std::move(copy), word.effect.output);
}

void typecheck(Generation_Info &geninfo, std::vector<Operation> const& ops)
{
	typecheck(geninfo, ops, {}, {});
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

template<typename Eff>
concept Effect = requires (Eff effect)
{
	{ effect.input } -> std::ranges::random_access_range;
	{ effect.output } -> std::ranges::random_access_range;

	requires std::is_same_v<std::ranges::range_value_t<decltype(effect.input)>, Type>;
	requires std::is_same_v<std::ranges::range_value_t<decltype(effect.output)>, Type>;
};

template<typename Effs>
concept Effects = requires (Effs effects, unsigned i)
{
	{ effects[i] } -> Effect;
	{ effects.size() } -> std::convertible_to<unsigned>;
};


void typecheck_stack_effects(State& state, Effects auto const& effects)
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
		Effect auto const& effect = effects[effect_id];
		std::unordered_map<decltype(Type::var), Type> generics;

		auto const compare = [&](Type const& stack, Type const& effect) {
			if (effect.kind == Type::Kind::Variable) {
				if (generics.contains(effect.var))
					return generics.at(effect.var) == stack;
				generics.insert({ effect.var, stack });
				return true;
			}
			return stack == effect;
		};

		auto const send = state.stack.rend();
		auto const ebeg = effect.input.rbegin();
		auto const eend = effect.input.rend();
		auto [s, e] = std::mismatch(state.stack.rbegin(), send, ebeg, eend, compare);

		auto match = std::distance(ebeg, e);

		if (e == eend) {
			state.stack.erase((state.stack.rbegin() + effect.input.size()).base(), state.stack.end());
			std::transform(effect.output.begin(), effect.output.end(), std::back_inserter(state.stack), [&](Type const& type) {
				if (type.kind != Type::Kind::Variable)
					return type;
				ensure_fatal(generics.contains(type.var), "Couldn't deduce type variable");
				return generics[type.var];
			});
			return;
		}

		if (s == send) {
		missing:
			for (; e != eend; ++e) deffered_errors.push_back({ effect_id, Error::Missing, *e });
			matching.push_back(match);
			continue;
		}

		for (; e != eend && s != send; ++s, ++e) {
			if (compare(*e, *s)) { ++match; continue; }
			deffered_errors.push_back({ effect_id, Error::Different_Types, *e, *s });
		}

		if (s == send && e != eend) {
			goto missing;
		} else {
			matching.push_back(match);
		}
	}


	auto const best_match_score = *std::max_element(matching.begin(), matching.end());

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
	static_assert(5 == (int)Type::Kind::Count+1, "All types are handled in Type_DSL");

	static constexpr auto _1 = std::array<Type, 1>{ Type { Type::Kind::Variable, 1 } };
	static constexpr auto _2 = std::array<Type, 1>{ Type { Type::Kind::Variable, 2 } };
	static constexpr auto _3 = std::array<Type, 1>{ Type { Type::Kind::Variable, 3 } };
	static constexpr auto _4 = std::array<Type, 1>{ Type { Type::Kind::Variable, 4 } };
	static constexpr auto _5 = std::array<Type, 1>{ Type { Type::Kind::Variable, 5 } };
	static constexpr auto _6 = std::array<Type, 1>{ Type { Type::Kind::Variable, 6 } };
	static constexpr auto _7 = std::array<Type, 1>{ Type { Type::Kind::Variable, 7 } };
	static constexpr auto _8 = std::array<Type, 1>{ Type { Type::Kind::Variable, 8 } };
	static constexpr auto _9 = std::array<Type, 1>{ Type { Type::Kind::Variable, 9 } };

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

void typecheck([[maybe_unused]] Generation_Info &geninfo, std::vector<Operation> const& ops, Typestack &&initial_typestack, Typestack const& expected)
{
	using namespace Type_DSL;

	std::unordered_map<decltype(State::ip), State> visited_do_ops;
	std::vector<State> states = { State { std::move(initial_typestack), std::span(expected), 0 } };

	while (!states.empty()) {
		auto& s = states.back();

		if (s.ip >= ops.size()) {
		// It's the same for `return` operation and end of ops
		ops_end:
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

		case Operation::Kind::If:
			{
				Typecheck_Stack_Effect(s, Bool >= Empty);
				assert(op.jump != Operation::Empty_Jump);
				states.push_back(State { s.stack, s.output, op.jump });
			}
			break;

		case Operation::Kind::Else:
			assert(op.jump != Operation::Empty_Jump);
			s.ip = op.jump;
			break;

		case Operation::Kind::End:
			assert(op.jump != Operation::Empty_Jump);
			s.ip = op.jump;
			break;

		case Operation::Kind::While:
			++s.ip;
			break;

		case Operation::Kind::Do:
			Typecheck_Stack_Effect(s, Bool >= Empty);

			if (visited_do_ops.contains(s.ip - 1)) {
				auto const& expected = visited_do_ops[s.ip-1];

				auto [stk, exp] = std::mismatch(s.stack.begin(), s.stack.end(), expected.stack.begin(), expected.stack.end());
				if (stk != s.stack.end() || exp != expected.stack.end()) {
					error_fatal("Loop differs stack");
				}

				states.pop_back();
			} else {
				visited_do_ops.insert({ s.ip - 1, s });
				states.push_back(s);
				assert(op.jump != Operation::Empty_Jump);
				states.back().ip = op.jump;
			}
			break;

		case Operation::Kind::Call_Symbol:
			assert(op.word);
			ensure_fatal(op.word->has_effect, op.token, "cannot typecheck word `{}` without stack effect"_format(op.sval));
			typecheck_stack_effects(s, std::array { op.word->effect });
			++s.ip;
			break;

		case Operation::Kind::Return:
			goto ops_end;

		case Operation::Kind::Intrinsic:
			{
				switch (op.intrinsic) {
				case Intrinsic_Kind::Drop:
					Typecheck_Stack_Effect(s, Any >= Empty);
					break;

				case Intrinsic_Kind::Two_Drop:
					Typecheck_Stack_Effect(s, Any >> Any >= Empty);
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

				case Intrinsic_Kind::Boolean_Negate:
					Typecheck_Stack_Effect(s, Bool >= Bool);
					break;

				case Intrinsic_Kind::Boolean_And:
				case Intrinsic_Kind::Boolean_Or:
					Typecheck_Stack_Effect(s, Bool >> Bool >= Bool);
					break;

				case Intrinsic_Kind::Bitwise_And:
				case Intrinsic_Kind::Bitwise_Or:
				case Intrinsic_Kind::Bitwise_Xor:
				case Intrinsic_Kind::Left_Shift:
				case Intrinsic_Kind::Right_Shift:
				case Intrinsic_Kind::Mul:
				case Intrinsic_Kind::Div:
				case Intrinsic_Kind::Mod:
				case Intrinsic_Kind::Min:
				case Intrinsic_Kind::Max:
					Typecheck_Stack_Effect(s, Int >> Int >= Int);
					break;

				case Intrinsic_Kind::Div_Mod:
					Typecheck_Stack_Effect(s, Int >> Int >= Int >> Int);
					break;

				case Intrinsic_Kind::Dup:
					Typecheck_Stack_Effect(s, _1 >= _1 >> _1);
					break;

				case Intrinsic_Kind::Two_Dup:
					Typecheck_Stack_Effect(s, _1 >> _2 >= _1 >> _2 >> _1 >> _2);
					break;

				case Intrinsic_Kind::Over:
					Typecheck_Stack_Effect(s, _1 >> _2 >= _1 >> _2 >> _1);
					break;

				case Intrinsic_Kind::Two_Over:
					Typecheck_Stack_Effect(s, _1 >> _2 >> _3 >> _4 >= _1 >> _2 >> _3 >> _4 >> _1 >> _2);
					break;

				case Intrinsic_Kind::Swap:
					Typecheck_Stack_Effect(s, _1 >> _2 >= _2 >> _1);
					break;

				case Intrinsic_Kind::Two_Swap:
					Typecheck_Stack_Effect(s, _1 >> _2 >> _3 >> _4 >= _3 >> _4 >> _1 >> _2);
					break;

				case Intrinsic_Kind::Tuck:
					Typecheck_Stack_Effect(s, _1 >> _2 >= _2 >> _1 >> _2);
					break;

				case Intrinsic_Kind::Rot:
					Typecheck_Stack_Effect(s, _1 >> _2 >> _3 >= _3 >> _1 >> _2);
					break;

				case Intrinsic_Kind::Random32:
				case Intrinsic_Kind::Random64:
					Typecheck_Stack_Effect(s, Empty >= Int);
					break;

				case Intrinsic_Kind::Load:
					Typecheck_Stack_Effect(s, Ptr >> Int >= Empty);
					break;

				case Intrinsic_Kind::Store:
					Typecheck_Stack_Effect(s, Ptr >> Int >= Empty);
					break;

				case Intrinsic_Kind::Top:
					Typecheck_Stack_Effect(s, _1 >= _1 >> Ptr);
					break;

				case Intrinsic_Kind::Syscall:
					{
						assert(op.token.sval.size() == 8 && op.token.sval[7] >= '0' && op.token.sval[7] <= '6');
						unsigned const syscall_count = op.token.sval[7] - '0';

						Stack_Effect effect;
						std::generate_n(std::back_inserter(effect.input), syscall_count, [i = 1u]() mutable {
								return Type { Type::Kind::Variable, i++ };
						});
						effect.input.push_back({ Type::Kind::Int }); // sycall number
						effect.output.push_back({ Type::Kind::Int });
					}
					break;

				case Intrinsic_Kind::Call:
					assert_msg(false, "unimplemented: Requires support for storing stack effects in types");
					break;
				}
			}
			break;
#if 0
		default:
			unreachable("unimplemented");
#endif
		}
	}
}
