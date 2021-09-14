#include <cstdint>
#include <limits>
#include <cstddef>

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

	static inline size_t syscall1(Syscall n, size_t a1)
	{
		size_t ret;
		asm volatile ("syscall" : "=a" (ret) : "a" (n), "D" (a1) : "rcx", "r11", "memory");
		return ret;
	}

	static inline size_t syscall3(Syscall n, size_t a1, size_t a2, size_t a3)
	{
		size_t ret;
		asm volatile ("syscall" : "=a" (ret) : "a" (n), "D" (a1), "S" (a2), "d" (a3) : "rcx", "r11", "memory");
		return ret;
	}

	template<typename ...T>
	static inline size_t syscall(Syscall syscall_id, T const& ...args)
	{
		static_assert(sizeof...(args) == 1 || sizeof...(args) == 3);
		if constexpr (sizeof...(args) == 1) { return syscall1(syscall_id, static_cast<size_t>(args)...); }
		if constexpr (sizeof...(args) == 3) { return syscall3(syscall_id, static_cast<size_t>(args)...); }
	}
}

static inline unsigned digits10(std::uint64_t v)
{
	unsigned result = 1;
	for (;;) {
		if (v < 10u) return result;
		if (v < 100u) return result + 1;
		if (v < 1000u) return result + 2;
		if (v < 10000u) return result + 3;
		v /= 10000u;
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
			unsigned const q = v / 10;
			unsigned const r = v % 10;
			buffer[pos--] = '0' + r;
			v = q;
		}

		*buffer = v + '0';
		buffer[len] = '\n';
		posix::syscall(posix::Write, posix::Stdout, (size_t)buffer, (size_t)len + 1);
	}

	void _stacky_exit(int exit_code)
	{
		posix::syscall(posix::Exit, exit_code);
	}

	void _stacky_print_cstr(char const *cstr)
	{
		auto p = cstr;
		for (; *p != '\0'; ++p) {}
		posix::syscall(posix::Write, posix::Stdout, (size_t)cstr, p - cstr);
	}
}
