# Brise language roadmap

This file separates implemented features from future language-design work. Items are not complete until code and tests land.

## Implemented in the v0.5.0 development branch

- [x] Number, string and boolean values
- [x] Arithmetic expressions, finite-result checks, floating-point remainder and exponentiation
- [x] Scientific-notation numeric literals
- [x] Logical conditions: and, or, not
- [x] Bounded while: loop
- [x] Expanded math library: trigonometry, logarithms, interpolation, clamping, number theory and rounding
- [x] Dense numeric matrices with bounded allocation, indexed access, identity, addition, subtraction, multiplication, transpose, scaling, determinant and inverse
- [x] Explicitly allocated numeric memory blocks with indexed access, shared allocation budget and `free:`
- [x] Assertion command and math smoke tests
- [x] File I/O restricted to regular files in the script directory, with symlink rejection and size/error checks
- [x] HTTPS with certificate and hostname verification (WinHTTP on Windows, OpenSSL on POSIX)
- [x] Network connect/read timeouts and 4 MiB response cap
- [x] BTC invokes GCC without a shell
- [x] Linux and Windows CI for interpreter and BTC smoke tests

## Next milestones

### Language core
- [ ] Multi-line blocks and a real parser/AST
- [ ] Escaped strings and Unicode-aware string operations
- [ ] Unified expression grammar including boolean operators and consistent precedence
- [ ] Lexical scopes, function parameters and return values
- [ ] Arrays/maps as first-class values

### Runtime and developer experience
- [ ] Structured error codes and source spans
- [ ] Debugger / REPL
- [ ] Package and module manager
- [ ] Stable language specification and semantic versioning
- [ ] Explicit redirect policy and configurable network permissions
- [ ] Bounded DNS lookup and cancellation support

### Compiler
- [ ] Define the supported BTC language subset
- [ ] Bring BTC parser behavior into parity with the interpreter or clearly separate the dialect
- [ ] Add compiler tests and generated-code validation

## Release rule

Do not call Brise production-ready until the parser, runtime safety boundaries, test coverage, documentation, and compiler behavior are stable and independently verified.
