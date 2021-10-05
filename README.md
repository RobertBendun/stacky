# stacky

**WIP** stack-based compiled concatenative programming language.

Currently it's somewhere between [B](https://en.wikipedia.org/wiki/B_(programming_language)) and [C](https://en.wikipedia.org/wiki/C_(programming_language)) programming languages in universe where B was stack based concatenative language.

## Goals

In mostly random order:
- [x] Easy to expand with new keywords and intrinsics
- [x] Turing complete
- [ ] Static type system (currently beeing developed)
- [ ] Ability to target OS-less enviroment (and maybe even write bootloader in pure Stacky)
- [ ] Compile time code execution
- [ ] Multiplatform (at least supporting Linux ARM and x86\_64)
- [ ] Ability to interact with C code (but not depending on it)

with some basic optimizations and good error reporting.

## Example
### hello, world
```
"io" import

"hello, world" println
```
### Program printing "HELLO" to stdout, that was transformed from "hello"

```
"io" import

Hello-Count 6 constant
Hello Hello-Count []byte

"hello"
0 while dup Hello-Count 1 - < do
	2dup + load8 'a' 'A' - -
	over Hello + swap store8
	1 +
end

Hello println
```

### Print current date
```
"io"   import
"time" import

"Date is " print

2cell-print fun
	dup 10 < if "0" print end
	print-int
end

now # returns seconds in Unix time
date # returns year, month, day, hours, minutes
	print-int "-" print
	2cell-print "-" print
	2cell-print " " print
	2cell-print ":" print
	2cell-print
```

## Language reference

### Control flow

#### if ... else ... end

Consumes top of the stack. If value was nonzero, executes if branch, otherwise else branch.
```
"io" import
10 if "value is 10!" else "value is not 10 :c" end println
```

#### while ... do ... end

While condition part (between `while` and `do`) is nonzero, executes repeatadly loop part (between `do` and `end`).

```
# Print numbers from 0 to 10
"io" import

0 while dup 11 = ! do
	dup .
	1 +
end
```

### Functions

`<name> fun <code> end` creates function with given name and code block.

```
"io" import

say-hello fun "Hi, " print print "!" println end

"Mark" say-hello # prints "Hi, Mark!"
```

#### Address of functions

`&<name>` puts `<name>` address onto stack, for example: `&foo`

#### Call operator

`call` calls function pointed by address on top of the stack.

```
"Mark" &say-hello call
```

### Standard library

#### algorithm
- `uniform32` - `(a b -- n)` returns random integer `n` in range `[a, b]`

#### io
- `nl` - prints newline to stdout
- `print` - `(pointer --)` - prints null terminated string to stdout
- `println` - `(pointer --)` - prints null terminated string to stdout and after it newline
- `.` - `(uint --)` - print unsigned integer to stdout with a newline at the end

#### limits
- `Max_Digits10` - `(-- uint)` - returns maximum number of decimal digits that unsigned integer may have

#### time
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

#### posix

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

- Tsoding [Porth](https://github.com/tsoding/porth) (main inspiration for starting this project)
- Classic stack based language [Forth](https://en.wikipedia.org/wiki/Forth_(programming_language))
- Modern stack based functional language [Factor](https://en.wikipedia.org/wiki/Factor_(programming_language))
