# Brise language roadmap

This file separates implemented features from future language-design work. Items are not complete until code and tests land.

## Implemented in the v0.5.0 development branch

- [x] Number, string and boolean values
- [x] Arithmetic expressions and comparisons
- [x] Logical conditions: `and`, `or`, `not`
- [x] Bounded `while:` loop
- [x] Basic string and math built-ins
- [x] Assertion command for smoke tests
- [x] Linux and Windows CI workflow

## Next milestones

### Language core
- [ ] Multi-line blocks and a real parser/AST
- [ ] Escaped strings and Unicode-aware string operations
- [ ] Consistent operator precedence, including boolean expressions in a unified expression grammar
- [ ] Lexical scopes, function parameters and return values
- [ ] Arrays/maps as first-class values

### Runtime and developer experience
- [ ] Sandboxed file access and configurable permissions
- [ ] HTTPS support and request timeouts
- [ ] Structured error codes and source spans
- [ ] Debugger / REPL
- [ ] Package and module manager
- [ ] Stable language specification and semantic versioning

### Compiler
- [ ] Define the supported BTC language subset
- [ ] Bring BTC parser behavior into parity with the interpreter or clearly separate the dialect
- [ ] Add compiler tests and generated-code validation

## Release rule

Do not call Brise production-ready until the parser, runtime safety boundaries, test coverage, documentation, and compiler behavior are stable and independently verified.
