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

		if (ch == '"') {
			token.kind = Token::Kind::String;
			auto const str_end = std::adjacent_find(std::cbegin(file) + i + 1, std::cend(file), [](auto const& prev, auto const& current) {
				return prev != '\\' && current == '\"';
			});

			if (str_end == std::cend(file))
				error(token, "Missing terminating \" character");

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
			} else if (std::all_of(std::cbegin(token.sval), std::cend(token.sval), [](auto c) { return std::isdigit(c) || c == '_'; })) {
				token.kind = Token::Kind::Integer;
				auto [ptr, ec] = std::from_chars(token.sval.data(), token.sval.data() + token.sval.size(), token.ival);
				assert(ptr == token.sval.data() + token.sval.size());
			} else if (token.sval.front() == '&') {
				token.kind = Token::Kind::Address_Of;
			} else {
				token.kind = Token::Kind::Word;
			}
		}

		i += token.sval.size();
		column += token.sval.size();
	}

	return true;
}
