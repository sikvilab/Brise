# Brise

**Brise** is a small, human-readable interpreted programming language written in C, with a C++ command-line launcher. Source files use the `.bri` extension.

> Current development target: **v0.5.0 (development branch)**. Brise is an experimental language, not yet a stable production runtime.

## Features

- Dynamic values: numbers, strings, booleans
- Variables and arithmetic expressions: `set:`, `calc:`, `+ - * / % ^`
- Comparisons and logical conditions: `== != < > <= >=`, `and`, `or`, `not`
- Conditional execution: `if:` with optional `else:`
- Bounded `while:` loops (10,000 iterations per loop as a safety limit)
- Built-in string helpers: `len`, `str`, `type`, `upper`, `lower`, `trim`, `contains`, `starts_with`, `ends_with`
- Built-in math helpers: `abs`, `sqrt`, `floor`, `ceil`, `round`, `min`, `max`, `pow`, plus constants `pi` and `e`
- Lists and `Say to everyone:`
- User-defined command blocks with `Command:`
- File inclusion, file read/write, console input, random values
- Optional SDL2 Graph module and basic HTTP client

## Quick start

Build with a C compiler and C++17 compiler:

```sh
g++ -std=c++17 main.cpp brise_runtime.c -I. -lm -o brise
```

On Windows with MinGW:

```sh
g++ -std=c++17 main.cpp brise_runtime.c -I. -o brise.exe
```

Run a program:

```sh
./brise examples/hello_world.bri
```

## Language tour

### Variables, output, and arithmetic

```brise
set:name = "Brise"
set:price = 120
set:discount = 15
calc:final_price = price - discount * 2
say:"Hello, (name)!"
say:final_price
```

### Built-in functions

```brise
set:title = upper(trim("  brise language  "))
set:message = "hello"
assert: len(title) == 14
assert: starts_with(message, "he")
assert: contains(message, "ell")
calc:radius = 3
calc:area = pi * pow(radius, 2)
say:"Area: (area)"
```

Built-in functions currently accept up to eight arguments syntactically; each function validates its own required argument count. String case conversion is ASCII-only in this release.

### Conditions

```brise
set:score = 87
if: score >= 80 and score < 90 (say:"Good result")
if: score < 50 or score == 0 (say:"Needs work") else: (say:"Keep going")
if: not (score < 80) (say:"Passed")
```

### Loops

```brise
set:counter = 0
while: counter < 3 (
    say:counter
    calc:counter = counter + 1
)
```

A loop stops when its condition becomes false. To prevent accidental infinite loops, one `while:` execution is limited to 10,000 iterations.

### Lists and custom commands

```brise
List: tools (Logic, Speed, Simplicity)
Say to everyone:(say:"Feature: (item)")
Command: greet (say:"Welcome to Brise!")
greet
```

## Current limitations

Brise is still under active development. The current implementation is line-oriented and does not yet provide lexical scopes, parameters/return values for user-defined commands, a package manager, a formal module system, a debugger, or a stable bytecode/compiled target. Error recovery and source diagnostics need further work. The built-in HTTP client currently supports plain HTTP only; do not send sensitive data through it. File operations are relative to the source file but are not yet sandboxed.

The legacy BTC compiler supports only a subset of Brise syntax and must not be considered feature-equivalent to the interpreter.

## Tests

The repository includes a smoke-test program under `tests/`. The GitHub Actions workflow builds the interpreter on Linux and Windows and runs the interpreter test program.

## License

MIT. See [LICENSE](LICENSE).
