#include "stacky.hh"

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

void typecheck(std::vector<Operation> const& ops, Typestack &&typestack, Typestack const& expected)
{
	bool return_has_been_seen = false;

	std::vector<std::tuple<Typestack, Operation::Kind>> blocks;

	auto const pop = [&typestack]() -> Type { auto retval = std::move(typestack.back()); typestack.pop_back(); return retval; };
	auto const push = [&typestack](Type::Kind kind, Operation const& op) { typestack.push_back({ kind, &op }); };
	auto const top = [&typestack](unsigned offset = 0) -> Type& {	return typestack[typestack.size() - offset - 1]; };

	auto const int_binop = [&typestack](Operation const& op, Type lhs, Type const& rhs, bool emit_value = true)
	{
		if (lhs.kind != Type::Kind::Int) unexpected_type(op, { Type::Kind::Int }, lhs);
		if (rhs.kind != Type::Kind::Int) unexpected_type(op, { Type::Kind::Int }, rhs);

		if (emit_value) {
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
					auto const val = pop();
					auto const addr = pop();
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
					if (return_has_been_seen) {
						typestack = std::move(types);
						return_has_been_seen = false;
						break;
					}

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
					ensure(!return_has_been_seen, "typechecking does not fully support return right now");

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

				case Operation::Kind::Do:
					{
						ensure(!return_has_been_seen, "typechecking does not fully support return right now");
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


				case Operation::Kind::While:
				case Operation::Kind::Call_Symbol:
				case Operation::Kind::Cast:
				case Operation::Kind::End:
				case Operation::Kind::Intrinsic:
				case Operation::Kind::Push_Int:
				case Operation::Kind::Push_Symbol:
				case Operation::Kind::Return:
					unreachable("all statements falling into this case does not open blocks");
				}
			}
			break;


		case Operation::Kind::Call_Symbol:
			{
				ensure_fatal(op.word != nullptr && op.word->has_effect, op.token,
						"Cannot typecheck word `{}` without type signature"_format(op.sval));
				auto const& effect = op.word->effect;
				ensure_enough_arguments(typestack, op, effect.input.size());

				for (unsigned i = effect.input.size() - 1; i < effect.input.size(); --i) {
					auto const &in = effect.input[i];
					if (auto top = pop(); in != top)
						unexpected_type(op, in, top);
				}

				typestack.insert(std::cend(typestack), std::cbegin(effect.output), std::cend(effect.output));
			}
			break;
		case Operation::Kind::Return:
			return_has_been_seen = true;
			if (auto [e, a] = std::mismatch(std::cbegin(expected), std::cend(expected), std::cbegin(typestack), std::cend(typestack)); e != std::cend(expected) || a != std::cend(typestack)) {
				error("function body should have the same type stack as described in stack effect signature");
				if (expected.size() != typestack.size()) {
					if (e == std::cend(expected)) {
						info("there are {} excess values in function body"_format(std::distance(a, std::cend(typestack))));
						print_typestack_trace(a, std::cend(typestack), "excess");
					} else {
						info("there are missing {} values in function body"_format(std::distance(e, std::cend(expected))));
						print_typestack_trace(e, std::cend(expected), "missing");
					}
				} else {
					// stacks are equal in size, so difference is in types not their amount
					for (; e != std::cend(expected) && a != std::cend(typestack); ++e, ++a) {
						if (e->kind == a->kind) continue;
						unexpected_type({}, *e, *a);
					}
				}
				exit(1);
			}
		}
	}

	// TODO fill types
	if (auto [e, a] = std::mismatch(std::cbegin(expected), std::cend(expected), std::cbegin(typestack), std::cend(typestack)); e != std::cend(expected) || a != std::cend(typestack)) {
		error("function body should have the same type stack as described in stack effect signature");
		if (expected.size() != typestack.size()) {
			if (e == std::cend(expected)) {
				info("there are {} excess values in function body"_format(std::distance(a, std::cend(typestack))));
				print_typestack_trace(a, std::cend(typestack), "excess");
			} else {
				info("there are missing {} values in function body"_format(std::distance(e, std::cend(expected))));
				print_typestack_trace(e, std::cend(expected), "missing");
			}
		} else {
			// stacks are equal in size, so difference is in types not their amount
			for (; e != std::cend(expected) && a != std::cend(typestack); ++e, ++a) {
				if (e->kind == a->kind) continue;
				unexpected_type({}, *e, *a);
			}
		}
		exit(1);
	}
}
