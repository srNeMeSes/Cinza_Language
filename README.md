Cinza Programming Language

Created by Evaniel Arcanjo Da Silva.

Cinza is a statically typed programming language written in C++20. Programs are compiled to
bytecode and run on the CVM, a register-based virtual machine. A tree-walk interpreter is kept as
the reference implementation (`--interp`): both produce byte-for-byte identical results.

- Language specification: [`spec/`](spec/README.md) (in Portuguese)
- Build: `make` · Tests: `make test` (both modes plus a differential test)
- Run: `cinza program.cinza` · Show bytecode: `cinza --bytecode program.cinza`
