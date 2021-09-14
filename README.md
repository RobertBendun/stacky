# stacky

**WIP** stack-based compiled programming language with purpose of beeing a learning expirience.

## Example
Program printing sum of 3 `111`

```
111 dup dup + + .
```

## Language reference

Supported operations:
- `!` - if top = 1 then push 0 else push 1
- `+` - add 2 elements and push result
- `.` - print top to stdout
- `<int>` - push integer literal (currently only natural numbers up to 2^63-1) onto a stack
- `=` - if top = one before top then push 1 onto stack, else push 0
- `divmod` - divides one before top by top and pushes division result and modulo result
- `dup` - duplicate top element
- `swap` - swap top with before top stack element

## See also

- Tsoding [Porth](shttps://github.com/tsoding/porth)
- Classic stack based language [Forth](https://en.wikipedia.org/wiki/Forth_(programming_language))
- Modern stack based functional language [Factor](https://en.wikipedia.org/wiki/Factor_(programming_language))
