#include <charconv>
#include <stack>

#include "stacky.hh"
#include "utilities.cc"

namespace parser
{
	inline auto parse_stringlike(Token const& token, std::string_view sequence, auto&& accumulator)
	{
		bool next_escaped = false;
		for (unsigned i = 0; i < sequence.size(); ++i) {
			auto v = sequence[i];
			if (v == '\\') { next_escaped = true; continue; }
			if (next_escaped) {
				switch (v) {
				case '0':  v = '\0'; break;
				case '\"': v = '\"'; break;
				case '\'': v = '\''; break;
				case '\\': v = '\\'; break;
				case 'e':  v = 27;   break;
				case 'n':  v = '\n'; break;
				case 'r':  v = '\r'; break;
				case 't':  v = '\t'; break;
				case 'u':
				case 'U':
					{
						auto const length = v == 'u' ? 4 : 8;

						ensure(i + length < sequence.size(), token, std::format("Unicode escape sequence must be exactly {} digits long", length));

						uint32_t rune = 0;
						auto end = sequence.data() + i + length + 1;
						auto const [ptr, ec] = std::from_chars(sequence.data() + i + 1, end, rune, 16);
						ensure(ptr == end, token, "Found non-decimal digit inside unicode escape sequence!");

						for (auto c : utf8::encode_rune(rune)) {
							if (!callv(accumulator, true, c))
									return;
						}
						i += length;
						next_escaped = false;
						continue;
					}
					break;
				case 'x':
					{
						auto hex_value = 0u;
						ensure(i + 2 < sequence.size(), token, "Hex escape sequences are always dwo digits long");
						for (unsigned j = 0; j < 2; ++j) {
							if (auto c = sequence[i + 1 + j]; c >= '0' && c <= '9') hex_value = (hex_value << (4*j)) | (c - '0');
							else if (c >= 'a' && c <= 'f') hex_value = (hex_value << (4*j)) | (c - 'a' + 10);
							else if (c >= 'A' && c <= 'F') hex_value = (hex_value << (4*j)) | (c - 'A' + 10);
							else error(token, std::format("Expected hexadecimal digit, found: {}", c));
						}
						v = hex_value;
						i += 2;
					}
					break;
				default:
					error(token, std::format("Unrecognized escape sequence: '\\{}'", v));
				}
				next_escaped = false;
			}
			if (!callv(accumulator, true, v))
				break;
		}
	}

	void extract_strings(std::vector<Token> &tokens, std::unordered_map<std::string, unsigned> &strings)
	{
		static unsigned next_string_id = 0;

		for (auto& token : tokens) {
			if (token.kind != Token::Kind::String)
				continue;

			std::string s;
			s.reserve(token.sval.size());
			parse_stringlike(token, token.sval.substr(1, token.sval.size() - 2), [&s](char c) { s.push_back(c); });

			if (auto [it, inserted] = strings.try_emplace(std::move(s), next_string_id); inserted) {
				token.ival = next_string_id++;
			} else {
				token.ival = it->second;
			}
		}
	}

	auto extract_include_or_import(std::vector<Token> &tokens) -> std::optional<std::tuple<Keyword_Kind, fs::path, fs::path, unsigned>>
	{
		for (auto i = 0u; i < tokens.size(); ++i) {
			auto &token = tokens[i];
			if (token.kind != Token::Kind::Keyword || (token.kval != Keyword_Kind::Include && token.kval != Keyword_Kind::Import)) {
				continue;
			}

			if (token.kval == Keyword_Kind::Include) {
				ensure(i >= 1, "Include requires path");
				ensure(tokens[i-1].kind == Token::Kind::String, "Include requires path");
			} else {
				ensure(i >= 1, "Import requires path");
				ensure(tokens[i-1].kind == Token::Kind::String, "Import requires path");
			}

			return {{
				token.kval,
				fs::path(token.location.file).parent_path(),
				std::string_view(tokens[i-1].sval).substr(1, tokens[i-1].sval.size() - 2),
				i-1
			}};
		}
		return std::nullopt;
	}

	void register_definitions(std::vector<Token>& tokens, Words &words)
	{
		auto const check_if_has_been_defined = [&](auto const& token, auto const& name) {
			if (compiler_arguments.warn_redefinitions && words.contains(name)) {
				warning(token, std::format("`{}` has already been defined", name));
			}
		};

		for (unsigned i = 0; i < tokens.size(); ++i) {
			auto &token = tokens[i];
			if (token.kind != Token::Kind::Keyword)
				continue;

			switch (token.kval) {
			case Keyword_Kind::Do:
			case Keyword_Kind::Dynamic:
			case Keyword_Kind::Else:
			case Keyword_Kind::End:
			case Keyword_Kind::If:
			case Keyword_Kind::Include:
			case Keyword_Kind::Import:
			case Keyword_Kind::Return:
			case Keyword_Kind::While:
			case Keyword_Kind::Bool:
			case Keyword_Kind::Typename:
			case Keyword_Kind::Stack_Effect_Divider:
			case Keyword_Kind::Stack_Effect_Definition:
				break;

			case Keyword_Kind::Function:
				{
					static auto lambda_count = 0u;
					if (token.sval[0] == '&') {
						auto fname = Anonymous_Function_Prefix + std::to_string(token.ival = lambda_count++);
						auto &word = words[fname];
						word.kind = Word::Kind::Function;
						word.id = Word::word_count++;
						word.location = token.location;
					} else {
						ensure(i >= 1 && tokens[i-1].kind == Token::Kind::Word, token, "Function should be preceeded by an identifier");
						auto const& fname = tokens[i-1].sval;
						check_if_has_been_defined(token, fname);
						auto &word = words[fname];
						word.kind = Word::Kind::Function;
						word.id = Word::word_count++;
						word.location = token.location;
					}
				}
				break;

			case Keyword_Kind::Constant:
				{
					ensure(i >= 2 && tokens[i-2].kind == Token::Kind::Word, token, "constant must be preceeded by an identifier");
					ensure(i >= 1 && tokens[i-1].kind == Token::Kind::Integer, token, "constant must be preceeded by an integer");

					check_if_has_been_defined(token, tokens[i-2].sval);
					auto &word = words[tokens[i-2].sval];
					word.kind  = Word::Kind::Integer;
					word.id    = Word::word_count++;
					word.ival  = tokens[i-1].ival;
					word.location = tokens[i-2].location;
				}
				break;

			case Keyword_Kind::Array:
				{
					ensure(i >= 2 && tokens[i-2].kind == Token::Kind::Word, token, std::format("{} should be preceeded by an identifier", token.sval));

					unsigned size = 0;
					switch (auto &t = tokens[i-1]; t.kind) {
					case Token::Kind::Integer:
						size = t.ival;
						break;
					case Token::Kind::Word:
						if (auto it = words.find(t.sval); it != std::end(words) && it->second.kind == Word::Kind::Integer) {
							size = it->second.ival;
							break;
						}
						[[fallthrough]];
					default:
						error(token, std::format("{} should be preceeded by an integer", token.sval));
					}

					switch (token.sval[3]) {
					case 'y':
					case '8': size *= 1; break;
					case '1': size *= 2; break;
					case '3': size *= 4; break;
					case 's':
					case '6': size *= 8; break;
					default:
						assert_msg(false, "unreachable");
					}

					check_if_has_been_defined(token, tokens[i-2].sval);
					auto &word     = words[tokens[i-2].sval];
					word.kind      = Word::Kind::Array;
					word.byte_size = size;
					word.id        = Word::word_count++;
					word.location = tokens[i-2].location;
				}
				break;
			}
		}
	}

	auto crossreference(std::vector<Operation> &ops)
	{
		std::stack<unsigned> stack;

		for (auto i = 0u; i < ops.size(); ++i) {
			auto &op = ops[i];
			switch (op.kind) {
			case Operation::Kind::Do:
				ensure(!stack.empty() && ops[stack.top()].kind == Operation::Kind::While, op.token, "`do` without matching `while`");
				op.jump = stack.top();
				stack.pop();
				stack.push(i);
				break;

			case Operation::Kind::While:
			case Operation::Kind::If:
				stack.push(i);
				break;

			case Operation::Kind::Else:
				// TODO add ensure of if existance
				ensure(!stack.empty() && ops[stack.top()].kind == Operation::Kind::If, op.token, "`else` without matching `if`");
				ops[stack.top()].jump = i + 1;
				stack.pop();
				stack.push(i);
				break;

			case Operation::Kind::End:
				{
					assert(!stack.empty());
					switch (ops[stack.top()].kind) {
					case Operation::Kind::If:
					case Operation::Kind::Else:
						ops[stack.top()].jump = i;
						stack.pop();
						ops[i].jump = i + 1;
						break;
					case Operation::Kind::Do:
						ops[i].jump = ops[stack.top()].jump;
						ops[stack.top()].jump = i + 1;
						stack.pop();
						break;
					default:
						// TODO vvvvvvvvvvvvvvvvvvvvv
						error(op.token, "End can only close `while..do` and `if` blocks");
						return false;
					}
				}
				break;

			default:
				;
			}
		}

		if (!stack.empty()) {
			auto const& token = ops[stack.top()].token;
			switch (token.kval) {
				case Keyword_Kind::If:	error(token, "Expected matching `else` or `end` for this `if`"); break;
				case Keyword_Kind::Else: 	error(token, "Expected matching `end` for this `else`"); break;
				case Keyword_Kind::While: error(token, "Expected matching `do` for this `while`"); break;
				case Keyword_Kind::Do: error(token, "Expected matching `end` for this `do`"); break;
				default: ;
			}
		}

		return true;
	}

	void translate_operation(std::span<Token> const& tokens, std::vector<Operation>& body, Words& words)
	{
		switch (auto const& token = tokens.back(); token.kind) {
		case Token::Kind::Address_Of:
			{
				auto &op = body.emplace_back(Operation::Kind::Push_Symbol);
				op.symbol_prefix = Function_Prefix;
				// TODO this may throw if we are trying to take address of non existing word
				op.ival = words.at(token.sval.substr(1)).id;
				op.token = token;
				op.location = token.location;
			}
			break;

		case Token::Kind::Char:
			{
				auto &op = body.emplace_back(Operation::Kind::Push_Int);
				op.token = token;
				op.ival = 0;
				// TODO investigate what type should char have. Since we support multibyte char literals, maybe it should have
				// the smallest type containing posibble value?
				op.type = Type::Kind::Int;
				op.location = token.location;

				parse_stringlike(token, token.sval.substr(1, token.sval.size() - 2),
						[&token, value = &op.ival, offset = 0](char c) mutable {
							*value |= c << 8 * offset++;
							// TODO 64-bit platform specific limitation
							if (offset > 8) {
								error(token, "Character literal cannot be longer then 8 bytes on this platform!");
								return false;
							}
							return true;
						}
				);
			}
			break;

		case Token::Kind::Integer:
			{
				auto &op = body.emplace_back(Operation::Kind::Push_Int);
				op.ival = token.ival;
				op.token = token;
				op.location = token.location;
			}
			break;

		case Token::Kind::String:
			{
				auto &op = body.emplace_back(Operation::Kind::Push_Symbol);
				op.symbol_prefix = String_Prefix;
				op.token = token;
				op.ival = token.ival;
				op.location = token.location;
			}
			break;

		case Token::Kind::Word:
			{
				auto word_it = words.find(token.sval);
				ensure(word_it != std::end(words), std::format("Word `{}` has not been defined yet", token.sval));
				switch (auto &word = word_it->second; word.kind) {
				case Word::Kind::Intrinsic:
					{
						auto &op = body.emplace_back(Operation::Kind::Intrinsic);
						op.intrinsic = word.intrinsic;
						op.token = token;
						op.location = token.location;
					}
					break;
				case Word::Kind::Integer:
					{
						// TODO investigate if word.token is set properly for integers
						// TODO Integer words erease type
						auto &op = body.emplace_back(Operation::Kind::Push_Int);
						op.ival = word.ival;
						op.token = token;
						op.location = token.location;
					}
					break;
				case Word::Kind::Array:
					{
						auto &op = body.emplace_back(Operation::Kind::Push_Symbol);
						op.symbol_prefix = Symbol_Prefix;
						op.ival = word.id;
						op.sval = token.sval;
						op.token = token;
						op.location = token.location;
					}
					break;
				case Word::Kind::Function:
					{
						auto &op = body.emplace_back(Operation::Kind::Call_Symbol);
						op.sval = token.sval;
						op.token = token;
						op.symbol_prefix = Function_Prefix;
						op.ival = word.id;
						op.word = &word;
						op.location = token.location;
					}
					break;
				}
			}
			break;

		case Token::Kind::Keyword:
			{
				switch (token.kval) {
				case Keyword_Kind::Array:
				case Keyword_Kind::Constant:
				case Keyword_Kind::End:
				case Keyword_Kind::Function:
				case Keyword_Kind::Dynamic:
				case Keyword_Kind::Stack_Effect_Definition:
				case Keyword_Kind::Stack_Effect_Divider:
					unreachable(
						"`translate_operation` only resolves simple operations. This should be handled by either funciton or global parser");
					break;
				case Keyword_Kind::Import:
				case Keyword_Kind::Include:
					unreachable("all includes should be eliminated in file inclusion process");
					break;

				case Keyword_Kind::Do:     body.emplace_back(Operation::Kind::Do,     token); break;
				case Keyword_Kind::Else:   body.emplace_back(Operation::Kind::Else,   token); break;
				case Keyword_Kind::If:     body.emplace_back(Operation::Kind::If,     token); break;
				case Keyword_Kind::Return: body.emplace_back(Operation::Kind::Return, token); break;
				case Keyword_Kind::While:  body.emplace_back(Operation::Kind::While,  token); break;

				case Keyword_Kind::Bool:
					{
						auto &op = body.emplace_back(Operation::Kind::Push_Int);
						op.ival = token.sval[0] == 't';
						op.token = token;
						op.type = Type::Kind::Bool;
						op.location = token.location;
					}
					break;
				case Keyword_Kind::Typename:
					{
						auto &op = body.emplace_back(Operation::Kind::Cast);
						op.token = token;
						op.type = Type::from(token);
						op.location = token.location;
					}
					break;
				}
			}
			break;
		}
	}

	void function_into_operations(std::span<Token> const& tokens, Word &func, Words& words)
	{
		std::optional<Stack_Effect> stack_effect_declaration = std::nullopt;

		auto &body = func.function_body;
		for (unsigned i = tokens.size() - 1; i < tokens.size(); --i) {
			auto const& token = tokens[i];
			if (token.kind != Token::Kind::Keyword) {
trivial:
				auto const trimmed = tokens.subspan(0, i+1);
				translate_operation(trimmed, body, words);
				continue;
			}

			switch (token.kval) {
			// Reasoning is that functions does not introduce scope, and definition inside them is global which is
			// counter-intuitive for most programmers
			case Keyword_Kind::Array:
			case Keyword_Kind::Constant:
				error(token, "Definitions of arrays or constants are not allowed inside function bodies!");
				break;

			case Keyword_Kind::Dynamic:
				func.is_dynamically_typed = true;
				ensure(i == 0, "Dynamic specifier must be placed after function keyword!");
				break;

			case Keyword_Kind::Stack_Effect_Definition:
				{
					// 1. Only typenames or numbers (chosen syntax for generics) are allowed
					// 2. Must be preceeded by function (in another words it extends to the start of tokens span)
					bool divider_has_been_seen = false;
					Stack_Effect effect;

					for (unsigned j = i-1; j < tokens.size(); --j) {
						auto const &token = tokens[j];
						if (token.kind == Token::Kind::Integer) {
							unreachable("unimplemented: Type variables");
						}
						if (token.kind != Token::Kind::Keyword) {
							error_fatal(token, "Type specification only allows integers or type names");
							break;
						}

						switch (token.kval) {
						case Keyword_Kind::Stack_Effect_Definition:
							error_fatal(token, "Nested type definitions are not allowed (`is` inside type definition)");
							break;
						case Keyword_Kind::Stack_Effect_Divider:
							ensure(!divider_has_been_seen, tokens[j], "Nested type definitions are not allowed (multiple `--` inside type definition");
							divider_has_been_seen = true;
							break;
						case Keyword_Kind::Typename:
							effect[divider_has_been_seen].push_back(Type::from(token));
							break;

						case Keyword_Kind::Dynamic:
							error_fatal(token, "Funciton cannot have type signature and be dynamic at the same time. (`dyn` inside type specification)");
							break;

						default:
							ensure_fatal(false, token, "Types can only be specified for functions");
							break;
						}
					}

					// TODO maybe iterate forward and don't reverse?
					std::reverse(std::begin(effect.input), std::end(effect.input));
					std::reverse(std::begin(effect.output), std::end(effect.output));

					stack_effect_declaration = std::move(effect);
					i = 0;
				}
				break;

			case Keyword_Kind::End:
				{
					unsigned const block_end = i;
					unsigned block_start, end_stack = 1;
					ensure_fatal(i >= 1, token, "Unexpected `end`.");

					for (block_start = i-1; block_start < tokens.size() && end_stack > 0; --block_start) {
						if (auto &t = tokens[block_start]; t.kind == Token::Kind::Keyword) {
							switch (t.kval) {
							case Keyword_Kind::End:      ++end_stack; break;
							case Keyword_Kind::Function: --end_stack; break;
							case Keyword_Kind::If:       --end_stack; break;
							case Keyword_Kind::While:    --end_stack; break;
							default:
								;
							}
						}
					}

					// We reached start of the file but there are still some ends on the end_stack
					ensure_fatal(end_stack == 0, token, "Unexpected `end`.");
					++block_start;

					auto const& start_token = tokens[block_start];

					if (start_token.kval != Keyword_Kind::Function) {
						body.emplace_back(Operation::Kind::End, token);
						break;
					}

					Word *word = nullptr;
					if (start_token.sval.front() == '&') {
						word = &words.at(Anonymous_Function_Prefix + std::to_string(tokens[block_start].ival));
						auto &op =  body.emplace_back(Operation::Kind::Push_Symbol);
						op.symbol_prefix = Function_Prefix;
						op.ival = word->id;
						op.token = start_token;
						i = block_start;
					} else {
						// TODO is this safe? dunno
						word = &words.at(tokens[block_start-1].sval);
						i = block_start - 1; // remove function name
					}

					function_into_operations({ tokens.begin() + block_start + 1, block_end - block_start - 1 }, *word, words);
				}
				break;

			default:
				goto trivial;
			}
		}

		if (stack_effect_declaration) {
			func.has_effect = true;
			func.effect = *std::move(stack_effect_declaration);
		}

		std::reverse(std::begin(body), std::end(body));
		std::for_each(body.begin(), body.end(), [&func](auto &o) { o.location = o.location.with_function(func.function_name); });
		crossreference(body);
	}

	void into_operations(std::span<Token> const& tokens, std::vector<Operation> &body, Words &words)
	{
		for (unsigned i = tokens.size() - 1; i < tokens.size(); --i) {
			auto const& token = tokens[i];
			if (token.kind != Token::Kind::Keyword) {
trivial:
				auto const trimmed = tokens.subspan(0, i+1);
				translate_operation(trimmed, body, words);
				continue;
			}

			switch (token.kval) {
			// TODO this is a hack
			case Keyword_Kind::Array:    i -= 2; break;
			case Keyword_Kind::Constant: i -= 2; break;

			case Keyword_Kind::End:
				{
					unsigned const block_end = i;
					unsigned block_start, end_stack = 1;
					ensure_fatal(i >= 1, token, "Unexpected `end`.");

					for (block_start = i-1; block_start < tokens.size() && end_stack > 0; --block_start) {
						if (auto &t = tokens[block_start]; t.kind == Token::Kind::Keyword) {
							switch (t.kval) {
							case Keyword_Kind::End:      ++end_stack; break;
							case Keyword_Kind::Function: --end_stack; break;
							case Keyword_Kind::If:       --end_stack; break;
							case Keyword_Kind::While:    --end_stack; break;
							default:
								;
							}
						}
					}

					// We reached start of the file but there are still some ends on the end_stack
					ensure_fatal(end_stack == 0, token, "Unexpected `end`.");
					++block_start;

					auto const& start_token = tokens[block_start];

					if (start_token.kval != Keyword_Kind::Function) {
						body.emplace_back(Operation::Kind::End, token);
						break;
					}

					Word *word = nullptr;
					if (start_token.sval.front() == '&') {
						word = &words.at(Anonymous_Function_Prefix + std::to_string(tokens[block_start].ival));
						auto &op =  body.emplace_back(Operation::Kind::Push_Symbol);
						op.symbol_prefix = Function_Prefix;
						op.ival = word->id;
						op.token = start_token;
						i = block_start;
						// TODO not sure if this should be "anonymous" or empty
						// word->function_name = "<anonymous>";
					} else {
						// TODO is this safe? dunno
						word = &words.at(tokens[block_start-1].sval);
						word->function_name = tokens[block_start-1].sval;
						i = block_start - 1; // remove function name
					}

					function_into_operations({ tokens.begin() + block_start + 1, block_end - block_start - 1 }, *word, words);
				}
				break;

			default:
				goto trivial;
			}
		}

		std::reverse(std::begin(body), std::end(body));
		crossreference(body);
	}
}
