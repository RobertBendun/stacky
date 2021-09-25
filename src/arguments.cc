#include <iostream>
#include <iomanip>

#include <boost/program_options.hpp>

namespace po = boost::program_options;

[[noreturn]]
void help(po::options_description const& desc)
{
	std::cout << "usage: stacky [command] [options]\n";
	std::cout << desc << '\n';
	exit(1);
}

void parse_arguments(int argc, char **argv)
{
	po::options_description common("Common options");
	common.add_options()
		("help,h", "produce help message")
		("verbose", "print all unnesesary info during compilation")
		("version,v", "print version of compiler")
	;

	po::options_description build("Build options");
	build.add_options()
		("output-file,o", po::value<std::string>(), "file name of produced executable")
		("target", po::value<std::string>(), "specifies compilation target (either `linux` or `x86_64`)")
	;

	po::options_description config("Configuration");
	config.add_options()
		("callstack-size", po::value<unsigned>(), "specify size of callstack")
		("constant,C", po::value<std::vector<std::string>>()->composing(), "define constant from command line")
		("include-path,I", po::value<std::vector<std::string>>()->composing(), "adds path to the list of dirs where Stacky files are searched when `include` word is executed")
	;

	po::options_description debug("Debugging");
	debug.add_options()
		("print-function,F", po::value<std::vector<std::string>>()->composing(), "print soon emited words of given function")
		("print-program,P", "print soon emited words of program")
	;

	po::options_description hidden("Hidden options");
	hidden.add_options()
		("source-file", po::value<std::vector<std::string>>(), "Source files with Stacky code")
		("command", po::value<std::string>(), "command to execute");

	po::positional_options_description pos;
	pos.add("command", 1).add("source-file", -1);

	po::options_description cmdline_options;
	cmdline_options.add(common).add(config).add(debug).add(hidden);

	po::options_description visible;
	visible.add(common).add(build).add(config).add(debug);

	po::parsed_options parsed = po::command_line_parser(argc, argv)
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

	if (command == "run") {
		compiler_arguments.run_mode = true;
	} else if (command == "build") {
		po::store(po::command_line_parser(opts).options(build).run(), vm);
	} else {
		std::cerr << "[ERROR] Unrecognized command: " << std::quoted(command) << '\n';
		exit(1);
	}

	if (!vm.count("source-file")) {
		std::cerr << "[ERROR] No source files were provided\n";
		exit(1);
	}

	compiler_arguments.source_files = vm["source-file"].as<std::vector<std::string>>();

	if (vm.count("output-file")) {
		compiler_arguments.executable = vm["output-file"].as<std::string>();
		std::cout << "Output file: " << compiler_arguments.executable << '\n';
	} else {
		auto src_path = fs::path(compiler_arguments.source_files[0]);
		compiler_arguments.executable = src_path.parent_path();
		compiler_arguments.executable /= src_path.stem();
	}

	compiler_arguments.assembly = compiler_arguments.executable;
	compiler_arguments.assembly += ".asm";
}
