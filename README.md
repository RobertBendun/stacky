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

## Language reference

### Standard library

#### io.stacky
- `nl` - prints newline to stdout
- `print` - `(pointer --)` - prints null terminated string to stdout
- `println` - `(pointer --)` - prints null terminated string to stdout and after it newline
- `.` - `(uint --)` - print unsigned integer to stdout with a newline at the end

#### limits.stacky
- `Max_Digits10` - `(-- uint)` - returns maximum number of decimal digits that unsigned integer may have

#### time.stacky
- `now` - `(-- seconds)` - returns number of seconds since [Unix time](https://en.wikipedia.org/wiki/Unix_time)
- `sleep` - `(seconds --)` - sleeps given number of seconds
- `date` - `(seconds -- year month day hours minutes)` - decomposes seconds since Unix time into human readable form
- `+minutes` - `(minutes seconds -- seconds)` - adds minutes to seconds
- `+hours` - `(hours seconds -- seconds)` - adds hours to seconds
- `+days` - `(days seconds -- seconds)` - adds days to seconds
- `+weeks` - `(weekds seconds -- seconds)` - adds weeks to seconds

- `-minutes` - `(minutes seconds -- seconds)` - subtracts minutes from seconds
- `-hours` - `(hours seconds -- seconds)` - subtracts hours from seconds
- `-days` - `(days seconds -- seconds)` - subtracts days from seconds
- `-weeks` - `(weekds seconds -- seconds)` - subtracts weeks from seconds

#### posix.stacky

Contains definition of constants related to POSIX compatible operating systems.
- `SYS_<syscall>` - constants holding syscall numbers, like `SYS_exit` = `60`
- `stdin`, `stdout`, `stderr`
- `CLOCK_<type>` - definition of clocks constants for `clock_gettime` syscall

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
