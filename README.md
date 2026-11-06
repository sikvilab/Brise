# Brise

**Brise** is a small, human-readable interpreted programming language written in C, with a C++ command-line launcher. Source files use the `.bri` extension.

> Current development target: **v0.5.0 (development branch)**. Brise is experimental and is not yet a stable production runtime.

## Features

- Dynamic values: numbers, strings, booleans
- Variables and arithmetic: `set:`, `calc:`, `+ - * / % ^`
- Comparisons and logical conditions: `== != < > <= >=`, `and`, `or`, `not`
- Conditional execution: `if:` with optional `else:`
- Bounded `while:` loops (10,000 iterations per loop)
- String functions: `len`, `str`, `type`, `upper`, `lower`, `trim`, `contains`, `starts_with`, `ends_with`
- Math functions: `abs`, `sqrt`, `floor`, `ceil`, `round`, `min`, `max`, `pow`; constants `pi` and `e`
- Lists and `Say to everyone:`
- User-defined command blocks with `Command:`
- File inclusion, file read/write, console input, random values
- Optional SDL2 Graph module and a basic HTTP client

## Build and run

Linux / macOS (GCC or Clang with C++17):

```sh
g++ -std=c++17 main.cpp brise_runtime.c -I. -lm -o brise
./brise examples/hello_world.bri
```

Windows (MinGW):

```powershell
g++ -std=c++17 main.cpp brise_runtime.c -I. -o brise.exe
.\brise.exe examples/hello_world.bri
```

## Language tour

### Variables, output, arithmetic

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
assert: len(title) == 14
assert: starts_with(title, "BRI")
assert: contains(title, "LANG")
calc:radius = 3
calc:area = pi * pow(radius, 2)
say:"Area: (area)"
```

String case conversion is ASCII-only in this development version.

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
while: counter < 3 (calc:counter = counter + 1)
say:"Counter: (counter)"
```

The interpreter currently reads one statement per source line, so control-flow action blocks must remain on the same line. A loop stops when its condition becomes false. To prevent accidental infinite loops, each `while:` execution is limited to 10,000 iterations.

### Lists and custom commands

```brise
List: tools (Logic, Speed, Simplicity)
Say to everyone:(say:"Feature: (item)")
Command: greet (say:"Welcome to Brise!")
greet
```

## Known limitations

Brise is still under active development. The current implementation does not yet provide lexical scopes, parameters or return values for user-defined commands, a package manager, a formal module system, a debugger, or a stable bytecode target. Error recovery and source diagnostics need more work.

The built-in HTTP client currently supports plain HTTP only; do not send sensitive data through it. File operations are relative to the source file but are not sandboxed. The legacy BTC compiler supports only a subset of Brise syntax and is not feature-equivalent to the interpreter.

## Tests

Run the smoke tests after building:

```sh
./brise tests/smoke.bri
```

GitHub Actions is configured to build and run these tests on Linux and Windows.

## License

MIT. See [LICENSE](LICENSE).
