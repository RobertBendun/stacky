template<typename ...T>
constexpr auto count_args(T const& ...args) noexcept -> unsigned
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
