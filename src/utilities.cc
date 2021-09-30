#include <functional>

constexpr auto count_args(auto const& ...args) noexcept -> unsigned
{
	return (((void)args, 1) + ...);
}

template<typename ...T>
constexpr auto sorted_array_of_tuples(T&& ...args) {
	auto array = std::array { std::forward<T>(args)... };
	std::sort(std::begin(array), std::end(array), [](auto const& lhs, auto const& rhs) {
		return std::get<0>(lhs) < std::get<0>(rhs);
	});
	return array;
}

constexpr auto enum2int(auto value)
{
	if constexpr (std::is_enum_v<decltype(value)>)
		return static_cast<std::underlying_type_t<decltype(value)>>(value);
	else
		return value;
}

constexpr auto linear(auto difference, auto fst, auto snd, auto ...rest) -> bool
{
	if constexpr (sizeof...(rest) >= 1)
		return enum2int(snd) - enum2int(fst) == difference && linear(difference, snd, rest...);
	else
		return enum2int(snd) - enum2int(fst) == difference;
}

auto run_command(bool quiet, auto const& ...messages) -> int
{
	std::stringstream ss;
	(ss << ... << messages);
	auto command = std::move(ss).str();
	if (!quiet) std::cout << "[CMD] " << command << '\n';
	return std::system(command.c_str());
}

template<typename It>
constexpr auto find_nth(It begin, It end, std::integral auto count, std::equality_comparable_with<std::iter_value_t<It>> auto const& v) -> It
{
	auto found = begin;
	for (; count-- > 0; ++found)
		if (auto const result = std::find(found, end, v); result != end)
			found = result;
		else
			break;
	return found;
}

template<typename It>
constexpr auto search_nth(It begin, It end, std::integral auto count, auto const& needle) -> It
{
	auto found = begin;
	auto const s = std::boyer_moore_searcher(needle.cbegin(), needle.cend());
	for (; count-- > 0; found += needle.size())
		if (auto const result = std::search(found, end, s); result != end)
			found = result;
		else
			break;
	return found;
}

template<typename F, typename B, typename... A>
static decltype(auto) callv(F&& func, B&& def, A&&... args)
{
	if constexpr (std::is_invocable_r_v<B, F, A...>) {
		return std::forward<F>(func)(std::forward<A>(args)...);
	} else {
		static_assert(std::is_void_v<std::invoke_result_t<F, A...>>);
		std::forward<F>(func)(std::forward<A>(args)...);
		return std::forward<B>(def);
	}
}

constexpr auto max_digits64(unsigned Base)
{
	switch (Base) {
	case 2:  return 64u;
	case 4:  return 32u;
	case 6:  return 25u;
	case 8:  return 22u;
	case 10: return 20u;
	case 16: return 16u;
	default:
		throw std::invalid_argument("max_digits64 does not support this base!");
	}
}

constexpr auto pow(std::uint64_t x, std::uint64_t n) -> std::uint64_t
{
	std::uint64_t r = 1;
	for (; n > 0; --n) r *= x;
	return r;
}
