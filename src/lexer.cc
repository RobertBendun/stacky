template<unsigned Base>
auto parse_int(Token &token) -> bool
{
	constexpr auto Max_Digits = max_digits64(Base);
	auto s = std::string_view(token.sval);

	if (Base != 10)
		s.remove_prefix(2); // skip 0_

	while (s.front() == '0' || s.front() == '_')
		s.remove_prefix(1);

	static constexpr auto Powers_Lookup = []<size_t ...I>(std::index_sequence<I...>) {
		return std::array {
			std::uint64_t(pow(Base, Max_Digits - I - 1))...
		};
	}(std::make_index_sequence<Max_Digits>{});

	static_assert(Powers_Lookup.size() == Max_Digits);

	unsigned underscores = std::count(std::cbegin(s), std::cend(s), '_');

	token.ival = 0;
	auto i = Max_Digits - s.size() - underscores;

	for (; !s.empty(); s.remove_prefix(1)) {
		auto const c = s.front();
		if (c == '_') continue;

		std::uint64_t const value =
			(c >= '0' && c <= '9') ? c - '0' :
			(c >= 'a' && c <= 'z') ? c - 'a' + 10 :
			(c >= 'A' && c <= 'Z') ? c - 'A' + 10 : -1;

		if (value >= Base) return false;
		token.ival += Powers_Lookup[i++] * value;
	}

	return true;
}

auto lex(std::string_view const file, std::string_view const path, std::vector<Token> &tokens)
{
	unsigned column = 1, line = 1;

	for (unsigned i = 0; i < file.size();) {
		auto ch = file[i];

		for (;;) {
			bool done_sth = false;

			for (; i < file.size() && std::isspace(ch); ch = file[++i]) {
				done_sth = true;
				if (ch == '\n') {
					++line;
					column = 1;
				} else {
					++column;
				}
			}

			if (i < file.size() && ch == '#') {
				done_sth = true;
				auto after_comment = std::find(std::cbegin(file) + i, std::cend(file), '\n');
				i = after_comment - std::cbegin(file) + 1;
				ch = file[i];
				column = 1;
				line++;
			}

			if (!done_sth)
				break;
		}

		if (i == file.size())
			break;

		auto &token = tokens.emplace_back(Location{path, column, line});

		if (ch == '"' || ch == '\'') {
			token.kind = ch == '"' ? Token::Kind::String : Token::Kind::Char;
			auto const str_end = std::adjacent_find(std::cbegin(file) + i + 1, std::cend(file), [terminating = ch](auto const& prev, auto const& current) {
				return prev != '\\' && current == terminating;
			});

			if (str_end == std::cend(file))
				error(token, "Missing terminating ", ch, " character");

			token.sval = { std::cbegin(file) + i, str_end + 2 };
		} else {
			auto const start = std::cbegin(file) + i;
			auto const first_ws = std::find_if(start, std::cend(file), static_cast<int(*)(int)>(std::isspace));
			token.sval = { start, first_ws };

			// is token a keyword?
			if (auto found = std::lower_bound(std::cbegin(String_To_Keyword), std::cend(String_To_Keyword), token.sval, [](auto const& lhs, auto const& rhs)
						{ return std::get<0>(lhs) < rhs; }); found != std::cend(String_To_Keyword) && std::get<0>(*found) == token.sval) {
				token.kind = Token::Kind::Keyword;
				token.kval = std::get<1>(*found);
			} else {
				bool consumed = false;
				if (token.sval[0] == '0') {
					switch (token.sval[1]) {
					case 'b': consumed = parse_int<2>(token);  break;
					case 's': consumed = parse_int<6>(token);  break;
					case 'o': consumed = parse_int<8>(token);  break;
					case 'x': consumed = parse_int<16>(token); break;
					default:
						;
					}
				}

				if (!consumed)
					consumed = parse_int<10>(token);

				if (consumed) {
					token.kind = Token::Kind::Integer;
					i += token.sval.size();
					column += token.sval.size();
					continue;
				}

				if (token.sval.front() == '&')
					token.kind = Token::Kind::Address_Of;
				else
					token.kind = Token::Kind::Word;
			}
		}

		i += token.sval.size();
		column += token.sval.size();
	}

	return true;
}
