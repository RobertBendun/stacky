#pragma once

#include "arguments.hh"
#include <source_location>
#include <format>
#include <iostream>

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
	Warning,
	Optimization,
};

static bool Compilation_Failed = false;

inline std::string_view report_kind_str(Report r)
{
#define Color_Error   "\x1b[31;1m"
#define Color_Info    "\x1b[34;1m"
#define Color_Warning "\x1b[35;1m"
#define Color_Reset   "\x1b[0m"

	if (compiler_arguments.output_colors) {
		switch (r) {
		case Report::Command:       return Color_Info    "cmd"          Color_Reset;
		case Report::Error:         return Color_Error   "error"        Color_Reset;
		case Report::Info:          return Color_Info    "info"         Color_Reset;
		case Report::Warning:       return Color_Warning "warning"      Color_Reset;
		case Report::Optimization:  return Color_Info    "optimized"    Color_Reset;
		default:
		case Report::Compiler_Bug:  return Color_Error   "compiler bug" Color_Reset;
		}
	} else {
		switch (r) {
		case Report::Command:       return "cmd";
		case Report::Error:         return "error";
		case Report::Info:          return "info";
		case Report::Warning:       return "warning";
		case Report::Optimization:  return "optimized";
		default:
		case Report::Compiler_Bug:  return "compiler bug";
		}
	}
#undef Color_Error
#undef Color_Info
#undef Color_Warning
#undef Color_Reset
}

inline void report(Report r, Locationable auto const& loc, auto const& m)
{
	Compilation_Failed |= r == Report::Error || r == Report::Compiler_Bug;
	std::cerr << std::format("{}:{}:{}: {}: {}\n", loc.file, loc.line, loc.column, report_kind_str(r), m);
}

inline void report_prefix(Report r)
{
	Compilation_Failed |= r == Report::Error || r == Report::Compiler_Bug;
	std::cerr << std::format("stacky: {}: ", report_kind_str(r));
}

inline void report(Report r, auto const& m)
{
	Compilation_Failed |= r == Report::Error || r == Report::Compiler_Bug;
	std::cerr << std::format("stacky: {}: {}\n", report_kind_str(r), m);
}

inline void report(Report r, Has_Location_Field auto const& s, auto const& ...message)
{
	report(r, s.location, message...);
}

inline void error(auto const& ...args)
{
	report(Report::Error, args...);
}

inline void error_fatal(auto const& ...args)
{
	report(Report::Error, args...);
	exit(1);
}

inline void ensure(bool condition, auto const& ...args)
{
	if (condition) return;
	report(Report::Error, args...);
	exit(1);
}

inline void ensure_fatal(bool condition, auto const& ...args)
{
	if (condition) return;
	report(Report::Error, args...);
	exit(1);
}

inline void warning(auto const& ...args)
{
	report(Report::Warning, args...);
}

inline void info(auto const& ...args)
{
	report(Report::Info, args...);
}

inline void assert_impl(bool test, std::string_view test_str, std::source_location sl, auto const &msg = std::string_view{})
{
	if (test) return;

	std::cerr << std::format("stacky: compiler bug: Assertion `{}` in {}:{}:{}:{} failed with message: {}\n",
		test_str, sl.file_name(), sl.line(), sl.column(), sl.function_name(), msg);

	exit(1);
}

[[noreturn]]
inline void unreachable(std::string_view message, std::source_location sl = std::source_location::current())
{
	std::cerr << std::format("stacky: compiler bug: unreachable code has been reached at {}:{}:{}:{} with message: {}\n",
		sl.file_name(), sl.line(), sl.column(), sl.function_name(), message);
	exit(1);
}

#define verbose(...) do { if (compiler_arguments.verbose) info(__VA_ARGS__); } while(0)

#ifdef assert
#undef assert
#endif
#define assert_msg(Expression, ...)	assert_impl(bool(Expression), #Expression, std::source_location::current(), __VA_ARGS__)
#define assert(Expression) assert_msg(Expression, "")
