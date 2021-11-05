#include <string>
#include <cstdint>

namespace utf8
{
	enum : uint32_t
	{
		Rune_Max_1 = (1<<7)  - 1,
		Rune_Max_2 = (1<<11) - 1,
		Rune_Max_3 = (1<<16) - 1,
	};

	enum : unsigned char
	{
		T1    = 0b00000000,
		Tx    = 0b10000000,
		T2    = 0b11000000,
		T3    = 0b11100000,
		T4    = 0b11110000,
		T5    = 0b11111000,
		Maskx = 0b00111111,
	};

	// TODO think about better alternative
	auto encode_rune(uint32_t r) -> std::string
	{
		if (r < Rune_Max_1) return std::string(1, r);
		else if (r < Rune_Max_2) return {{
			char(T2 | char(r >> 6)),
			char(Tx | char(r) & Maskx)
		}};
		else if (r < Rune_Max_3) return {{
			char(T3 | char(r >> 12)),
			char(Tx | char(r >> 6) & Maskx),
			char(Tx | char(r) & Maskx)
		}};

		return {{
			char(T4 | char(r >> 18)),
			char(Tx | char(r >> 12) & Maskx),
			char(Tx | char(r >> 6) & Maskx),
			char(Tx | char(r) & Maskx)
		}};
	}
}
