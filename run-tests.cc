#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <source_location>
#include <span>

#include "errors.cc"
#include "ipstream.hh"
#include "utilities.cc"

using namespace std::string_view_literals;
namespace fs = std::filesystem;
namespace rc = std::regex_constants;

static bool quiet_mode       = false;
static bool dot_compare_mode = false;
static unsigned tests_count  = 0;
static unsigned tests_passed = 0;

template<typename It>
requires std::is_same_v<std::iter_value_t<It>, char>
struct Line_Column_Iterator : It
{
	Line_Column_Iterator() = default;
	Line_Column_Iterator(It it) : It(it) { update(); }

	auto base() const
	{
		return static_cast<It>(*this);
	}

	auto operator++() -> Line_Column_Iterator&
	{
		It::operator++();
		update();
		return *this;
	}

	void update()
	{
		char c = It::operator*();
		switch (c) {
		case '\n': ++line;     [[fallthrough]];
		case '\r': column = 1; break;
		default:
			++column;
		}
	}

	unsigned line = 1;
	unsigned column = 0;
};

template<typename It>
auto print_in_context(It begin, It section_begin, It section_end, It end)
{
	*std::copy(
		std::find(std::reverse_iterator(section_begin), std::reverse_iterator(begin), '\n').base(),
		std::find(section_end, end, '\n'),
		std::ostream_iterator<char>(std::cout)
	) = '\n';
}

int main(int argc, char **argv)
{
	auto const test_dir = fs::path("./tests");

	for (std::string_view arg : std::span(argv + 1, argc - 1)) {
		if (arg == "-q" || arg == "--quiet"sv) {
			quiet_mode = true;
		}
	}

	switch (auto status = fs::status(test_dir); status.type()) {
	case fs::file_type::not_found: error("Directory ", test_dir, " does not exists"); return 1;
	case fs::file_type::directory: break;
	default: error(test_dir, " is not a directory");
	}


	auto dot_compare = std::regex("#\\s+dot\\s+compare\\s+", rc::optimize | rc::icase | rc::ECMAScript);
	for (auto entry : fs::recursive_directory_iterator(test_dir)) {
		auto const& source_code_path = entry.path();
		if (source_code_path.extension() != ".stacky")
			continue;
		++tests_count;

		auto output_file_path = source_code_path.parent_path();
		output_file_path /= source_code_path.stem();
		output_file_path += ".txt";

		auto executable_path = source_code_path.parent_path();
		executable_path /= source_code_path.stem();

		if (!fs::exists(output_file_path)) {
			error("Test ", source_code_path, " does not have matching expected output file ", output_file_path);
			return 1;
		}

		std::string source;
		{
			std::ifstream source_code(source_code_path);
			if (!source_code) {
				error("Cannot open test ", source_code_path);
				return 1;
			}
			source.assign(std::istreambuf_iterator<char>(source_code), {});
		}

		dot_compare_mode = std::regex_search(source, dot_compare);
		assert_msg(dot_compare_mode, "Dot compare is currently only supported mode");

		run_command(quiet_mode, "./stacky ", source_code_path);

		std::string output;
		{
			std::ifstream output_file(output_file_path);
			if (!output_file) {
				error("Cannot open output file ", output_file_path);
				return 1;
			}
			output.assign(std::istreambuf_iterator<char>(output_file), {});
		}

		std::string program;
		{
			rp::ipstream program_process(executable_path);
			if (!program_process) {
				error("Cannot execute program ", executable_path);
				return 1;
			}
			program.assign(std::istreambuf_iterator<char>(program_process), {});
		}

		auto const source_begin  = Line_Column_Iterator(std::cbegin(source));
		auto const source_end    = Line_Column_Iterator(std::cend(source));

		auto const output_begin  = Line_Column_Iterator(std::cbegin(output));
		auto       output_it     = output_begin;
		auto const output_end    = Line_Column_Iterator(std::cend(output));

		auto const program_begin = Line_Column_Iterator(std::cbegin(program));
		auto       program_it    = program_begin;
		auto const program_end   = Line_Column_Iterator(std::cend(program));

		for (bool failed = false;;) {
			std::tie(program_it, output_it) = std::mismatch(program_it, program_end, output_it, output_end);
			if (program_it == program_end && output_it == output_end) {
				tests_passed += !failed;
				break;
			}

			failed = true;
			auto const [pit, oit] = std::mismatch(program_it, program_end, output_it, output_end, std::not_equal_to{});
			auto const first_dot = find_nth(source_begin, source_end, program_it.line, '.');
			auto const last_dot = find_nth(first_dot, source_end, pit.line - program_it.line, '.');

			std::cout << "[FAIL] " << source_code_path << " diverges from provided expected output\n";

			std::cout << "src:" << first_dot.line << ':' << first_dot.column << ": ";
			print_in_context(source_begin, first_dot, last_dot, source_end);

			std::cout << "out:" << output_it.line << ':' << output_it.column << ": ";
			print_in_context(output_begin, output_it, oit, output_end);

			std::cout << "exe:" << program_it.line << ':' << program_it.column << ": ";
			print_in_context(program_begin, program_it, pit, program_end);

			std::tie(program_it, output_it) = std::tuple{pit, oit};
		}
	}

	if (!quiet_mode || tests_count != tests_passed) {
		std::cout << "-------- RESULTS --------\n";
		std::cout << "Tests count:  " << tests_count << '\n';
		std::cout << "Tests passed: " << tests_passed << " (" << (tests_passed * 100 / tests_count) << "%)\n";
	}

	return tests_count != tests_passed;
}
