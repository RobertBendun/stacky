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
	while (count-- > 0)
		if (auto const result = std::find(found, end, v); result != end)
			found = result;
		else
			break;
	return found;
}
