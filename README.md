
# LISP9

2019-07-28

By Nils M Holm, in the public domain

LISP9 is a lexically scoped LISP-1 with tail call elimination.
It is a retro LISP in the sense that it does not add any new
ideas, but merely implements concepts that have been established
long ago. LISP9 is similar to R4RS Scheme to a degree that many
trivial Scheme programs can probably be converted to LISP9 by
performing a few simple substitutions, like SET! -> SETQ, etc.

The LISP9 system consists of a read-eval-print loop (REPL), a
bytecode compiler, and an ad-hoc, SECD-like abstract machine.
LISP9 is also a retro LISP in the sense that its heap space
would roughly fit in the core memory of a moderately sized KL10,
but the default size of the heap space can be extended easily.

Although the name of LISP9 may suggest that it runs on Plan 9,
it currently doesn't (unless you use APE). It may at some point,
though.

## Notes for this release

Nils has many fine books and implementations of Lisp at [https://www.t3x.org](https://www.t3x.org)


The home for this GitHub release is [https://github.com/blakemcbride/LISP9](https://github.com/blakemcbride/LISP9)

