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

						ensure(i + length < sequence.size(), token, "Unicode escape sequence must be exactly ", length, " digits long");

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
							else error(token, "Expected hexadecimal digit, found: ", c);
						}
						v = hex_value;
						i += 2;
					}
					break;
				default:
					error(token, "Unrecognized escape sequence: '\\", v, "'");
				}
				next_escaped = false;
			}
			if (!callv(accumulator, true, v))
				break;
		}
	}

	auto extract_strings(std::vector<Token> &tokens, std::unordered_map<std::string, unsigned> &strings)
	{
		static unsigned next_string_id = 0;

		for (auto& token : tokens) {
			if (token.kind != Token::Kind::String)
				continue;

			// TODO insert based not on how string is written in source code
			// but how it's on result basis. Strings like "Hi world" and "Hi\x20world"
			// should resolve into equal pointers

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

	auto extract_include(std::vector<Token> &tokens) -> std::optional<std::tuple<fs::path, fs::path, unsigned>>
	{
		for (auto i = 0u; i < tokens.size(); ++i) {
			auto &token = tokens[i];
			if (token.kind != Token::Kind::Keyword || token.kval != Keyword_Kind::Include) {
				continue;
			}

			ensure(i >= 1, "Include requires path");
			ensure(tokens[i-1].kind == Token::Kind::String, "Include requires path");

			return {{
				fs::path(token.location.file).parent_path(),
				std::string_view(tokens[i-1].sval).substr(1, tokens[i-1].sval.size() - 2),
				i-1
			}};
		}
		return std::nullopt;
	}

	auto register_definitions(std::vector<Token> const& tokens, Words &words)
	{
		auto const check_if_has_been_defined = [&](auto const& token, auto const& name) {
			if (compiler_arguments.warn_redefinitions && words.contains(name)) {
				warning(token, name, " has already been defined");
			}
		};

		for (unsigned i = 0; i < tokens.size(); ++i) {
			auto token = tokens[i];
			if (token.kind != Token::Kind::Keyword)
				continue;

			switch (token.kval) {
			case Keyword_Kind::Do:
			case Keyword_Kind::Else:
			case Keyword_Kind::End:
			case Keyword_Kind::If:
			case Keyword_Kind::Include:
			case Keyword_Kind::Return:
			case Keyword_Kind::While:
				break;

			case Keyword_Kind::Function:
				{
					ensure(i >= 1 && tokens[i-1].kind == Token::Kind::Word, token, "Function should be preceeded by an identifier");
					auto const& fname = tokens[i-1].sval;
					check_if_has_been_defined(token, fname);
					auto &word = words[fname];
					word.kind = Word::Kind::Function;
					word.id = Word::word_count++;
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
				}
				break;

			case Keyword_Kind::Array:
				{
					ensure(i >= 2 && tokens[i-2].kind == Token::Kind::Word, token, token.sval, " should be preceeded by an identifier");

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
						error(token, token.sval, " should be preceeded by an integer");
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

	void transform_into_operations(std::span<Token> const& tokens, std::vector<Operation> &body, Words& words)
	{
		for (unsigned i = tokens.size() - 1; i < tokens.size(); --i) {
			auto &token = tokens[i];
			switch (token.kind) {
			case Token::Kind::Address_Of:
				{
					auto &op = body.emplace_back(Operation::Kind::Push_Symbol);
					op.symbol_prefix = Function_Prefix;
					op.ival = words.at(token.sval.substr(1)).id;
					op.token = token;
				}
				break;

			case Token::Kind::Char:
				{
					auto &op = body.emplace_back(Operation::Kind::Push_Int);
					op.token = token;
					op.ival = 0;

					parse_stringlike(token, token.sval.substr(1, token.sval.size() - 2),
							[&token, value = &op.ival, offset = 0](char c) mutable {
								*value |= c << 8 * offset++;
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
				}
				break;

			case Token::Kind::String:
				{
					auto &op = body.emplace_back(Operation::Kind::Push_Symbol);
					op.symbol_prefix = String_Prefix;
					op.token = token;
					op.ival = token.ival;
				}
				break;

			case Token::Kind::Word:
				{
					auto word_it = words.find(token.sval);
					ensure(word_it != std::end(words), token, "Word `", token.sval, "` has not been defined yet");
					switch (auto word = word_it->second; word.kind) {
					case Word::Kind::Intrinsic:
						{
							auto &op = body.emplace_back(Operation::Kind::Intrinsic);
							op.intrinsic = word.intrinsic;
							op.token = token;
						}
						break;
					case Word::Kind::Integer:
						{
							auto &op = body.emplace_back(Operation::Kind::Push_Int);
							op.ival = word.ival;
						}
						break;
					case Word::Kind::Array:
						{
							auto &op = body.emplace_back(Operation::Kind::Push_Symbol);
							op.symbol_prefix = Symbol_Prefix;
							op.ival = word.id;
						}
						break;
					case Word::Kind::Function:
						{
							auto &op = body.emplace_back(Operation::Kind::Call_Symbol);
							op.sval = token.sval;
							op.symbol_prefix = Function_Prefix;
							op.ival = word.id;
						}
						break;
					}
				}
				break;

			case Token::Kind::Keyword:
				{
					switch (token.kval) {
					case Keyword_Kind::End:
						{
							unsigned j, end_stack = 1;
							ensure_fatal(i >= 1, token, "Unexpected `end`.");
							for (j = i-1; j < tokens.size() && end_stack > 0; --j) {
								if (auto &t = tokens[j]; t.kind == Token::Kind::Keyword)
									switch (t.kval) {
									case Keyword_Kind::End:      ++end_stack; break;
									case Keyword_Kind::Function: --end_stack; break;
									case Keyword_Kind::If:       --end_stack; break;
									case Keyword_Kind::While:    --end_stack; break;
									default:
										;
									}
							}

							ensure_fatal(end_stack == 0, token, "Unexpected `end`.");
							++j;

							if (tokens[j].kval == Keyword_Kind::Function) {
								auto &func = words.at(tokens[j-1].sval);
								transform_into_operations({ tokens.begin() + j + 1, i - j - 1 }, func.function_body, words);
								i = j-1;
							} else {
								body.emplace_back(Operation::Kind::End, token);
							}
						}
						break;

						case Keyword_Kind::Include: break; // all includes should be eliminated by now
						case Keyword_Kind::Array:    i -= 2; break;
						case Keyword_Kind::Constant: i -= 2; break;
						case Keyword_Kind::Function: i -= 1; break;

						case Keyword_Kind::Do:          body.emplace_back(Operation::Kind::Do, token);     break;
						case Keyword_Kind::Else:        body.emplace_back(Operation::Kind::Else, token);   break;
						case Keyword_Kind::If:          body.emplace_back(Operation::Kind::If, token);     break;
						case Keyword_Kind::Return:      body.emplace_back(Operation::Kind::Return, token); break;
						case Keyword_Kind::While:       body.emplace_back(Operation::Kind::While, token);  break;
					}
				}
				break;
			}
		}

		std::reverse(std::begin(body), std::end(body));
		crossreference(body);
	}
}
