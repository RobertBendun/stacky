# stacky

**WIP** stack-based compiled programming language with purpose of beeing a learning expirience. **Currently Linux-only**

## Example
Program printing "HELLO" to stdout, that was transformed from "hello"

```
"io" include

Hello-Count 6 constant
Hello Hello-Count []byte

"hello"
0 while dup Hello-Count 1 - < do
	2dup + read8 32 -
	over Hello + swap write8
	1 +
end

Hello println
```

## Makefile
- `make install-nvim` - installs Stacky's syntax highlighting for Neovim
- `make stacky` - makes only compiler
- `make test` - runs all tests
- `make clean` - cleans all intermidiate files

## Requirements
- [NASM](https://nasm.us/)
- [LD](https://linux.die.net/man/1/ld)
- [GCC with C++ compiler](https://gcc.gnu.org/) for compiler and stdlib compilation
- [Boost Program\_Options](https://www.boost.org/)

For Arch-based users:
```shell
pacman -S gcc binutils nasm boost
```

## See also

- Tsoding [Porth](https://github.com/tsoding/porth)
- Classic stack based language [Forth](https://en.wikipedia.org/wiki/Forth_(programming_language))
- Modern stack based functional language [Factor](https://en.wikipedia.org/wiki/Factor_(programming_language))
