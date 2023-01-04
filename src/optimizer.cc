#include "stacky.hh"

#include "utilities.cc"

namespace optimizer
{
	auto for_all_functions(Generation_Info &geninfo, auto &&iteration)
	{
		bool result = callv(iteration, false, geninfo, geninfo.main);
		for (auto &[name, word] : geninfo.words)
			if (word.kind == Word::Kind::Function)
				result |= callv(iteration, false, geninfo, word.function_body);
		return result;
	}

	void remove_unused_words_and_strings(
			Generation_Info &geninfo,
			std::vector<Operation> const& function_body,
			std::unordered_set<std::uint64_t> &used_words,
			std::unordered_set<std::uint64_t> &used_strings)
	{
		for (auto const& op : function_body) {
			if (op.kind != Operation::Kind::Push_Symbol && op.kind != Operation::Kind::Call_Symbol)
				continue;

			if (op.token.kind == Token::Kind::String) {
				used_strings.insert(op.token.ival);
			} else {
				if (used_words.contains(op.ival))
					continue;

				used_words.insert(op.ival);
				auto const word = std::find_if(std::cbegin(geninfo.words), std::cend(geninfo.words), [word_id = op.ival](auto const &entry)
				{
					return entry.second.id == word_id;
				});
				assert(word != std::cend(geninfo.words));
				if (word->second.kind != Word::Kind::Function)
					continue;
				remove_unused_words_and_strings(geninfo, word->second.function_body, used_words, used_strings);
			}
		}
	}

	auto remove_unused_words_and_strings(Generation_Info &geninfo) -> bool
	{
		std::unordered_set<std::uint64_t> used_words;
		std::unordered_set<std::uint64_t> used_strings;

		remove_unused_words_and_strings(geninfo, geninfo.main, used_words, used_strings);
		auto const removed_words = std::erase_if(geninfo.words, [&](auto const& entry) {
			return (entry.second.kind == Word::Kind::Function || entry.second.kind == Word::Kind::Array) && !used_words.contains(entry.second.id);
		});

		auto const removed_strings = std::erase_if(geninfo.strings, [&](auto const& entry) {
			return !used_strings.contains(entry.second);
		});

		if (removed_words > 0)   verbose(fmt::format("Removed {} functions and arrays", removed_words));
		if (removed_strings > 0) verbose(fmt::format("Removed {} strings", removed_strings));

		return removed_words + removed_strings;
	}

	auto optimize_comptime_known_conditions([[maybe_unused]] Generation_Info &geninfo, std::vector<Operation> &function_body) -> bool
	{
		bool done_something = false;

		auto const remap = [&function_body](unsigned start, unsigned end, unsigned amount, auto const& ...msg)
		{
			for (auto& op : function_body)
				if (op.jump >= start && op.jump <= end) {
					// (std::cout << ... << msg) << ((sizeof...(msg)==0)+" ") << op.jump << " -> " << (op.jump - amount) << '\n';
					op.jump -= amount;
				}
		};

		for (auto branch_op = 1u; branch_op < function_body.size(); ++branch_op) {
			auto const condition_op = branch_op - 1;

			auto const& condition = function_body[condition_op];
			auto const& branch = function_body[branch_op];
			if (condition.kind != Operation::Kind::Push_Int || branch.kind != Operation::Kind::Do && branch.kind != Operation::Kind::If)
				continue;
			done_something = true;

			switch (branch.kind) {
			case Operation::Kind::Do:
				{
					auto const condition_ival = condition.ival;
					auto const end_op = branch.jump-1;

					if (condition.ival != 0) {
						assert(function_body[branch.jump-1].kind == Operation::Kind::End);

						if (std::cbegin(function_body) + branch.jump + 1 != std::cend(function_body)) {
							warning(function_body[branch.jump].location, "Dead code: Loop is infinite");
							info(function_body[condition_op-1].location, "Infinite loop introduced here.");
						}

						function_body.erase(std::cbegin(function_body) + branch.jump, std::end(function_body));
						function_body.erase(std::cbegin(function_body) + condition_op, std::cbegin(function_body) + branch_op + 1);
						remap(branch_op+1, end_op, 3);
						verbose(branch.token, "Optimizing infinite loop (condition is always true)");
					} else {
						function_body.erase(std::cbegin(function_body) + condition_op, std::cbegin(function_body) + branch.jump);
						remap(end_op, -1, end_op - branch_op + 3);
						verbose(branch.token, "Optimizing never executing loop (condition is always false)");
					}

					// find and remove `while`
					auto while_op = unsigned(condition_op-1);
					while (while_op < function_body.size() &&
							function_body[while_op].kind != Operation::Kind::While && function_body[while_op].jump != branch_op)
						--while_op;
					function_body.erase(std::cbegin(function_body) + while_op);
					if (condition_ival == 0)
						remap(while_op, branch_op, 1);
				}
				break;
			case Operation::Kind::If:
				{
					auto const end_or_else = branch.jump-1;
					auto const else_op = end_or_else;
					auto const end = function_body[end_or_else].kind == Operation::Kind::Else ? function_body[end_or_else].jump-1 : end_or_else;

					if (condition.ival != 0) {
						// Do `if` has else branch? If yes, remove it. Otherwise remove unnesesary end operation
						if (end != else_op) {
							function_body.erase(std::cbegin(function_body) + else_op, std::cbegin(function_body) + end + 2);
							remap(end, -1, end - else_op + 1);
						} else {
							function_body.erase(std::cbegin(function_body) + else_op);
							remap(end, -1, 1);
						}
						remap(branch_op, end_or_else, 2); // `if` and condition
						function_body.erase(std::cbegin(function_body) + condition_op, std::cbegin(function_body) + branch_op + 1);
						verbose(branch.token, "Optimizing always then `if` (conditions is always true)");
					} else {
						// Do `if` has else branch? If yes, remove `end` operation from it
						if (end != else_op) {
							function_body.erase(std::cbegin(function_body) + end + 1);
						}

						// remove then branch and condition
						function_body.erase(std::cbegin(function_body) + condition_op, std::cbegin(function_body) + else_op + 1);
						remap(end_or_else, -1, 3 + (end_or_else != end));
						verbose(branch.token, "Optimizing always else `if` (condition is always false)");
					}
				}
				break;
			default:
				unreachable("We check earlier for possible values");
			}

			branch_op -= 1;
		}

		return done_something;
	}

	void optimize(Generation_Info &geninfo)
	{
		while (remove_unused_words_and_strings(geninfo)
			|| for_all_functions(geninfo, optimize_comptime_known_conditions))
		{
		}
	}
}
