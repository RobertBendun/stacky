# stacky

**WIP** stack-based compiled programming language with purpose of beeing a learning expirience.

## Example
Program printing sum of 3 `111`

```
111 dup dup + + .
```

## Language reference

Supported operations:
- `+` - add 2 elements and push result
- `.` - print top to stdout
- `<int>` - push integer literal (currently only natural numbers up to 2^63-1) onto a stack
- `dup` - duplicate top element

## See also

- Tsoding [Porth](https://github.com/tsoding/porth)
- Classic stack based language [Forth](https://en.wikipedia.org/wiki/Forth_(programming_language))
- Modern stack based functional language [Factor](https://en.wikipedia.org/wiki/Factor_(programming_language))
