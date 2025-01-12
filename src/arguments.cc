#include "arguments.hh"
#include <iostream>

#include <boost/program_options.hpp>
#include <iterator>

namespace fs = std::filesystem;
namespace po = boost::program_options;

#include "errors.hh"

Arguments compiler_arguments;

[[noreturn]]
void help(po::options_description const& desc)
{
	std::cout << "usage: stacky build [options] <sources...>\n";
	std::cout << "       stacky run   [options] <sources...> [-- <args...>]\n";
	std::cout << desc << '\n';
	exit(1);
}

void Arguments::parse(int argc, char **argv)
try {
	std::vector<std::string> cmdline;

	{
		bool seen_separator = false;

		for (auto arg : std::span(argv+1, argv+argc)) {
			if (seen_separator) {
				arguments.push_back(arg);
			} else if (arg == std::string_view("--")) {
				seen_separator = true;
			} else {
				cmdline.push_back(arg);
			}
		}
	}

	po::options_description common("Common options");
	common.add_options()
		("help,h", "produce help message")
		("verbose,v", "print all unnesesary info during compilation")
		("check,c", "type check program")
		("no-colors,C", "errors, warnings and info messages will NOT show color coded")
	;

	po::options_description build("Build options");
	build.add_options()
		("output,o", po::value<std::string>()->value_name("<path>"), "file name of produced executable")
	;

	po::options_description config("Configuration");
	config.add_options()
		("include,I", po::value<std::vector<fs::path>>()->composing()->value_name("<path>"), "adds path to the list of dirs where Stacky files are searched when `include` or `import` word is executed")
	;

	po::options_description debug("Debugging");
	debug.add_options()
		("dump-effects", "dump all defined words types")
		("control-flow", "generate control flow graph of a program")
		("control-flow-for", po::value<std::string>()->value_name("<function>"), "generate control flow graph of a function")
	;

	po::options_description hidden("Hidden options");
	hidden.add_options()
		("source", po::value<std::vector<std::string>>(), "Source files with Stacky code")
		("command", po::value<std::string>(), "command to execute");

	po::positional_options_description pos;
	pos.add("command", 1).add("source", -1);

	po::options_description cmdline_options;
	cmdline_options.add(common).add(config).add(debug).add(hidden);

	po::options_description visible;
	visible.add(common).add(build).add(config).add(debug);


	po::parsed_options parsed = po::command_line_parser(cmdline)
		.options(cmdline_options)
		.positional(pos)
		.allow_unregistered()
		.run();

	po::variables_map vm;
	po::store(parsed, vm);

	if (!vm.count("command") || argc == 0 || vm.count("help")) {
		help(visible);
	}

	auto const& command = vm["command"].as<std::string>();

	std::vector<std::string> opts = po::collect_unrecognized(parsed.options, po::include_positional);
	opts.erase(opts.begin());

	if (command == "build" || (run_mode = command == "run")) {
		po::store(po::command_line_parser(opts).options(build).run(), vm);
	} else {
		error_fatal(std::format("Unrecognized command: {}", command));
	}

	source_files = vm["source"].as<std::vector<std::string>>();
	ensure_fatal(source_files.size(), "no input files");

	if (vm.count("output")) {
		executable = vm["output"].as<std::string>();
	} else {
		auto src_path = fs::path(source_files[0]);
		executable = src_path.parent_path();
		executable /= src_path.stem();
	}

	assembly = executable;
	assembly += ".asm";

	if (vm.count("include"))
		include_search_paths = vm["include"].as<std::vector<fs::path>>();

	compiler = fs::canonical("/proc/self/exe");
	include_search_paths.push_back(compiler.parent_path() / "std");


	verbose   = vm.count("verbose");
	typecheck = vm.count("check");
	dump_words_effects = vm.count("dump-effects");
	output_colors = !vm.count("no-colors") && isatty(STDOUT_FILENO);

	if (control_flow_graph = vm.count("control-flow")) {
		control_flow = executable;
		control_flow += ".dot";
	}

	if (vm.count("control-flow-for")) {
		control_flow_graph = true;
		control_flow_function = vm["control-flow-for"].as<std::string>();
		control_flow = executable;
		control_flow += ".fun.dot";
	}
} catch (boost::program_options::unknown_option const& u) {
	error_fatal(u.what());
}
