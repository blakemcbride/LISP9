# CLAUDE.md

## Project Overview

LISP9 is a lexically-scoped LISP-1 interpreter with tail call elimination and bytecode compilation, originally written by Nils M Holm. Public domain (CC0). It implements a dialect close to R4RS Scheme. Extended with floating point number support.

## Build

```bash
make          # Compiles ls9 executable and creates ls9.image
make test     # Runs original test suite (test.ls9)
make clean    # Removes binaries and intermediate files
./ls9 test-float.ls9   # Runs float test suite (419 tests)
```

Requires an ANSI C89-compatible compiler (gcc, clang, cc) and the math library (`-lm`, added automatically by the Makefile).

## Architecture

- **ls9.c** - Entire interpreter in one C file: reader, compiler, bytecode VM, GC
- **ls9.ls9** - Standard library bootstrap (loaded to create ls9.image via `dump-image`)
- **test.ls9** - Original test suite
- **test-float.ls9** - Floating point test suite (419 tests)
- **src/** - Library and benchmark files (.ls9)

### Compilation pipeline

`read -> expand (macros) -> syncheck -> clsconv (closure conversion) -> compile (bytecode) -> interpret`

### Memory model

Two pools: node pool (Car/Cdr/Tag arrays, 262144 nodes) and vector pool (262144 cells). GC uses Deutsch/Schorr/Waite pointer reversal (non-recursive mark). Values protected from GC via `protect()`/`unprot()` macros.

### VM

SECD-like accumulator machine with registers: Acc, Ip, Sp, Fp, Ep, Prog. Stack in resizable `Rts[]` vector.

## Numeric Types

### Fixnums (integers)
Stored as nested atoms: `mkatom(T_FIXNUM, mkatom(value, NIL))`. Value stored directly in a cell's Car field. Range: INT_MIN to INT_MAX.

### Floats (doubles)
Stored in the vector pool via `newvec(T_FLOAT, sizeof(double))`. The 8-byte `double` is copied into the vector pool with `memcpy` (avoiding aliasing issues). Tagged with `VECTOR_TAG` so the GC handles them automatically. Each float uses 4 cells in the vector pool + 1 node.

### Polymorphic arithmetic
`+`, `-`, `*`, `div`, `=`, `<`, `>`, `<=`, `>=`, `abs`, `negate`, `max`, `min` all accept both fixnums and floats. When either operand is a float, the result is promoted to float. Pure fixnum operations return fixnum. `rem` is fixnum-only. `expt` returns fixnum when both args are non-negative integers and the result fits.

### Float-specific primitives
- Type predicates: `floatp`, `numberp`
- Conversion: `fixnum->float`, `float->fixnum` (truncates)
- Math: `sqrt`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `exp`, `log`, `expt`, `floor`, `ceiling`, `round`

### Reader/Printer
Float literals: `3.14`, `-2.5`, `1e10`, `1.5e-3`. The reader tries `scanfloat` (requires `.` or `e`/`E` in token) before `scanfix`, so large numbers like `10000000000.0` parse correctly. The printer uses `%.15g` format with `.0` appended when needed to distinguish from integers.

### numstr/strnum
`numstr` handles both fixnums (with radix) and floats (decimal only). `strnum` tries fixnum first, then float (radix 10 only).

## C Coding Conventions

- ANSI C89 only (no C99 features), plus `<math.h>`
- K&R style, 8-character tab indentation
- Lowercase with underscores for functions; UPPERCASE for macros/constants
- `cell` = `int` (basic memory unit), `byte` = `unsigned char`
- Type checking via macros: `fixp()`, `floatp()`, `numberp()`, `charp()`, `stringp()`, `symbolp()`, etc.
- GC safety: always `protect()` values before allocating, `unprot()` after

## Type Tags

```
T_BYTECODE (-10)  T_CATCHTAG (-11)  T_CHAR    (-12)  T_CLOSURE (-13)
T_FIXNUM   (-14)  T_INPORT   (-15)  T_OUTPORT (-16)  T_STRING  (-17)
T_SYMBOL   (-18)  T_VECTOR   (-19)  T_FLOAT   (-20)
```

## Special Constants

- NIL = -1, TRUE = -2, EOFMARK = -3, UNDEF = -4

## Key Files

| File | Purpose |
|------|---------|
| ls9.c | Complete interpreter (reader, compiler, VM, GC) |
| ls9.ls9 | Standard library / bootstrap |
| test.ls9 | Original test suite |
| test-float.ls9 | Float test suite (419 tests) |
| lisp9.txt | Reference manual |
| src/disasm.ls9 | Bytecode disassembler |
| src/grind.ls9 | Pretty printer |
| src/help.ls9 | Interactive help system |
| src/boyer.ls9 | Logic benchmark |
| Makefile | Build system |

## Running

```bash
./ls9                    # REPL (prompt: * )
./ls9 -q                 # Quiet/batch mode
echo '(+ 1 2.5)' | ./ls9 -q
```

REPL commands: `,c cmd` (shell), `,h topic` (help), `,l file` (load). Last result in `**`.

## Important Notes

- Primitives (like `sqrt`, `sin`, etc.) are compiled to opcodes, not closures. They cannot be passed to higher-order functions directly; wrap in a lambda: `(mapcar (lambda (x) (sqrt x)) '(1 4 9))`
- Changing the opcode enum invalidates existing image files; `make` rebuilds the image automatically
- The `eqv` function in ls9.ls9 was extended to compare floats by value using `=`
