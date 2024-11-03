#include "errors.hh"
#include "stacky.hh"

#include "utilities.cc"
#include <algorithm>
#include <iterator>
#include <vector>
#include <utility>


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

		if (removed_words > 0)   verbose(std::format("Removed {} functions and arrays", removed_words));
		if (removed_strings > 0) verbose(std::format("Removed {} strings", removed_strings));

		return removed_words + removed_strings;
	}

	auto optimize_comptime_known_conditions([[maybe_unused]] Generation_Info &geninfo, std::vector<Operation> &function_body) -> bool
	{
		bool done_something = false;

		auto const remap = [&function_body](unsigned start, unsigned end, unsigned amount, auto const& ...msg)
		{
			for (auto& op : function_body)
				if (op.jump >= start && op.jump <= end) {
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

	auto constant_folding([[maybe_unused]] Generation_Info &geninfo, std::vector<Operation> &function_body) -> bool
	{
		bool done_something = false;

		std::optional<std::size_t> foldable_start = std::nullopt;
		std::vector<std::int64_t> stack;

		enum Action { Continue, Break };
		auto const finish_constant_folding = [&](std::size_t unhandled_operation_id) -> Action {
			if (!foldable_start.has_value() || *foldable_start + 1 == unhandled_operation_id) {
				foldable_start = std::nullopt;
				stack = {};
				return Continue;
			}
			std::span to_optimize(function_body);
			to_optimize = to_optimize.subspan(*foldable_start, unhandled_operation_id - *foldable_start);

			auto const stack_the_same_as_operations = std::ranges::equal(to_optimize, stack,
					[](Operation const& op, std::int64_t value) { return op.kind == Operation::Kind::Push_Int && int64_t(op.ival) == value; });

			if (stack_the_same_as_operations) {
				foldable_start = std::nullopt;
				stack = {};
				return Continue;
			}

			done_something = true;
			function_body.erase(function_body.begin() + *foldable_start, function_body.begin() + unhandled_operation_id);
			std::ranges::transform(stack, std::inserter(function_body, function_body.begin() + *foldable_start),
					[](std::int64_t value) {
						return Operation {
							.kind = Operation::Kind::Push_Int,
							.ival = std::uint64_t(value),
						};
					}
			);

			auto const delta = std::ssize(stack) - std::ssize(to_optimize);
			for (Operation &potential_jump : function_body) {
				if (potential_jump.jump != Operation::Empty_Jump && potential_jump.jump > *foldable_start) {
					potential_jump.jump += delta;
				}
			}

			return Break;
		};

		for (auto i = 0u; i < function_body.size(); ++i) {
			auto const& op = function_body[i];

			if (!foldable_start.has_value()) {
				if (op.kind == Operation::Kind::Push_Int) {
					foldable_start = i;
				} else {
					continue;
				}
			}

			switch (op.kind) {
			case Operation::Kind::Push_Symbol:
			case Operation::Kind::Call_Symbol:
			case Operation::Kind::Cast:
			case Operation::Kind::End:
			case Operation::Kind::If:
			case Operation::Kind::Else:
			case Operation::Kind::While:
			case Operation::Kind::Do:
			case Operation::Kind::Return:
				switch (finish_constant_folding(i)) {
				case Continue: continue;
				case Break: return done_something;
				}
				break;

			case Operation::Kind::Push_Int:
				stack.push_back(op.ival);
				continue;

			case Operation::Kind::Intrinsic:
				switch (op.intrinsic) {
#define Math(Name, Op) \
					case Name: \
						{ \
							if (stack.size() < 2) switch (finish_constant_folding(i)) { \
								case Continue: continue; \
								case Break: return done_something; \
							} \
							auto const a = stack.back(); stack.pop_back(); \
							auto const b = stack.back(); stack.pop_back(); \
							stack.push_back(b Op a); \
						} \
						break;
					Math(Intrinsic_Kind::Add, +)
					Math(Intrinsic_Kind::Subtract, -)
					Math(Intrinsic_Kind::Equal, ==)
					Math(Intrinsic_Kind::Bitwise_And, &)
					Math(Intrinsic_Kind::Bitwise_Or, |)
					Math(Intrinsic_Kind::Bitwise_Xor, ^)
					Math(Intrinsic_Kind::Div, /)
					Math(Intrinsic_Kind::Greater, >)
					Math(Intrinsic_Kind::Greater_Eq, >=)
					Math(Intrinsic_Kind::Left_Shift, <<)
					Math(Intrinsic_Kind::Less, <)
					Math(Intrinsic_Kind::Less_Eq, <=)
					Math(Intrinsic_Kind::Mod, %)
					Math(Intrinsic_Kind::Mul, *)
					Math(Intrinsic_Kind::Not_Equal, !=)
					Math(Intrinsic_Kind::Right_Shift, >>)
#undef Math

					case Intrinsic_Kind::Drop:
						{
							if (stack.size() < 1) switch (finish_constant_folding(i)) {
								case Continue: continue;
								case Break: return done_something;
							}
							stack.pop_back();
						}
						break;

					case Intrinsic_Kind::Dup:
						{
							if (stack.size() < 1) switch (finish_constant_folding(i)) {
								case Continue: continue;
								case Break: return done_something;
							}
							stack.push_back(stack.back());
						}
						break;

					case Intrinsic_Kind::Two_Dup:
						{
							if (stack.size() < 2) switch (finish_constant_folding(i)) {
								case Continue: continue;
								case Break: return done_something;
							}
							stack.push_back(stack[stack.size()-2]);
							stack.push_back(stack[stack.size()-2]);
						}
						break;

					case Intrinsic_Kind::Max:
						{
							if (stack.size() < 2) switch (finish_constant_folding(i)) {
								case Continue: continue;
								case Break: return done_something;
							}
							auto const a = stack.back(); stack.pop_back();
							auto const b = stack.back(); stack.pop_back();
							stack.push_back(std::max(a, b));
						}
						break;

					case Intrinsic_Kind::Min:
						{
							if (stack.size() < 2) switch (finish_constant_folding(i)) {
								case Continue: continue;
								case Break: return done_something;
							}
							auto const a = stack.back(); stack.pop_back();
							auto const b = stack.back(); stack.pop_back();
							stack.push_back(std::min(a, b));
						}
						break;

					case Intrinsic_Kind::Over: // a b -- a b a
						{
							if (stack.size() < 2) switch (finish_constant_folding(i)) {
								case Continue: continue;
								case Break: return done_something;
							}
							stack.push_back(stack[stack.size()-2]);
						}
						break;


					case Intrinsic_Kind::Rot: // a b c -- b c a
						{
							if (stack.size() < 3) switch (finish_constant_folding(i)) {
								case Continue: continue;
								case Break: return done_something;
							}
							auto &c = stack[stack.size()-1]; // c -> a
							auto &b = stack[stack.size()-2]; // b -> c
							auto &a = stack[stack.size()-3]; // a -> b
							a = std::exchange(b, std::exchange(c, a));
						}
						break;

					case Intrinsic_Kind::Swap: // a b -- b a
						{
							if (stack.size() < 2) switch (finish_constant_folding(i)) {
								case Continue: continue;
								case Break: return done_something;
							}
							std::swap(stack[stack.size()-1], stack[stack.size()-2]);
						}
						break;

					case Intrinsic_Kind::Tuck: // a b -- b a b
						{
							if (stack.size() < 2) switch (finish_constant_folding(i)) {
								case Continue: continue;
								case Break: return done_something;
							}
							stack.push_back(stack.back());
							std::swap(stack[stack.size()-3], stack[stack.size()-2]);
						}
						break;

					case Intrinsic_Kind::Two_Drop:
					case Intrinsic_Kind::Two_Over:
					case Intrinsic_Kind::Two_Swap:
					case Intrinsic_Kind::Div_Mod:
					case Intrinsic_Kind::Boolean_Or:
					case Intrinsic_Kind::Boolean_And:
					case Intrinsic_Kind::Boolean_Negate:
					case Intrinsic_Kind::Load:
					case Intrinsic_Kind::Store:
					case Intrinsic_Kind::Top:
					case Intrinsic_Kind::Call:
					case Intrinsic_Kind::Random32:
					case Intrinsic_Kind::Random64:
					case Intrinsic_Kind::Syscall:
						switch (finish_constant_folding(i)) {
							case Continue: continue;
							case Break: return done_something;
						}
				}
				continue;
			}
		}

		finish_constant_folding(function_body.size());

		return done_something;
	}

	void optimize(Generation_Info &geninfo)
	{
		while (remove_unused_words_and_strings(geninfo)
			|| for_all_functions(geninfo, optimize_comptime_known_conditions)
			|| for_all_functions(geninfo, constant_folding))
		{
		}
	}
}
