#include "stacky.hh"
#include <fstream>

#define Node_Prefix "Stacky_instr_"

void generate_control_flow_graph(Generation_Info const& geninfo, fs::path dot_path, std::string const& function)
{
	std::ofstream out(dot_path);
	ensure_fatal(bool(out), "Could not create file `{}`."_format(dot_path.c_str()));

	auto const& function_body = [&]() -> auto const& {
		if (function.empty())
			return geninfo.main;
		auto maybe_function = geninfo.words.find(function);
		ensure_fatal(maybe_function != std::end(geninfo.words), "Word `{}` has not been defined"_format(function));
		ensure_fatal(maybe_function->second.kind == Word::Kind::Function,
			"`{}` is not a function (control graph can only be generated for functions)"_format(function));
		return maybe_function->second.function_body;
	}();

	out << "digraph Program {\n";
	out << "	labelloc=\"t\";\n";

	if (function.empty())
		out << "	label=\"Control flow of a program\";\n";
	else
		out << "	label=\"Control flow of a function `" << function << "`\";\n";

	auto const link_next = [&](auto from, auto to, std::string_view style = {}) {
		while (to < function_body.size()) {
			if (function_body[to].kind == Operation::Kind::End) { to = function_body[to].jump; continue; }
			if (function_body[to].kind == Operation::Kind::Return)
				to = function_body.size();
			break;
		}
		out << "	" Node_Prefix << from << "	-> " Node_Prefix << to << ' ' << style << ";\n";
	};

	for (auto i = 0u; i < function_body.size(); ++i) {
		auto const& op = function_body[i];

		switch (op.kind) {
		case Operation::Kind::Push_Int:
			out << "	" Node_Prefix << i << " [label=" << op.ival << " shape=record];\n";
			link_next(i, i + 1);
			break;

		case Operation::Kind::Cast:
		case Operation::Kind::Intrinsic:
		case Operation::Kind::Push_Symbol:
			if (op.intrinsic == Intrinsic_Kind::Less)
				out << "	" Node_Prefix << i << "	[label=\"&lt;\" shape=record];\n";
			else
				out << "	" Node_Prefix << i << "	[label=" <<  std::quoted(op.token.sval) << " shape=record];\n";
			link_next(i, i + 1);
			break;

		case Operation::Kind::Call_Symbol:
			out << "	" Node_Prefix << i << " [label=\"CALL\\n" << op.sval << "\"];\n";
			link_next(i, i + 1);
			break;

		case Operation::Kind::If:
			out << "	" Node_Prefix << i << " [label=IF];\n";
			link_next(i, i + 1, "[label=T]");
			link_next(i, op.jump, "[label=F style=dashed]");
			break;

		case Operation::Kind::Do:
			out << "	" Node_Prefix << i << " [label=DO];\n";
			link_next(i, i + 1, "[label=T]");
			link_next(i, op.jump, "[label=F style=dashed]");
			break;

		case Operation::Kind::Else:
			out << "	" Node_Prefix << i << " [label=ELSE];\n";
			link_next(i, op.jump);
			break;

		case Operation::Kind::While:
			out << "	" Node_Prefix << i << "	[label=WHILE];\n";
			link_next(i, i + 1);
			break;

		case Operation::Kind::Return:
		case Operation::Kind::End:
			// ignore, link_next will link past return and end jumps since they are unconditional
			break;
		}
	}

	out << "	" Node_Prefix << function_body.size() << " [label=RETURN fontcolor=red];\n";
	out << "}\n";
}
