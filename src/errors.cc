template<typename Location>
concept Locationable = requires (Location const& loc) {
	{ loc.file }   -> std::convertible_to<std::string_view>;
	{ loc.line }   -> std::convertible_to<unsigned>;
	{ loc.column } -> std::convertible_to<unsigned>;
};

template<typename Struct>
concept Has_Location_Field = requires (Struct const& s) {
	{ s.location } -> Locationable;
};

enum class Report
{
	Command,
	Compiler_Bug,
	Error,
	Info,
};

static bool Compilation_Failed = false;

inline void report(Report report, auto const& ...message)
{
	auto& out = report == Report::Info ? std::cout : std::cerr;

	std::cout << std::flush;
	std::cerr << std::flush;
	switch (report) {
	case Report::Command:      out << "[CMD] ";          break;
	case Report::Compiler_Bug: out << "[COMPILER BUG] "; break;
	case Report::Error:        out << "[ERROR] ";        break;
	case Report::Info:         out << "[INFO] ";         break;
	}

	Compilation_Failed |= (report == Report::Compiler_Bug || report == Report::Compiler_Bug);

	(out << ... << message) << '\n';

	if (report == Report::Compiler_Bug)
		std::exit(1);
}

inline void report(Report r, Locationable auto const& loc, auto const& ...message)
{
	report(r, loc.file, ':', loc.line, ':', loc.column, ": ", message...);
}

inline void report(Report r, Has_Location_Field auto const& s, auto const& ...message)
{
	report(r, s.location, message...);
}

inline void error(auto const& ...args)
{
	report(Report::Error, args...);
}

inline void ensure(bool condition, auto const& ...args)
{
	if (condition) return;
	report(Report::Error, args...);
}

inline void assert_impl(bool test, std::string_view test_str, std::source_location sl, auto const& ...args)
{
	if (test) return;
	report(Report::Compiler_Bug, "Assertion ", std::quoted(test_str), " in ",
			sl.file_name(), ':', sl.line(), ':', sl.column(), ':', sl.function_name(),
			"\n\twith message: ", args...);
}

#ifdef assert
#undef assert
#endif
#define assert_msg(Expression, ...)	assert_impl(bool(Expression), #Expression, std::source_location::current(), __VA_ARGS__)
#define assert(Expression) assert_msg(Expression, "")
