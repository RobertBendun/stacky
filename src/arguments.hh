#pragma once

#include <filesystem>
#include <string>
#include <vector>

extern struct Arguments
{
	std::vector<std::filesystem::path>   include_search_paths;
	std::vector<std::string>             source_files;
	std::vector<std::string>             arguments;
	std::filesystem::path                compiler;
	std::filesystem::path                executable;
	std::filesystem::path                assembly;
	std::filesystem::path                control_flow;

	std::string control_flow_function;

	bool warn_redefinitions = true;
	bool verbose            = false;
	bool typecheck          = false;
	bool control_flow_graph = false;
	bool run_mode           = false;
	bool dump_words_effects = false;
	bool output_colors      = true;

	void parse(int argc, char **argv);
} compiler_arguments;
