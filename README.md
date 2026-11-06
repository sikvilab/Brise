# Brise

**Brise** is a small, human-readable interpreted programming language written in C, with a C++ command-line launcher. Source files use the .bri extension.

> Current development target: **v0.5.0 (development branch)**. Brise is experimental and is not yet a stable production runtime.

## Features

- Dynamic values: numbers, strings, booleans
- Variables and arithmetic: set:, calc:, + - * / % ^
- Comparisons and logical conditions: == != < > <= >=, and, or, not
- Conditional execution: if: with optional else:
- Bounded while: loops (10,000 iterations per loop)
- String functions: len, str, type, upper, lower, trim, contains, starts_with, ends_with
- Math functions: abs, sqrt, floor, ceil, round, trunc, pow, mod, min, max, clamp, lerp, round_to
- Trigonometry and logarithms: sin, cos, tan, asin, acos, atan, atan2, sinh, cosh, tanh, exp, log, ln, log10, log2, cbrt
- Number utilities: sign, factorial, gcd, lcm, hypot, is_prime, is_finite, is_nan, deg_to_rad, rad_to_deg
- Constants: pi, tau, e, sqrt2, ln2, ln10
- Lists, custom command blocks, file inclusion, console input and random values
- Basic and advanced matrices: create, set, identity, add, subtract, multiply, transpose, scale, determinant, inverse
- Manually managed numeric memory blocks with indexed cells and explicit `free:` deallocation (shared 1,048,576-cell runtime limit)
- File I/O restricted to regular files in the script's own directory (no path separators or symlinks; 4 MiB limit)
- Netwe supports HTTP and HTTPS; HTTPS validates the server certificate and hostname
- Optional SDL2 Graph module

## Build and run

### Linux (GCC, C++17, OpenSSL 1.1.1+)

Install prerequisites on Debian/Ubuntu:

~~~sh
sudo apt-get install g++ gcc libssl-dev
~~~

Build and run:

~~~sh
g++ -std=c++17 main.cpp brise_runtime.c -I. -lssl -lcrypto -ldl -lm -o brise
./brise examples/hello_world.bri
~~~

### Windows (MinGW)

HTTPS uses the Windows WinHTTP API and Windows certificate store.

~~~powershell
g++ -std=c++17 main.cpp brise_runtime.c -I. -lwinhttp -o brise.exe
.\brise.exe examples/hello_world.bri
~~~

## Language tour

### Variables, output and arithmetic

~~~brise
set:name = "Brise"
set:price = 120
set:discount = 15
calc:final_price = price - discount * 2
say:"Hello, (name)!"
say:final_price
~~~

### Scientific notation and mathematical functions

~~~brise
calc:mass = 1.25e2
calc:area = pi * pow(3, 2)
calc:angle = deg_to_rad(90)
calc:height = sin(angle) * 10
calc:rounded = round_to(3.14159, 3)
calc:bounded = clamp(height, 0, 10)
say:"Area: (area), height: (height)"
assert: factorial(5) == 120
assert: gcd(54, 24) == 6
assert: is_prime(97)
~~~

See examples/math_showcase.bri for a longer math tour and examples/matrix_memory_demo.bri for matrix and memory operations.

### Conditions

~~~brise
set:score = 87
if: score >= 80 and score < 90 (say:"Good result")
if: score < 50 or score == 0 (say:"Needs work") else: (say:"Keep going")
if: not (score < 80) (say:"Passed")
~~~

### Loops

~~~brise
set:counter = 0
while: counter < 3 (calc:counter = counter + 1)
say:"Counter: (counter)"
~~~

The interpreter currently reads one statement per source line, so control-flow action blocks must remain on the same line. Each while: execution is limited to 10,000 iterations.

### Matrices

Matrix cells store finite double-precision numbers. Matrices are zero-initialized, use zero-based row and column indexes, and share a 1,048,576-cell (8 MiB) allocation limit with manual memory blocks.

~~~brise
// Basic matrix operations
Matrix:create(A, 2, 2)
Matrix:set(A, 0, 0, 1)
Matrix:set(A, 0, 1, 2)
Matrix:set(A, 1, 0, 3)
Matrix:set(A, 1, 1, 4)
Matrix:identity(I, 2)
Matrix:add(S, A, I)
Matrix:print(S)

// Advanced linear algebra
Matrix:multiply(P, A, I)
Matrix:transpose(T, A)
Matrix:det(A, determinant)
calc:det_again = mdet("A")
Matrix:inverse(A_inverse, A)
calc:first_cell = mget("A", 0, 0)
assert: mrows("A") == 2
assert: mcols("A") == 2
~~~

Available advanced operations include `add`, `sub`/`subtract`, `mul`/`multiply`, `copy`, `transpose`, `scale`, `det`/`determinant`, and `inverse`/`inv`. Matrix query functions are `mget(name,row,col)`, `mrows(name)`, `mcols(name)`, `mdet(name)`, and `mexists(name)`. Names passed to query functions must be strings. Matrix command names may be unquoted identifiers.

### Managed memory

`Memory:alloc` reserves a named block of numeric cells, `Memory:set` writes a cell, and `mem_get` reads it. `free:` releases a matrix and/or memory block by name. Indexes start at zero; use-after-free and out-of-range access produce runtime errors.

~~~brise
Memory:alloc(weights, 4)
Memory:set(weights, 0, 0.25)
Memory:set(weights, 1, -0.5)
calc:first_weight = mem_get("weights", 0)
calc:weight_cells = mem_size("weights")
say:"First weight: (first_weight)"
say:"Cell count: (weight_cells)"
free:weights
assert: not mem_exists("weights")
~~~

The matrix and manual-memory API is available in the interpreter. BTC remains a preview compiler for its documented subset and does not yet transpile these commands.


### BriseNN — neural networks

BriseNN is an experimental, native multilayer-perceptron library. It uses dense layers, Xavier weight initialization, sigmoid/tanh/ReLU/linear activations, forward propagation, and online stochastic-gradient backpropagation. Training and prediction data are supplied as Brise matrices (one sample per row). Models can be saved and loaded as versioned text files in the script directory.

~~~brise
Matrix:create(X, 4, 2)
Matrix:set(X, 0, 0, 0)
Matrix:set(X, 0, 1, 0)
Matrix:set(X, 1, 0, 0)
Matrix:set(X, 1, 1, 1)
Matrix:set(X, 2, 0, 1)
Matrix:set(X, 2, 1, 0)
Matrix:set(X, 3, 0, 1)
Matrix:set(X, 3, 1, 1)
Matrix:create(Y, 4, 1)
Matrix:set(Y, 0, 0, 0)
Matrix:set(Y, 1, 0, 1)
Matrix:set(Y, 2, 0, 1)
Matrix:set(Y, 3, 0, 1)

NN:create(classifier, 2)
NN:layer(classifier, 4)
NN:activation(classifier, "tanh")
NN:layer(classifier, 1)
NN:activation(classifier, "sigmoid")
NN:train(classifier, X, Y, 3000, 0.5)
NN:predict(classifier, X, P)
say:"First prediction: (mget(\"P\", 0, 0))"
NN:save(classifier, "classifier.nn")
NN:load(restored, "classifier.nn")
~~~

API:

- NN:create(name, input_size) creates a model.
- NN:layer(name, neuron_count) appends a dense layer (default activation: tanh).
- NN:activation(name, "relu|tanh|sigmoid|linear") configures the most recently added layer.
- NN:forward(name, input_matrix, output_matrix) performs inference; NN:predict is an alias.
- NN:train(name, input_matrix, target_matrix, epochs, learning_rate) trains with online SGD/backpropagation and mean-squared-error gradients.
- NN:save(name, "file.nn") and NN:load(name, "file.nn") persist/restore model parameters.

Input and target matrices must have the same number of rows; their column counts must match the network input and output widths. Matrix indexes are zero-based. The model file is a versioned text format, not a portable executable. Model parameters are limited to 1,048,576 per network; training is currently single-threaded and does not shuffle samples.


### Networking

~~~brise
Netwe:page("https://example.com/")
say:page
~~~

On Linux, Brise uses OpenSSL with certificate-chain and hostname verification. On Windows, it uses WinHTTP and the Windows certificate store. Network requests use connection/read timeouts and are limited to 4 MiB of response data. DNS resolution uses the operating-system resolver and may take longer than the socket timeout. Redirects are not followed automatically.

### Safe file I/O

read: and write: accept only a single regular filename in the same directory as the current .bri file. Directory separators, absolute paths, symlinks and reparse points are rejected. Individual file reads/writes are limited to 4 MiB.

~~~brise
write:("notes.txt", "Hello from Brise")
read:notes("notes.txt")
say:notes
~~~

### BTC compiler

BTC is a preview transpiler that supports only a subset of Brise syntax. It does not yet have full feature parity with the interpreter. The compiler now starts GCC directly with an argument array rather than invoking a shell.

## Known limitations

Brise is experimental. It does not yet provide lexical scopes, parameters or return values for user-defined commands, a package manager, a formal module system, a debugger, stable bytecode, or a full AST-based multiline parser. Error recovery and source diagnostics need more work. String case conversion is ASCII-only. Netwe: currently supports GET requests only and does not automatically follow redirects.

## Tests

Run interpreter checks:

~~~sh
./brise tests/smoke.bri
~~~

Run the BTC test:

~~~sh
./btc tests/btc_smoke.bri --no-build
gcc -std=c11 tests/btc_smoke.c -o tests/btc_smoke
./tests/btc_smoke
~~~

GitHub Actions builds the interpreter and BTC on Linux and Windows, runs smoke tests, checks file path traversal, and performs a local certificate-verified HTTPS test on Linux.

## License

MIT. See LICENSE.
