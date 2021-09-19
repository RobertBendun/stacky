#include <cstdint>
#include <limits>
#include <cstddef>
#include <concepts>

namespace posix
{
	enum : int {
		Stdin  = 0,
		Stdout = 1,
	};

	enum Syscall : int
	{
		Brk   = 12,
		Exit  = 60,
		Write = 1,
	};

	static inline size_t syscall1(Syscall n, uint64_t a1)
	{
		size_t ret;
		asm volatile ("syscall" : "=a" (ret) : "a" (n), "D" (a1) : "rcx", "r11", "memory");
		return ret;
	}

	static inline size_t syscall3(Syscall n, uint64_t a1, uint64_t a2, uint64_t a3)
	{
		size_t ret;
		asm volatile ("syscall" : "=a" (ret) : "a" (n), "D" (a1), "S" (a2), "d" (a3) : "rcx", "r11", "memory");
		return ret;
	}

	static inline size_t syscall(Syscall syscall_id, std::convertible_to<uint64_t> auto const& ...args)
	{
		static_assert(sizeof...(args) == 1 || sizeof...(args) == 3);
		if constexpr (sizeof...(args) == 1) { return syscall1(syscall_id, static_cast<uint64_t>(args)...); }
		if constexpr (sizeof...(args) == 3) { return syscall3(syscall_id, static_cast<uint64_t>(args)...); }
	}
}

static inline unsigned digits10(std::uint64_t v)
{
	unsigned result = 1;
	for (;;) {
		if (v < 10u) return result;
		if (v < 100u) return result + 1;
		if (v < 1'000u) return result + 2;
		if (v < 10'000u) return result + 3;
		v /= 10'000u;
		result += 4;
	}
}

extern "C" {
	void _stacky_print_u64(std::uint64_t v)
	{
		char buffer[std::numeric_limits<decltype(v)>::digits10 + 1]{};
		unsigned const len = digits10(v);
		unsigned pos = len - 1;

		while (v >= 10) {
			std::uint64_t const q = v / 10;
			unsigned const r = v % 10;
			buffer[pos--] = '0' + r;
			v = q;
		}

		*buffer = v + '0';
		buffer[len] = '\n';
		posix::syscall(posix::Write, posix::Stdout, (uint64_t)buffer, (uint64_t)len + 1);
	}

	void _stacky_exit(int exit_code)
	{
		posix::syscall(posix::Exit, exit_code);
	}

	void _stacky_newline()
	{
		char nl = '\n';
		posix::syscall(posix::Write, posix::Stdout, (size_t)&nl, 1);
	}

	void _stacky_print_cstr(char const *cstr)
	{
		auto p = cstr;
		for (; *p != '\0'; ++p) {}
		posix::syscall(posix::Write, posix::Stdout, (size_t)cstr, p - cstr);
	}
}
