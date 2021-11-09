#include <tuple>
#include <sstream>
#include "stacky.hh"

using namespace std::string_view_literals;

#include "utilities.cc"

static constexpr auto String_To_Keyword = sorted_array_of_tuples(
	std::tuple { "&fun"sv,      Keyword_Kind::Function },
	std::tuple { "--"sv,        Keyword_Kind::Stack_Effect_Divider },
	std::tuple { "is"sv,        Keyword_Kind::Stack_Effect_Definition },
	std::tuple { "[]byte"sv,    Keyword_Kind::Array },
	std::tuple { "[]u16"sv,     Keyword_Kind::Array },
	std::tuple { "[]u32"sv,     Keyword_Kind::Array },
	std::tuple { "[]u64"sv,     Keyword_Kind::Array },
	std::tuple { "[]u8"sv,      Keyword_Kind::Array },
	std::tuple { "[]usize"sv,   Keyword_Kind::Array },
	std::tuple { "any"sv,       Keyword_Kind::Typename },
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

// This value represents number of keywords inside enumeration
// Since one keyword kind may represents several symbols,
// we relay on number of kinds defined
static_assert(int(Keyword_Kind::Last)+1 == 15, "Exhaustive definition of keywords lookup");

template<unsigned Base>
static auto parse_int(Token &token) -> bool
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

	unsigned const underscores = std::count(std::cbegin(s), std::cend(s), '_');

	if (s.size() >= 2 && (s[s.size() - 2] == 'i' || s[s.size() - 2] == 'u')) {
		if (!s.ends_with('8')) {
			return false;
		}
		token.byte_size = 1;
		s.remove_suffix(2);
	} else if (s.size() >= 3 && (s[s.size() - 3] == 'i' || s[s.size() - 3] == 'u')) {
		if (s.ends_with("16")) token.byte_size = 2; else
		if (s.ends_with("32")) token.byte_size = 4; else
		if (s.ends_with("64")) token.byte_size = 8; else return false;
		s.remove_suffix(3);
	}

	token.ival = 0;

	auto i = Max_Digits - s.size() + underscores;

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

bool lex(std::string_view const file, std::string_view const path, std::vector<Token> &tokens)
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

		auto &token = tokens.emplace_back(Token::Location{path, column, line});

		if (ch == '"' || ch == '\'') {
			token.kind = ch == '"' ? Token::Kind::String : Token::Kind::Char;

			if (i + 1 > file.size())
				error_fatal(token, "Missing terminating `{}` character"_format(ch));

			if (file[i+1] == ch) {
				if (ch == '\'') error_fatal(token, "Empty character literals are invalid");
				else token.sval = "\"\"";
			} else {
				auto const str_end = std::adjacent_find(std::cbegin(file) + i + 1, std::cend(file), [terminating = ch](auto const& prev, auto const& current) {
					return prev != '\\' && current == terminating;
				});

				if (str_end == std::cend(file))
					error_fatal(token, "Missing terminating `{}` character."_format(ch));
				else
					token.sval = { std::cbegin(file) + i, str_end + 2 };
			}
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
