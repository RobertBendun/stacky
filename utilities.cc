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
