Cinza Programming Language

Created by Evaniel Arcanjo Da Silva.

Cinza is a statically typed programming language written in C++20. It has two backends, both
kept in sync with the language:

- the CVM, a register-based virtual machine: `cinza program.cinza` runs a program right away;
- a native compiler built on LLVM: `cinzac program.cinza -o program.exe` builds an executable
  (`-O2` to optimize).

Both share the front-end and the runtime rules, so a program prints the same output and the
same errors, with the same stack traces, on either one.

- Language specification: [`spec/`](spec/README.md) (in Portuguese)
- Build: `make` (the CVM) · `make cinzac` (the native compiler, in `llvm/bin/`)
- Tests: `make test` (CVM) · `make test-llvm` (the same suite through `cinzac`)
- Show bytecode: `cinza --bytecode program.cinza` · Show the IR: `cinzac --emit-llvm program.cinza`
- Design notes: [`cvm/DESENHO.md`](cvm/DESENHO.md) and [`llvm/DESENHO.md`](llvm/DESENHO.md)
