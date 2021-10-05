/*
 * LISP9 Interpreter
 * Nils M Holm, 2018,2019
 * In the public domain
 *
 * If your country does not have a concept like the public
 * domain, the Creative Common Zero (CC0) licence applies,
 * see https://creativecommons.org/publicdomain/zero/1.0/
 */

#define VERSION "20190812"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <signal.h>
#include <setjmp.h>

/*
 * Tunable parameters
 */

#define IMAGEFILE	"ls9.image"
#define IMAGESRC	"ls9.ls9"

#define NNODES		262144
#define NVCELLS		262144
#define NPORTS		20
#define TOKLEN		80
#define CHUNKSIZE	1024
#define MXMAX		2000
#define NTRACE		10
#define PRDEPTH		1024

/*
 * Basic data types
 */

#define cell	int
#define byte	unsigned char
#define uint	unsigned int

/*
 * Special Objects
 */

#define specialp(x)	((x) < 0)
#define NIL		(-1)
#define TRUE		(-2)
#define EOFMARK		(-3)
#define UNDEF		(-4)
#define RPAREN		(-5)
#define DOT		(-6)

/*
 * Memory pools
 */

cell	*Car = NULL,
	*Cdr = NULL;
byte	*Tag = NULL;

cell	*Vectors = NULL;

cell	Freelist = NIL;
cell	Freevec = 0;

#define ATOM_TAG	0x01	/* Atom, CAR = type, CDR = next */
#define MARK_TAG	0x02	/* Mark */
#define TRAV_TAG	0x04	/* Traversal */
#define VECTOR_TAG	0x08	/* Vector, CAR = type, CDR = content */
#define PORT_TAG	0x10	/* Atom is an I/O port (with ATOM_TAG) */
#define USED_TAG	0x20	/* Port: used flag */
#define LOCK_TAG	0x40	/* Port: locked (do not close) */
#define CONST_TAG	0x80	/* Node is immutable */

#define tag(n)		(Tag[n])

#define car(x)          (Car[x])
#define cdr(x)          (Cdr[x])
#define caar(x)         (Car[Car[x]])
#define cadr(x)         (Car[Cdr[x]])
#define cdar(x)         (Cdr[Car[x]])
#define cddr(x)         (Cdr[Cdr[x]])
#define caaar(x)        (Car[Car[Car[x]]])
#define caadr(x)        (Car[Car[Cdr[x]]])
#define cadar(x)        (Car[Cdr[Car[x]]])
#define caddr(x)        (Car[Cdr[Cdr[x]]])
#define cdaar(x)        (Cdr[Car[Car[x]]])
#define cdadr(x)        (Cdr[Car[Cdr[x]]])
#define cddar(x)        (Cdr[Cdr[Car[x]]])
#define cdddr(x)        (Cdr[Cdr[Cdr[x]]])
#define caaaar(x)       (Car[Car[Car[Car[x]]]])
#define caaadr(x)       (Car[Car[Car[Cdr[x]]]])
#define caadar(x)       (Car[Car[Cdr[Car[x]]]])
#define caaddr(x)       (Car[Car[Cdr[Cdr[x]]]])
#define cadaar(x)       (Car[Cdr[Car[Car[x]]]])
#define cadadr(x)       (Car[Cdr[Car[Cdr[x]]]])
#define caddar(x)       (Car[Cdr[Cdr[Car[x]]]])
#define cadddr(x)       (Car[Cdr[Cdr[Cdr[x]]]])
#define cdaaar(x)       (Cdr[Car[Car[Car[x]]]])
#define cdaadr(x)       (Cdr[Car[Car[Cdr[x]]]])
#define cdadar(x)       (Cdr[Car[Cdr[Car[x]]]])
#define cdaddr(x)       (Cdr[Car[Cdr[Cdr[x]]]])
#define cddaar(x)       (Cdr[Cdr[Car[Car[x]]]])
#define cddadr(x)       (Cdr[Cdr[Car[Cdr[x]]]])
#define cdddar(x)       (Cdr[Cdr[Cdr[Car[x]]]])
#define cddddr(x)       (Cdr[Cdr[Cdr[Cdr[x]]]])

/*
 * Tagged data types
 */

#define T_BYTECODE	(-10)
#define T_CATCHTAG	(-11)
#define T_CHAR		(-12)
#define T_CLOSURE	(-13)
#define T_FIXNUM	(-14)
#define T_INPORT	(-15)
#define T_OUTPORT	(-16)
#define T_STRING	(-17)
#define T_SYMBOL	(-18)
#define T_VECTOR	(-19)

/*
 * Basic constructors 
 */

#define cons(a, d)	cons3((a), (d), 0)
#define mkatom(a, d)	cons3((a), (d), ATOM_TAG)

/*
 * Accessors
 */

#define portno(n)	(cadr(n))
#define string(n)	((byte *) &Vectors[Cdr[n]])
#define stringlen(n)	(Vectors[Cdr[n] - 1])
#define symname(n)	(string(n))
#define symlen(n)	(stringlen(n))
#define vector(n)	(&Vectors[Cdr[n]])
#define veclink(n)	(Vectors[Cdr[n] - 2])
#define vecndx(n)	veclink(n)
#define vecsize(k)	(2 + ((k) + sizeof(cell)-1) / sizeof(cell))
#define veclen(n)	(vecsize(stringlen(n)) - 2)

/*
 * Type predicates
 */

#define charp(n) \
	(!specialp(n) && (tag(n) & ATOM_TAG) && T_CHAR == car(n))

#define closurep(n) \
	(!specialp(n) && (tag(n) & ATOM_TAG) && T_CLOSURE == car(n))

#define ctagp(n) \
	(!specialp(n) && (tag(n) & ATOM_TAG) && T_CATCHTAG == car(n))

#define eofp(n)	(EOFMARK == (n))

#define fixp(n) \
	(!specialp(n) && (tag(n) & ATOM_TAG) && T_FIXNUM == car(n))

#define inportp(n) \
	(!specialp(n) && (tag(n) & ATOM_TAG) && \
	 (tag(n) & PORT_TAG) && T_INPORT == car(n))

#define outportp(n) \
	(!specialp(n) && (tag(n) & ATOM_TAG) && \
	 (tag(n) & PORT_TAG) && T_OUTPORT == car(n))

#define stringp(n) \
	(!specialp(n) && (tag(n) & VECTOR_TAG) && T_STRING == car(n))

#define symbolp(n) \
	(!specialp(n) && (tag(n) & VECTOR_TAG) && T_SYMBOL == car(n))

#define vectorp(n) \
	(!specialp(n) && (tag(n) & VECTOR_TAG) && T_VECTOR == car(n))

#define atomp(n) \
	(specialp(n) || (tag(n) & ATOM_TAG) || (tag(n) & VECTOR_TAG))

#define pairp(x) (!atomp(x))

#define listp(x) (NIL == (x) || pairp(x))

#define constp(n) \
	(!specialp(n) && (tag(n) & CONST_TAG))

/*
 * Abstract machine opcodes
 */

enum {	OP_ILL, OP_APPLIS, OP_APPLIST, OP_APPLY, OP_TAILAPP, OP_QUOTE,
	OP_ARG, OP_REF, OP_PUSH, OP_PUSHTRUE, OP_PUSHVAL, OP_POP,
	OP_DROP, OP_JMP, OP_BRF, OP_BRT, OP_HALT, OP_CATCHSTAR,
	OP_THROWSTAR, OP_CLOSURE, OP_MKENV, OP_PROPENV, OP_CPREF,
	OP_CPARG, OP_ENTER, OP_ENTCOL, OP_RETURN, OP_SETARG, OP_SETREF,
	OP_MACRO,

	OP_ABS, OP_ALPHAC, OP_ATOM, OP_BITOP, OP_CAAR, OP_CADR, OP_CAR,
	OP_CDAR, OP_CDDR, OP_CDR, OP_CEQUAL, OP_CGRTR, OP_CGTEQ,
	OP_CHAR, OP_CHARP, OP_CHARVAL, OP_CLESS, OP_CLOSE_PORT,
	OP_CLTEQ, OP_CMDLINE, OP_CONC, OP_CONS, OP_CONSTP, OP_CTAGP,
	OP_DELETE, OP_DIV, OP_DOWNCASE, OP_DUMP_IMAGE, OP_EOFP, OP_EQ,
	OP_EQUAL, OP_ERROR, OP_ERROR2, OP_ERRPORT, OP_EVAL, OP_EXISTSP,
	OP_FIXP, OP_FLUSH, OP_FORMAT, OP_FUNP, OP_GC, OP_GENSYM,
	OP_GRTR, OP_GTEQ, OP_INPORT, OP_INPORTP, OP_LESS, OP_LISTSTR,
	OP_LISTVEC, OP_LOAD, OP_LOWERC, OP_LTEQ, OP_MAX, OP_MIN,
	OP_MINUS, OP_MKSTR, OP_MKVEC, OP_MX, OP_MX1, OP_NCONC,
	OP_NEGATE, OP_NRECONC, OP_NULL, OP_NUMERIC, OP_NUMSTR,
	OP_OBTAB, OP_OPEN_INFILE, OP_OPEN_OUTFILE, OP_OUTPORT,
	OP_OUTPORTP, OP_PAIR, OP_PEEKC, OP_PLUS, OP_PRIN, OP_PRINC,
	OP_QUIT, OP_READ, OP_READC, OP_RECONC, OP_REM, OP_RENAME,
	OP_SCONC, OP_SEQUAL, OP_SETCAR, OP_SETCDR, OP_SET_INPORT,
	OP_SET_OUTPORT, OP_SFILL, OP_SGRTR, OP_SGTEQ, OP_SIEQUAL,
	OP_SIGRTR, OP_SIGTEQ, OP_SILESS, OP_SILTEQ, OP_SLESS, OP_SLTEQ,
	OP_SREF, OP_SSET, OP_SSIZE, OP_STRINGP, OP_STRLIST, OP_STRNUM,
	OP_SUBSTR, OP_SUBVEC, OP_SYMBOL, OP_SYMBOLP, OP_SYMNAME,
	OP_SYMTAB, OP_SYSCMD, OP_TIMES, OP_UNTAG, OP_UPCASE, OP_UPPERC,
	OP_VCONC, OP_VECLIST, OP_VECTORP, OP_VFILL, OP_VREF, OP_VSET,
	OP_VSIZE, OP_WHITEC, OP_WRITEC };

/*
 * I/O functions
 */

void	prints(char *s);
void	prin(cell x);

#define printb(s)	prints((char *) s)
#define nl()		prints("\n")

int	set_outport(int port);

/*
 * Error reporting and handling
 */

int	Trace[NTRACE];
int	Tp = 0;

void clrtrace(void) {
	int	i;

	for (i=0; i<NTRACE; i++) Trace[i] = -1;
}

int gottrace(void) {
	int	i;

	for (i=0; i<NTRACE; i++)
		if (Trace[i] != -1) return 1;
	return 0;
}

int	Plimit = 0;

int	Line = 1;
cell	Files = NIL;

cell	Symbols;

char	*ntoa(int x, int r);

void report(char *s, cell x) {
	int	i, j, o;

	o = set_outport(2);
	prints("*** error: ");
	prints(s);
	if (x != UNDEF) {
		prints(": ");
		Plimit = 100;
		prin(x);
		Plimit = 0;
	}
	nl();
	if (Files != NIL) {
		prints("*** file: ");
		printb(string(car(Files)));
		prints(", line: ");
		prints(ntoa(Line, 10));
		nl();
	}
	if (gottrace()) {
		prints("*** trace:");
		i = Tp;
		for (j=0; j<NTRACE; j++) {
			if (i >= NTRACE) i = 0;
			if (Trace[i] != -1) {
				prints(" ");
				printb(symname(vector(Symbols)[Trace[i]]));
			}
			i++;
		}
		nl();
	}
	set_outport(o);
}

jmp_buf	Restart;
jmp_buf	Errtag;
cell	Handler = NIL;

cell	Glob;
cell	S_errtag, S_errval;

int	assq(cell x, cell a);
void	bindset(cell v, cell a);
cell	mkstr(char *s, int k);

void error(char *s, cell x) {
	cell	n;

	n = assq(S_errtag, Glob);
	Handler = (NIL == n)? NIL: cadr(n);
	if (Handler != NIL) {
		n = assq(S_errval, Glob);
		if (n != NIL && cadr(n) == Handler)
			bindset(S_errval, mkstr(s, strlen(s)));
		longjmp(Errtag, 1);
	}
	report(s, x);
	longjmp(Restart, 1);
}

void expect(char *who, char *what, cell got) {
	char	b[100];

	sprintf(b, "%s: expected %s", who, what);
	error(b, got);
}

void fatal(char *s) {
	fprintf(stderr, "*** fatal error: ");
	fprintf(stderr, "%s\n", s);
	exit(EXIT_FAILURE);
}

/*
 * Low-level input/output
 */

FILE	*Ports[NPORTS];
char	Port_flags[NPORTS];

int	Inport = 0,
	Outport = 1,
	Errport = 2;

cell	Outstr = NIL;
int	Outmax = 0;
int	Outptr = 0;

char	*Instr = NULL;
char	Rejected = -1;

int readc(void) {
	int	c;

	if (Instr != NULL) {
		if (Rejected > -1) {
			c = Rejected;
			Rejected = -1;
			return c;
		}
		if (0 == *Instr) {
			return EOF;
		}
		else {
			return *Instr++;
		}
	}
	else {
		if (NULL == Ports[Inport])
			fatal("readc: input port is not open");
		return getc(Ports[Inport]);
	}
}

void rejectc(int c) {
	if (Instr != NULL) {
		Rejected = c;
	}
	else {
		ungetc(c, Ports[Inport]);
	}
}

cell	mkport(int p, cell t);

void flush(void) {
	if (fflush(Ports[Outport]))
		error("file write error, port",
			mkport(Outport, T_OUTPORT));
}

void blockwrite(char *s, int k) {
	cell	n;

	if (1 == Plimit) return;
	if (Outstr != NIL) {
		while (Outptr + k >= Outmax) {
			n = mkstr(NULL, Outmax+1000);
			memcpy(string(n), string(Outstr), Outptr);
			Outmax += 1000;
			Outstr = n;
		}
		memcpy(&string(Outstr)[Outptr], s, k);
		Outptr += k;
		string(Outstr)[Outptr] = 0;
		return;
	}
	if (NULL == Ports[Outport])
		fatal("blockwrite: output port is not open");
	if (fwrite(s, 1, k, Ports[Outport]) != k)
		error("file write error, port",
			mkport(Outport, T_OUTPORT));
	if ((1 == Outport || 2 == Outport) && '\n' == s[k-1])
		flush();
	if (Plimit) {
		Plimit -= k;
		if (Plimit < 1) Plimit = 1;
	}
}

void writec(int c) {
	char	b[1];

	b[0] = c;
	blockwrite(b, 1);
}

void prints(char *s) {
	blockwrite(s, strlen(s));
}

/*
 * Memory management
 */

void alloc_nodepool(void) {
	Car = malloc(sizeof(cell) * NNODES);
	Cdr = malloc(sizeof(cell) * NNODES);
	Tag = malloc(NNODES);
	if (NULL == Car || NULL == Cdr || NULL == Tag)
		fatal("alloc_nodepool: out of physical memory");
	memset(Car, 0, sizeof(cell) * NNODES);
	memset(Cdr, 0, sizeof(cell) * NNODES);
	memset(Tag, 0, NNODES);
}

void alloc_vecpool(void) {
	Vectors = malloc(sizeof(cell) * NVCELLS);
	if (NULL == Vectors)
		fatal("alloc_vecpool: out of physical memory");
	memset(Vectors, 0, sizeof(cell) * NVCELLS);
}

#define OBFREE		0
#define OBALLOC		1
#define	OBUSED		2

#define ISIZE0		1
#define ISIZE1		3
#define ISIZE2		5

#define fetcharg(a, i)	(((a)[i] << 8) | (a)[(i)+1])

cell	Obarray, Obmap;

void marklit(cell p) {
	int	i, k, op;
	byte	*v, *m;

	k = stringlen(p);
	v = string(p);
	m = string(Obmap);
	for (i=0; i<k; ) {
		op = v[i];
		if (OP_QUOTE == op) {
			m[fetcharg(v, i+1)] = OBUSED;
			i += ISIZE1;
		}
		else if (OP_ARG == op || OP_PUSHVAL == op || OP_JMP == op ||
			 OP_BRF == op || OP_BRT == op || OP_CLOSURE == op ||
			 OP_MKENV == op || OP_ENTER == op || OP_ENTCOL == op ||
			 OP_SETARG == op || OP_SETREF == op || OP_MACRO == op)
		{
			i += ISIZE1;
		}
		else if (OP_REF == op || OP_CPARG == op || OP_CPREF == op) {
			i += ISIZE2;
		}
		else {
			i += ISIZE0;
		}
	}
}

/*
 * Mark nodes which can be accessed through N.
 * Using modified Deutsch/Schorr/Waite pointer reversal algorithm.
 * S0: M==0, T==0, unvisited, process CAR (vectors: process 1st slot);
 * S1: M==1, T==1, CAR visited, process CDR (vectors: process next slot);
 * S2: M==1, T==0, completely visited, return to parent.
 */

void mark(cell n) {
	cell	x, parent, *v;
	int	i;

	parent = NIL;
	while (1) {
		if (specialp(n) || (tag(n) & MARK_TAG)) {
			if (NIL == parent)
				break;
			if (tag(parent) & VECTOR_TAG) { /* S1 --> S1|done */
				i = vecndx(parent);
				v = vector(parent);
				if (tag(parent) & TRAV_TAG &&
				    i+1 < veclen(parent)
				) {			/* S1 --> S1 */
					x = v[i+1];
					v[i+1] = v[i];
					v[i] = n;
					n = x;
					vecndx(parent) = i+1;
				}
				else {			/* S1 --> done */
					x = parent;
					parent = v[i];
					v[i] = n;
					n = x;
					veclink(n) = n;
				}
			}
			else if (tag(parent) & TRAV_TAG) { /* S1 --> S2 */
				x = cdr(parent);
				cdr(parent) = car(parent);
				car(parent) = n;
				tag(parent) &= ~TRAV_TAG;
				n = x;
			}
			else {				/* S2 --> done */
				x = parent;
				parent = cdr(x);
				cdr(x) = n;
				n = x;
			}
		}
		else if (tag(n) & VECTOR_TAG) {		/* S0 --> S1 */
			tag(n) |= MARK_TAG;
			if (T_VECTOR == car(n) && veclen(n) != 0) {
				tag(n) |= TRAV_TAG;
				vecndx(n) = 0;
				v = vector(n);
				x = v[0];
				v[0] = parent;
				parent = n;
				n = x;
			}
			else {
				veclink(n) = n;
			}
		}
		else if (tag(n) & ATOM_TAG) {		/* S0 --> S2 */
			if (cdr(n) != NIL) {
				if (T_BYTECODE == car(n)) {
					marklit(cdr(n));
				}
				else if (T_INPORT == car(n) ||
					 T_OUTPORT == car(n)
				)
					Port_flags[portno(n)] |= USED_TAG;
			}
			x = cdr(n);
			cdr(n) = parent;
			parent = n;
			n = x;
			tag(parent) |= MARK_TAG;
		}
		else {					/* S0 --> S1 */
			x = car(n);
			car(n) = parent;
			tag(n) |= MARK_TAG;
			parent = n;
			n = x;
			tag(parent) |= TRAV_TAG;
		}
	}
}

int	GC_verbose = 0;
cell	*GC_roots[];
cell	Rts;
int	Sp;

int gc(void) {
	int	i, n, k, sk;
	char	buf[100];
	cell	*a;
	byte	*m;

	for (i=0; i<NPORTS; i++) {
		if (Port_flags[i] & LOCK_TAG)
			Port_flags[i] |= USED_TAG;
		else if (i == Inport || i == Outport)
			Port_flags[i] |= USED_TAG;
		else
			Port_flags[i] &= ~USED_TAG;
	}
	if (Rts != NIL) {
		sk = stringlen(Rts);
		stringlen(Rts) = (1 + Sp) * sizeof(cell);
	}
	for (i=0; GC_roots[i] != NULL; i++) {
		mark(*GC_roots[i]);
	}
	if (Rts != NIL) {
		stringlen(Rts) = sk;
	}
	k = 0;
	Freelist = NIL;
	for (i=0; i<NNODES; i++) {
		if (!(tag(i) & MARK_TAG)) {
			cdr(i) = Freelist;
			Freelist = i;
			k++;
		}
		else {
			tag(i) &= ~MARK_TAG;
		}
	}
	for (i=0; i<NPORTS; i++) {
		if (!(Port_flags[i] & USED_TAG) && Ports[i] != NULL) {
			fclose(Ports[i]);
			Ports[i] = NULL;
		}
	}
	n = NIL == Obarray? 0: veclen(Obarray);
	a = NIL == Obarray? NULL: vector(Obarray);
	m = NIL == Obmap? NULL: string(Obmap);
	for (i=0; i<n; i++) {
		if (OBUSED  == m[i]) {
			m[i] = OBALLOC;
		}
		else {
			m[i] = OBFREE;
			a[i] = NIL;
		}
	}
	if (GC_verbose) {
		sprintf(buf, "GC: %d nodes reclaimed", k);
		prints(buf); nl();
		flush();
	}
	return k;
}

cell	Tmp_car = NIL,
	Tmp_cdr = NIL;

cell cons3(cell pcar, cell pcdr, int ptag) {
	cell	n;
	int	k;

	if (NIL == Freelist) {
		if (0 == (ptag & ~CONST_TAG))
			Tmp_car = pcar;
		if (!(ptag & VECTOR_TAG))
			Tmp_cdr = pcdr;
		k = gc();
		if (k < NNODES / 2) {
			/* memory low! */
		}
		Tmp_car = Tmp_cdr = NIL;
		if (NIL == Freelist)
			error("cons3: out of nodes", UNDEF);
	}
	n = Freelist;
	Freelist = cdr(Freelist);
	car(n) = pcar;
	cdr(n) = pcdr;
	tag(n) = ptag;
	return n;
}

#define RAW_VECLINK	0
#define RAW_VECSIZE	1
#define RAW_VECDATA	2

void unmark_vecs(void) {
	int	p, k, link;

	p = 0;
	while (p < Freevec) {
		link = p;
		k = Vectors[p + RAW_VECSIZE];
		p += vecsize(k);
		Vectors[link] = NIL;
	}
}

int gcv(void) {
	int	v, k, to, from;
	char	buf[100];

	unmark_vecs();
	gc();		/* re-mark live vectors */
	to = from = 0;
	while (from < Freevec) {
		v = Vectors[from + RAW_VECSIZE];
		k = vecsize(v);
		if (Vectors[from + RAW_VECLINK] != NIL) {
			if (to != from) {
				memmove(&Vectors[to], &Vectors[from],
					k * sizeof(cell));
				cdr(Vectors[to + RAW_VECLINK]) =
					to + RAW_VECDATA;
			}
			to += k;
		}
		from += k;
	}
	k = Freevec - to;
	if (GC_verbose) {
		sprintf(buf, "GCV: %d cells reclaimed", k);
		prints(buf); nl();
		flush();
	}
	Freevec = to;
	return k;
}

cell newvec(cell type, int size) {
	cell	n;
	int	v, wsize;

	wsize = vecsize(size);
	if (Freevec + wsize >= NVCELLS) {
		gcv();
		if (Freevec + wsize >= NVCELLS)
			error("newvec: out of vector space", UNDEF);
	}
	v = Freevec;
	Freevec += wsize;
	n = cons3(type, v + RAW_VECDATA, VECTOR_TAG);
	Vectors[v + RAW_VECLINK] = n;
	Vectors[v + RAW_VECSIZE] = size;
	return n;
}

cell	Protected = NIL;
cell	Tmp = NIL;

#define protect(n) (Protected = cons((n), Protected))

cell unprot(int k) {
	cell	n = NIL; /*LINT*/

	while (k) {
		if (NIL == Protected)
			error("unprot: stack underflow", UNDEF);
		n = car(Protected);
		Protected = cdr(Protected);
		k--;
	}
	return n;
}

/*
 * High-level data types
 */

#define mkfix(n) mkatom(T_FIXNUM, mkatom((n), NIL))

#define fixval(n) (cadr(n))

#define add_ovfl(a,b) \
	((((b) > 0) && ((a) > INT_MAX - (b))) || \
	 (((b) < 0) && ((a) < INT_MIN - (b))))

#define sub_ovfl(a,b) \
	((((b) < 0) && ((a) > INT_MAX + (b))) || \
	 (((b) > 0) && ((a) < INT_MIN + (b))))

#define mkchar(c) mkatom(T_CHAR, mkatom((c) & 0xff, NIL))

#define charval(n) (cadr(n))

cell	Nullstr = NIL;

cell mkstr(char *s, int k) {
	cell	n;

	if (0 == k) return Nullstr;
	n = newvec(T_STRING, k+1);
	if (NULL == s) {
		memset(string(n), 0, k+1);
	}
	else {
		memcpy(string(n), s, k);
		string(n)[k] = 0;
	}
	return n;
}

cell	Nullvec = NIL;

cell mkvec(int k) {
	cell	n, *v;
	int	i;

	if (0 == k) return Nullvec;
	n = newvec(T_VECTOR, k * sizeof(cell));
	v = vector(n);
	for (i=0; i<k; i++) v[i] = NIL;
	return n;
}

cell mkport(int portno, cell type) {
	cell	n;
	int	pf;

	pf = Port_flags[portno];
	Port_flags[portno] |= LOCK_TAG;
	n = mkatom(portno, NIL);
	n = cons3(type, n, ATOM_TAG|PORT_TAG);
	Port_flags[portno] = pf;
	return n;
}

int htsize(int n) {
	if (n < 47) return 47;
	if (n < 97) return 97;
	if (n < 199) return 199;
	if (n < 499) return 499;
	if (n < 997) return 997;
	if (n < 9973) return 9973;
	if (n < 19997) return 19997;
	return 39989;
}

cell mkht(int k) {
	cell	n;

	n = mkfix(0); /* mutable, can't use Zero */
	protect(n);
	n = cons(n, mkvec(htsize(k)));
	unprot(1);
	return n;
}

#define htlen(d)	veclen(cdr(d))
#define htelts(d)	fixval(car(d))
#define htdata(d)	cdr(d)
#define htslots(d)	vector(cdr(d))

uint hash(byte *s, uint k) {
	uint	h = 0xabcd;

	while (*s) h = ((h << 5) + h) ^ *s++;
	return h % k;
}

uint obhash(cell x, uint k) {
	if (specialp(x))
		return abs(x) % k;
	if (symbolp(x))
		return hash(symname(x), k);
	if (fixp(x))
		return abs(fixval(x)) % k;
	if (charp(x))
		return charval(x) % k;
	if (stringp(x))
		return hash(string(x), k);
	return 0;
}

int match(cell a, cell b) {
	int	k;

	if (a == b) {
		return 1;
	}
	if (fixp(a) && fixp(b)) {
		return fixval(a) == fixval(b);
	}
	if (charp(a) && charp(b)) {
		return charval(a) == charval(b);
	}
	if (symbolp(a) && symbolp(b)) {
		k = symlen(a);
		if (symlen(b) != k) return 0;
		return memcmp(symname(a), symname(b), k) == 0;
	}
	if (stringp(a) && stringp(b)) {
		k = stringlen(a);
		if (stringlen(b) != k) return 0;
		return memcmp(string(a), string(b), k) == 0;
	}
	return 0;
}

void htgrow(cell d) {
	int	nk, i, h, k;
	cell	nd, e, n;

	k = htlen(d);
	nk = 1 + htlen(d);
	nd = mkht(nk);
	protect(nd);
	nk = htlen(nd);
	for (i = 0; i < k; i++) {
		for (e = htslots(d)[i]; e != NIL; e = cdr(e)) {
			h = obhash(caar(e), nk);
			n = cons(car(e), htslots(nd)[h]);
			htslots(nd)[h] = n;
		}
	}
	htdata(d) = htdata(nd);
	unprot(1);
}

int htlookup(cell d, cell k) {
	cell	x;
	int	h;

	h = obhash(k, htlen(d));
	x = htslots(d)[h];
	while (x != NIL) {
		if (match(caar(x), k)) return car(x);
		x = cdr(x);
	}
	return UNDEF;
}

void htadd(cell d, cell k, cell v) {
	cell	e;
	int	h;

	Tmp = k;
	protect(v);
	protect(k);
	Tmp = NIL;
	if (htelts(d) >= htlen(d))
		htgrow(d);
	h = obhash(k, htlen(d));
	e = cons(k, v);
	e = cons(e, htslots(d)[h]);
	htslots(d)[h] = e;
	htelts(d)++;
	unprot(2);
}

cell htrem(cell d, cell k) {
	cell	*x, *v;
	int	h;

	h = obhash(k, htlen(d));
	v = htslots(d);
	x = &v[h];
	while (*x != NIL) {
		if (match(caar(*x), k)) {
			*x = cdr(*x);
			htelts(d)--;
			break;
		}
		x = &cdr(*x);
	}
	return d;
}

cell	Symhash = NIL;
cell	Symbols = NIL;
int	Symptr = 0;

cell mksym(char *s, int k) {
	cell	n;

	n = newvec(T_SYMBOL, k+1);
	strcpy((char *) symname(n), s);
	return n;
}

cell findsym(char *s) {
	cell	y;

	y = mksym(s, strlen(s));
	y = htlookup(Symhash, y);
	if (y != UNDEF) return car(y);
	return NIL;
}

cell intern(cell y) {
	cell	n, *vn, *vs;
	int	i, k;

	protect(y);
	htadd(Symhash, y, mkfix(Symptr));
	unprot(1);
	k = veclen(Symbols);
	if (Symptr >= k) {
		n = mkvec(k + CHUNKSIZE);
		vs = vector(Symbols);
		vn = vector(n);
		for (i=0; i<k; i++) vn[i] = vs[i];
		Symbols = n;
	}
	vector(Symbols)[Symptr] = y;
	Symptr++;
	return y;
}

cell symref(char *s) {
	cell	y, new;

	y = findsym(s);
	if (y != NIL) return y;
	new = mksym(s, strlen(s));
	return intern(new);
}

/*
 * Some useful list functions
 */

cell reconc(cell n, cell m) {
	while (n != NIL) {
		if (atomp(n)) error("reconc: dotted list", n);
                m = cons(car(n), m);
		n = cdr(n);
        }
        return m;
}

#define reverse(n) reconc((n), NIL)

cell nreconc(cell n, cell m) {
	cell	h;

	while (n != NIL) {
		if (atomp(n)) error("nreconc: dotted list", n);
		h = cdr(n);
		cdr(n) = m;
		m = n;
		n = h;
	}
	return m;
}

#define nreverse(n) nreconc((n), NIL)

cell conc(cell a, cell b) {
	cell	n;

	a = reverse(a);
	protect(a);
	n = b;
	while (a != NIL) {
		n = cons(car(a), n);
		a = cdr(a);
	}
	unprot(1);
	return n;
}

cell nconc(cell a, cell b) {
	cell	n;

	n = a;
	if (NIL == a) return b;
	while (cdr(a) != NIL) a = cdr(a);
	cdr(a) = b;
	return n;
}

/*
 * High-level port I/O
 */

int newport(void) {
	int	i, n;

	for (n=0; n<2; n++) {
		for (i=0; i<NPORTS; i++) {
			if (NULL == Ports[i])
				return i;
		}
		if (0 == n) gc();
	}
	return -1;
}

int open_inport(char *path) {
	int	i;

	i = newport();
	if (i < 0) return -1;
	Ports[i] = fopen(path, "r");
	if (NULL == Ports[i]) return -1;
	return i;
}

int open_outport(char *path, int append) {
	int	i;

	i = newport();
	if (i < 0) return -1;
	Ports[i] = fopen(path, append? "a": "w");
	if (NULL == Ports[i]) return -1;
	return i;
}

cell set_inport(cell port) {
	cell	p = Inport;

	Inport = port;
	return p;
}

int set_outport(int port) {
	int	p = Outport;

	Outport = port;
	return p;
}

void close_port(int port) {
	if (port < 0 || port >= NPORTS)
		return;
	if (NULL == Ports[port]) {
		Port_flags[port] = 0;
		return;
	}
	fclose(Ports[port]);
	Ports[port] = NULL;
	Port_flags[port] = 0;
}

void reset_stdports(void) {
	clearerr(stdin);
	clearerr(stdout);
	clearerr(stderr);
	Inport = 0;
	Outport = 1;
	Errport = 2;
}

int lock_port(int port) {
	if (port < 0 || port >= NPORTS)
		return -1;
	Port_flags[port] |= LOCK_TAG;
	return 0;
}

int unlock_port(int port) {
	if (port < 0 || port >= NPORTS)
		return -1;
	Port_flags[port] &= ~LOCK_TAG;
	return 0;
}

/*
 * Global environment
 */

cell	Glob = NIL;

void bindnew(cell v, cell a) {
	cell	n;

	n = cons(a, NIL);
	n = cons(v, n);
	Glob = cons(n, Glob);
}

int assq(cell x, cell a) {
	for (; a != NIL; a = cdr(a))
		if (caar(a) == x) return car(a);
	return NIL;
}

void bindset(cell v, cell a) {
	cell	b;

	b = assq(v, Glob);
	if (b != NIL) cadr(b) = a;
}

/*
 * Reader
 */

cell	S_apply, S_def, S_defmac, S_defun, S_errtag,
	S_errval, S_if, S_ifstar, S_imagefile, S_labels, S_lambda,
	S_macro, S_prog, S_quiet, S_quote, S_qquote, S_starstar,
	S_splice, S_setq, S_start, S_unquote;

cell	P_abs, P_alphac, P_atom, P_bitop, P_caar, P_cadr, P_car,
	P_catchstar, P_cdar, P_cddr, P_cdr, P_cequal, P_cgrtr, P_cgteq,
	P_char, P_charp, P_charval, P_cless, P_close_port, P_clteq,
	P_cmdline, P_conc, P_cons, P_constp, P_ctagp, P_delete, P_div,
	P_downcase, P_dump_image, P_eofp, P_eq, P_equal, P_gc, P_error,
	P_errport, P_eval, P_existsp, P_fixp, P_flush, P_format, P_funp,
	P_gensym, P_grtr, P_gteq, P_inport, P_inportp, P_less,
	P_liststr, P_listvec, P_load, P_lowerc, P_lteq, P_max, P_min,
	P_minus, P_mkstr, P_mkvec, P_mx, P_mx1, P_nconc, P_nreconc,
	P_not, P_null, P_numeric, P_numstr, P_obtab, P_open_infile,
	P_open_outfile, P_outport, P_outportp, P_pair, P_peekc, P_plus,
	P_prin, P_princ, P_quit, P_read, P_readc, P_reconc, P_rem,
	P_rename, P_sconc, P_sequal, P_set_inport, P_set_outport,
	P_setcar, P_setcdr, P_sfill, P_sgrtr, P_sgteq, P_siequal,
	P_sigrtr, P_sigteq, P_siless, P_silteq, P_sless, P_slteq,
	P_sref, P_sset, P_ssize, P_stringp, P_strlist, P_strnum,
	P_substr, P_subvec, P_symbol, P_symbolp, P_symname, P_symtab,
	P_syscmd, P_throwstar, P_times, P_untag, P_upcase, P_upperc,
	P_veclist, P_vconc, P_vectorp, P_vfill, P_vref, P_vset, P_vsize,
	P_whitec, P_writec;

volatile int	Intr;

int	Inlist = 0;
int	Quoting = 0;

#define octalp(c) \
	('0' == (c) || '1' == (c) || '2' == (c) || '3' == (c) || \
	 '4' == (c) || '5' == (c) || '6' == (c) || '7' == (c))

int octchar(char *s) {
	int	v = 0;

	if (!octalp(*s)) return -1;
	while (octalp(*s)) {
		v = 8*v + *s - '0';
		s++;
	}
	return (*s || v > 255)? -1: v;
}

#define symbolic(c) \
	(isalpha(c) || isdigit(c) || (c && strchr("!$%^&*-/_+=~.?<>:", c)))

#define LP	'('
#define RP	')'

int strcmp_ci(char *s1, char *s2) {
	int	c1, c2;

	while (1) {
		c1 = tolower((int) *s1++);
		c2 = tolower((int) *s2++);
		if (!c1 || !c2 || c1 != c2)
			break;
	}
	return c1-c2;
}

char	*Readerr = NULL;

void rderror(char *s, cell x) {
	if (NULL == Instr) error(s, x);
	Readerr = s;
}

cell rdchar(void) {
	char	name[TOKLEN+1];
	int	i, c, v;

	c = readc();
	name[0] = c;
	c = readc();
	for (i=1; i<TOKLEN; i++) {
		if (Intr || Readerr) return NIL;
		if (!isalpha(c) && !isdigit(c)) break;
		name[i] = c;
		c = readc();
	}
	name[i] = 0;
	rejectc(c);
	if (TOKLEN == i)
		rderror("char name too long",
			mkstr(name, strlen(name)));
	if (!strcmp_ci(name, "ht")) return mkchar(9);
	if (!strcmp_ci(name, "nl")) return mkchar(10);
	if (!strcmp_ci(name, "sp")) return mkchar(' ');
	v = octchar(&name[1]);
	if ('\\' == *name && v >= 0) return mkchar(v);
	if (i != 1) rderror("bad character name",
			mkstr(name, strlen(name)));
	return mkchar(name[0]);
}

cell	xread2(void);

cell rdlist(void) {
	cell		n, a, p;
	cell		new;
	static char	badpair[] = "malformed pair";

	Inlist++;
	n = xread2();
	if (RPAREN == n) {
		Inlist--;
		return NIL;
	}
	p = NIL;
	a = cons3(n, NIL, CONST_TAG);
	protect(a);
	while (n != RPAREN) {
		if (Intr || Readerr) {
			unprot(1);
			return NIL;
		}
		if (EOFMARK == n)  {
			unprot(1);
			rderror("missing ')'", UNDEF);
			return NIL;
		}
		else if (DOT == n) {
			if (NIL == p) {
				unprot(1);
				rderror(badpair, UNDEF);
				return NIL;
			}
			n = xread2();
			cdr(p) = n;
			if (RPAREN == n || xread2() != RPAREN) {
				unprot(1);
				rderror(badpair, UNDEF);
				return NIL;
			}
			Inlist--;
			return unprot(1);
		}
		car(a) = n;
		p = a;
		n = xread2();
		if (n != RPAREN) {
			Tmp = n;
			new = cons3(NIL, NIL, CONST_TAG);
			Tmp = NIL;
			cdr(a) = new;
			a = cdr(a);
		}
	}
	Inlist--;
	return unprot(1);
}

cell	listvec(cell x, int veclit);

cell rdvec(void) {
	return listvec(rdlist(), 1);
}

int pos(int p, char *s) {
	int	i;

	i = 0;
	for (; *s; s++) {
		if (p == *s) return i;
		i++;
	}
	return -1;
}

cell scanfix(char *s, int r, int of) {
	int	v, g, i;
	char	*p;
	char	d[] = "0123456789abcdefghijklmnopqrstuvwxyz";

	g = 1;
	p = s;
	if ('+' == *p) {
		p++;
	}
	else if ('-' == *p) {
		p++;
		g = -1;
	}
	v = 0;
	while (*p) {
		i = pos(tolower(*p), d);
		if (i < 0 || i >= r) return NIL;
		if (	v > INT_MAX/r ||
			(v > 0 && add_ovfl(v*r, i)) ||
			(v < 0 && sub_ovfl(v*r, i)))
		{
			if (!of) return NIL;
			rderror("fixnum too big", mkstr(s, strlen(s)));
		}
		else if (v < 0)
			v = v*r - i;
		else
			v = v*r + i;
		p++;
		if (g) v *= g;
		g = 0;
	}
	if (g) return NIL;
	return mkfix(v);
}

cell rdsymfix(int c, int r, int sym) {
	char	name[TOKLEN+1];
	int	i;
	cell	n;

	for (i=0; i<TOKLEN; i++) {
		if (!symbolic(c)) break;
		name[i] = tolower(c);
		c = readc();
	}
	name[i] = 0;
	rejectc(c);
	if (TOKLEN == i) rderror("symbol or fixnum too long",
				mkstr(name, strlen(name)));
	n = scanfix(name, r, 1);
	if (n != NIL) return n;
	if (!sym) rderror("invalid digits after #radixR",
				mkstr(name, strlen(name)));
	if ('t' == name[0] && 0 == name[1])
		return TRUE;
	if (!strcmp(name, "nil"))
		return NIL;
	return symref(name);
}

cell rdfix(int c) {
	int	r;

	r = 0;
	while (isdigit(c)) {
		r = r*10 + c - '0';
		c = readc();
	}
	if (c != 'r') rderror("'R' expected after #radix", UNDEF);
	if (r < 2 || r > 36) rderror("bad radix in #radixR", mkfix(r));
	c = readc();
	return rdsymfix(c, r, 0);
}

cell rdstr(void) {
	char	name[TOKLEN+1];
	int	i, j, c, u, v;
	cell	n;

	c = readc();
	u = 0;
	for (i=0; i<TOKLEN; i++) {
		if (Intr || Readerr) return NIL;
		if ('"' == c) break;
		if ('\n' == c) Line++;
		if (EOF == c) rderror("EOF in string", UNDEF);
		if ('\\' == c) {
			c = readc();
			if ('\\' == c || '"' == c) {
				/**/
			}
			else if ('t' == c) {
				c = '\t';
			}
			else if ('n' == c) {
				c = '\n';
			}
			else if (octalp(c)) {
				v = 0;
				j = 0;
				while (j < 3 && octalp(c)) {
					v = v * 8 + c-'0';
					c = readc();
					j++;
				}
				rejectc(c);
				if (v > 255) rderror("invalid char", mkfix(v));
				c = v;
			}
			else if (0 == u) {
				u = c;
			}
		}
		name[i] = c;
		c = readc();
	}
	name[i] = 0;
	if (u) rderror("unknown slash sequence", mkchar(u));
	if (i >= TOKLEN) rderror("string too long", mkstr(name, i));
	if (u) return NIL;
	n = mkstr(name, i);
	tag(n) |= CONST_TAG;
	return n;
}

cell rdquote(cell q) {
	cell	n;

	Quoting++;
	n = xread2();
	Quoting--;
	return cons(q, cons(n, NIL));
}

cell meta(void) {
	int	c, cmd, i;
	cell	n, cmdsym;
	char	s[128];

	cmd = tolower(readc());
	c = readc();
	while (' ' == c) c = readc();
	i = 0;
	while (c != '\n' && c != EOF) {
		if (i < sizeof(s) - 6)
			s[i++] = c;
		c = readc();
	}
	rejectc(c);
	s[i] = 0;
	if ('l' == cmd) strcat(s, ".ls9");
	n = mkstr(s, strlen(s));
	n = 0 == i? NIL: cons(n, NIL);
	protect(n);
	switch (cmd) {
	case 'c':	cmdsym = symref("syscmd"); break;
	case 'h':	cmdsym = symref("help"); break;
	case 'l':	cmdsym = P_load; break;
	default: 	prints(",c = syscmd"); nl();
			prints(",h = help"); nl();
			prints(",l = load"); nl();
			return NIL;
	}
	unprot(1);
	return cons(cmdsym, n);
}

cell xread2(void) {
	int	c;

	c = readc();
	while (1) {
		while (' ' == c || '\t' == c || '\n' == c || '\r' == c) {
			if (Intr || Readerr) return NIL;
			if ('\n' == c) Line++;
			c = readc();
		}
		if (c != ';') break;
		while (c != '\n' && c != EOF)
			c = readc();
	}
	if (Intr || Readerr) return NIL;
	if (EOF == c) {
		return EOFMARK;
	}
	else if ('#' == c) {
		c = readc();
		if ('\\' == c) return rdchar();
		else if (LP == c) return rdvec();
		else if (isdigit(c)) return rdfix(c);
		else rderror("bad # syntax", mkchar(c));
	}
	else if ('"' == c) {
		return rdstr();
	}
	else if (LP == c) {
		return rdlist();
	}
	else if (RP == c) {
		if (!Inlist) rderror("unexpected ')'", UNDEF);
		return RPAREN;
	}
	else if ('\'' == c) {
		return rdquote(S_quote);
	}
	else if ('`' == c || '@' == c) {
		return rdquote(S_qquote);
	}
	else if (',' == c) {
		if (!Inlist && !Quoting) return meta();
		c = readc();
		if ('@' == c) return rdquote(S_splice);
		rejectc(c);
		return rdquote(S_unquote);
	}
	else if ('.' == c) {
		if (!Inlist) rderror("unexpected '.'", UNDEF);
		return DOT;
	}
	else if (symbolic(c)) {
		return rdsymfix(c, 10, 1);
	}
	else {
		rderror("funny input character, code", mkfix(c));
	}
	return NIL;
}

cell xread(void) {
	cell	x;

	Inlist = 0;
	Quoting = 0;
	Readerr = NULL;
	x = xread2();
	if (Intr) error("aborted", UNDEF);
	return x;
}

/*
 * Printer
 */

char *ntoa(int x, int r) {
	static char	buf[200];
	int		i = 0, neg;
	char		*p = &buf[sizeof(buf)-1];
	char		d[] = "0123456789abcdefghijklmnopqrstuvwxyz";

	neg = x<0;
	*p = 0;
	while (x || 0 == i) {
		i++;
		p--;
		*p = d[abs(x % r)];
		x = x / r;
	}
	if (neg) {
		p--;
		*p = '-';
	}
	return p;
}

void prchar(int sl, cell x) {
	if (sl) {
		prints("#\\");
		if (9 == charval(x)) prints("ht");
		else if (10 == charval(x)) prints("nl");
		else if (' ' == charval(x)) prints("sp");
		else if (charval(x) < 32 || charval(x) > 126) {
			prints("\\");
			prints(ntoa(fixval(x), 8));
		}
		else writec(charval(x));
	}
	else {
		writec(charval(x));
	}
}

void prfix(cell x) {
	prints(ntoa(fixval(x), 10));
}

void prstr(int sl, cell x) {
	int	i, c;

	if (sl) {
		writec('"');
		for (i=0; i<stringlen(x)-1; i++) {
			c = (byte) string(x)[i];
			if ('"' == c)
				prints("\\\"");
			else if ('\\' == c)
				prints("\\\\");
			else if (10 == c)
				prints("\\n");
			else if (c < ' ' || c > 126) {
				writec('\\');
				if (octalp(string(x)[i+1])) {
					if (c < 100) writec('0');
					if (c < 10) writec('0');
				}
				prints(ntoa(c, 8));
			}
			else
				writec(c);
		}
		writec('"');
	}
	else {
		printb(string(x));
	}
}

void prex(int sl, cell x, int d);

void prlist(int sl, cell x, int d) {
	writec(LP);
	while (x != NIL && Plimit != 1) {
		prex(sl, car(x), d+1);
		x = cdr(x);
		if (x != NIL) {
			writec(' ');
			if (atomp(x)) {
				prints(". ");
				prex(sl, x, d+1);
				break;
			}
		}
	}
	writec(RP);
}

void prvec(int sl, cell x, int d) {
	int	i;

	prints("#(");
	for (i=0; i<veclen(x); i++) {
		prex(sl, vector(x)[i], d+1);
		if (i < veclen(x)-1) writec(' ');
	}
	writec(')');
}

void prport(int out, cell x) {
	prints("#<");
	prints(out? "out": "in");
	prints("port ");
	prints(ntoa(portno(x), 10));
	prints(">");
}

void pruspec(cell x) {
	prints("#<special object ");
	prints(ntoa(x, 10));
	prints(">");
}

void pruatom(cell x) {
	prints("#<atom ");
	prints(ntoa(car(x), 10));
	prints(">");
}

#define quoted(x, q) \
	(car(x) == (q) && cdr(x) != NIL && NIL == cddr(x))

void prquote(int sl, cell x, int d) {
	if (car(x) == S_quote) writec('\'');
	else if (car(x) == S_qquote) writec('@');
	else if (car(x) == S_unquote) writec(',');
	else if (car(x) == S_splice) prints(",@");
	prex(sl, cadr(x), d);
}

void prex(int sl, cell x, int d) {
	if (d > PRDEPTH) {
		prints("\n");
		error("prin: nesting too deep", UNDEF);
	}
	if (Intr) {
		Intr = 0;
		error("interrupted", UNDEF);
	}
	if (NIL == x) prints("nil");
	else if (TRUE == x) prints("t");
	else if (EOFMARK == x) prints("#<eof>");
	else if (UNDEF == x) prints("#<undef>");
	else if (charp(x)) prchar(sl, x);
	else if (fixp(x)) prfix(x);
	else if (symbolp(x)) printb(symname(x));
	else if (stringp(x)) prstr(sl, x);
	else if (vectorp(x)) prvec(sl, x, d);
	else if (closurep(x)) prints("#<function>");
	else if (ctagp(x)) prints("#<catch tag>");
	else if (inportp(x)) prport(0, x);
	else if (outportp(x)) prport(1, x);
	else if (specialp(x)) pruspec(x);
	else if (atomp(x)) pruatom(x);
	else if (quoted(x, S_quote)) prquote(sl, x, d);
	else if (quoted(x, S_qquote)) prquote(sl, x, d);
	else if (quoted(x, S_unquote)) prquote(sl, x, d);
	else if (quoted(x, S_splice)) prquote(sl, x, d);
	else prlist(sl, x, d);
}

void xprint(int sl, cell x) {
	prex(sl, x, 0);
	if (1 == Plimit) {
		Plimit = 0;
		prints("...");
	}
}

void prin(cell x) { xprint(1, x); }

void princ(cell x) { xprint(0, x); }

void print(cell x) { prin(x); nl(); }

/*
 * Syntax checker
 */

int length(cell n) {
	int	k;

	for (k = 0; n != NIL; n = cdr(n))
		k++;
	return k;
}

void ckargs(cell x, int min, int max) {
	int	k;
	char	buf[100];

	k = length(x)-1;
	if (k < min || (k > max && max >= 0)) {
		sprintf(buf, "%s: wrong number of arguments",
			symname(car(x)));
		error(buf, x);
	}
}

int syncheck(cell x, int top);

int ckseq(cell x, int top) {
	for (; pairp(x); x = cdr(x))
		syncheck(car(x), top);
	return 0;
}

int ckapply(cell x) {
	ckargs(x, 2, -1);
	return 0;
}

int ckdef(cell x, int top) {
	ckargs(x, 2, 2);
	if (!symbolp(cadr(x)))
		error("def: expected symbol", cadr(x));
	if (!top) error("def: must be at top level", x);
	return syncheck(caddr(x), 0);
}

int ckif(cell x) {
	ckargs(x, 2, 3);
	return ckseq(cdr(x), 0);
}

int ckifstar(cell x) {
	ckargs(x, 2, 2);
	return ckseq(cdr(x), 0);
}

int symlistp(cell x) {
	cell	p;

	for (p = x; pairp(p); p = cdr(p)) {
		if (!symbolp(car(p)))
			return 0;
	}
	return symbolp(p) || NIL == p;
}

int memq(cell x, cell a) {
	for (; a != NIL; a = cdr(a))
		if (car(a) == x) return a;
	return NIL;
}

int uniqlistp(cell x) {
	if (NIL == x) return 1;
	while (cdr(x) != NIL) {
		if (memq(car(x), cdr(x)) != NIL)
			return 0;
		x = cdr(x);
	}
	return 1;
}

cell flatargs(cell a) {
	cell	n;

	protect(n = NIL);
	while (pairp(a)) {
		n = cons(car(a), n);
		car(Protected) = n;
		a = cdr(a);
	}
	if (a != NIL) n = cons(a, n);
	unprot(1);
	return nreverse(n);
}

int cklambda(cell x) {
	ckargs(x, 2, -1);
	if (!symlistp(cadr(x)))
		error("lambda: invalid formals", cadr(x));
	if (!uniqlistp(flatargs(cadr(x))))
		error("lambda: duplicate formal", cadr(x));
	return ckseq(cddr(x), 0);
}

int ckmacro(cell x, int top) {
	ckargs(x, 2, 2);
	if (!symbolp(cadr(x)))
		error("macro: expected symbol", cadr(x));
	if (!top) error("macro: must be at top level", x);
	return syncheck(caddr(x), 0);
}

int ckprog(cell x, int top) {
	return ckseq(cdr(x), top);
}

int ckquote(cell x) {
	ckargs(x, 1, 1);
	return 0;
}

int cksetq(cell x) {
	ckargs(x, 2, 2);
	if (!symbolp(cadr(x)))
		error("setq: expected symbol", cadr(x));
	return ckseq(cddr(x), 0);
}

int syncheck(cell x, int top) {
	cell	p;

	if (atomp(x)) return 0;
	for (p = x; pairp(p); p = cdr(p))
		;
	if (p != NIL)
		error("dotted list in program", x);
	if (car(x) == S_apply) return ckapply(x);
	if (car(x) == S_def) return ckdef(x, top);
	if (car(x) == S_if) return ckif(x);
	if (car(x) == S_ifstar) return ckifstar(x);
	if (car(x) == S_lambda) return cklambda(x);
	if (car(x) == S_macro) return ckmacro(x, top);
	if (car(x) == S_prog) return ckprog(x, top);
	if (car(x) == S_quote) return ckquote(x);
	if (car(x) == S_setq) return cksetq(x);
	return ckseq(x, top);
}

/*
 * Compiler, closure conversion
 */

cell set_union(cell a, cell b) {
	cell	n;

	a = reverse(a);
	protect(a);
	protect(n = b);
	while (pairp(a)) {
		if (memq(car(a), b) == NIL)
			n = cons(car(a), n);
		car(Protected) = n;
		a = cdr(a);
	}
	if (a != NIL && memq(a, b) == NIL)
		n = cons(a, n);
	unprot(2);
	return n;
}

int subrp(cell x);

cell freevars(cell x, cell e) {
	cell	n, u, a;
	int	lam;

	lam = 0;
	if (memq(x, e) != NIL) {
		return NIL;
	}
	else if (symbolp(x)) {
		return cons(x, NIL);
	}
	else if (!pairp(x)) {
		return NIL;
	}
	else if (car(x) == S_quote) {
		return NIL;
	}
	else if (car(x) == S_apply ||
		 car(x) == S_prog ||
		 car(x) == S_if ||
		 car(x) == S_ifstar ||
		 car(x) == S_setq
	) {
		x = cdr(x);
	}
	else if (car(x) == S_def ||
		 car(x) == S_macro
	) {
		x = cddr(x);
	}
	else if (subrp(car(x))) {
		x = cdr(x);
	}
	else if (car(x) == S_lambda) {
		protect(e);
		a = flatargs(cadr(x));
		protect(a);
		n = set_union(a, e);
		protect(n);
		e = n;
		x = cddr(x);
		lam = 1;
	}
	protect(u = NIL);
	while (pairp(x)) {
		n = freevars(car(x), e);
		protect(n);
		u = set_union(u, n);
		unprot(1);;
		car(Protected) = u;
		x = cdr(x);
	}
	n = unprot(1);
	if (lam) e = unprot(3);
	return n;
}

int posq(cell x, cell a) {
	int	n;

	n = 0;
	for (; a != NIL; a = cdr(a)) {
		if (car(a) == x) return n;
		n++;
	}
	return NIL;
}

cell	I_a, I_e;

cell initmap(cell fv, cell e, cell a) {
	cell	m, n, p;
	int	i, j;

	protect(m = NIL);
	i = 0;
	while (fv != NIL) {
		p = cons(car(fv), NIL);
		protect(p);
		n = mkfix(i);
		p = cons(n, p);
		car(Protected) = p;
		if ((j = posq(car(fv), a)) != NIL) {
			n = mkfix(j);
			p = cons(n, p);
			unprot(1);
			p = cons(I_a, p);
		}
		else if ((j = posq(car(fv), e)) != NIL) {
			n = mkfix(j);
			p = cons(n, p);
			unprot(1);
			p = cons(I_e, p);
		}
		else {
			error("undefined symbol", car(fv));
		}
		m = cons(p, m);
		car(Protected) = m;
		i++;
		fv = cdr(fv);
	}
	return nreverse(unprot(1));
}

cell lastpair(cell x) {
	if (NIL == x) return NIL;
	while (cdr(x) != NIL)
		x = cdr(x);
	return x;
}

cell	Env = NIL,
	Envp = NIL;

void newvar(cell x) {
	cell	n;

	if (memq(x, Env) != NIL) return;
	if (NIL == Envp) Envp = lastpair(Env);
	n = cons(x, NIL);
	cdr(Envp) = n;
	Envp = n;
}

void newvars(cell x) {
	while (x != NIL) {
		newvar(car(x));
		x = cdr(x);
	}
}

cell cconv(cell x, cell e, cell a);

cell mapconv(cell x, cell e, cell a) {
	cell	n, new;

	protect(n = NIL);
	while (pairp(x)) {
		new = cconv(car(x), e, a);
		n = cons(new, n);
		car(Protected) = n;
		x = cdr(x);
	}
	return nreverse(unprot(1));
}

cell	I_closure;

cell lamconv(cell x, cell e, cell a) {
	cell	cl, fv, args, m;

	fv = freevars(x, NIL);
	protect(fv);
	newvars(fv);
	args = flatargs(cadr(x));
	protect(args);
	m = initmap(fv, e, a);
	protect(m);
	cl = mapconv(cddr(x), fv, args);
	cl = cons(m, cl);
	cl = cons(cadr(x), cl);
	cl = cons(I_closure, cl);
	unprot(3);
	return cl;
}

int contains(cell a, cell x) {
	if (a == x) return 1;
	if (pairp(a) && (contains(car(a), x) || contains(cdr(a), x)))
		return 1;
	return 0;
}

int liftable(cell x) {
	return !contains(x, S_setq);
}

cell liftnames(cell m) {
	#define name cadddr
	cell	a, n;

	protect(a = NIL);
	while (m != NIL) {
		if (caar(m) == I_a) {
			n = name(car(m));
			a = cons(n, a);
			car(Protected) = a;
		}
		m = cdr(m);
	}
	return nreverse(unprot(1));
	#undef name
}

cell	I_arg, I_ref;

cell liftargs(cell m) {
	#define source	cadr
	cell	a, n;

	protect(a = NIL);
	while (m != NIL) {
		if (caar(m) == I_a) {
			n = source(car(m));
			n = cons(n, NIL);
			n = cons(caar(m) == I_a? I_arg: I_ref, n);
			a = cons(n, a);
			car(Protected) = a;
		}
		m = cdr(m);
	}
	return nreverse(unprot(1));
	#undef source
}

cell appconv(cell x, cell e, cell a) {
	cell	fn, as, fv, fnargs, m, n, lv, vars, cv;

	fn = car(x);
	as = cdr(x);
	fv = freevars(fn, NIL);
	protect(fv);
	fnargs = flatargs(cadr(fn));
	protect(fnargs);
	newvars(fv);
	m = initmap(fv, e, a);
	protect(m);
	as = mapconv(as, e, a);
	protect(as);
	n = liftargs(m);
	as = nconc(n, as);
	car(Protected) = as;
	lv = liftnames(m);
	protect(lv);
	vars = conc(lv, cadr(fn));
	protect(vars);
	cv = set_union(lv, fnargs);
	cadr(Protected) = cv;
	fn = mapconv(cddr(fn), e, cv);
	fn = cons(NIL, fn);
	fn = cons(vars, fn);
	fn = cons(I_closure, fn);
	unprot(6);
	return cons(fn, as);
}

cell defconv(cell x, cell e, cell a) {
	cell	n, m;

	newvar(cadr(x));
	n = cons(cconv(caddr(x), e, a), NIL);
	protect(n);
	m = mkfix(posq(cadr(x), e));
	protect(m);
	m = cons(I_ref, cons(m, cons(cadr(x), NIL)));
	unprot(2);
	return cons(S_setq, cons(m, n));
}

cell cconv(cell x, cell e, cell a) {
	int	n;

	if (	pairp(x) &&
		(S_apply == car(x)  ||
		 S_if == car(x)     ||
		 S_ifstar == car(x) ||
		 S_prog == car(x)   ||
		 S_setq == car(x)   ||
		 subrp(car(x))))
	{
		return cons(car(x), mapconv(cdr(x), e, a));
	}
	if ((n = posq(x, a)) != NIL) {
		return cons(I_arg, cons(mkfix(n), NIL));
	}
	if ((n = posq(x, e)) != NIL) {
		Tmp = mkfix(n);
		n = cons(I_ref, cons(Tmp, cons(x, NIL)));
		Tmp = NIL;
		return n;
	}
	if (symbolp(x)) {
		error("undefined symbol", x);
		return NIL;
	}
	if (atomp(x)) {
		return x;
	}
	if (S_quote == car(x)) {
		return x;
	}
	if (	pairp(car(x)) &&
		S_lambda == caar(x) &&
		liftable(car(x)))
	{
		return appconv(x, e, a);
	}
	if (S_lambda == car(x)) {
		return lamconv(x, e, a);
	}
	if (S_def == car(x)) {
		return defconv(x, e, a);
	}
	if (S_macro == car(x)) {
		return cons(car(x),
			    cons(cadr(x),
				 mapconv(cddr(x), e, a)));
	}
	return mapconv(x, e, a);
}

cell carof(cell a) {
	cell	n;

	protect(n = NIL);
	while (a != NIL) {
		n = cons(caar(a), n);
		car(Protected) = n;
		a = cdr(a);
	}
	unprot(1);
	return nreverse(n);
}

cell zipenv(cell vs, cell oe) {
	cell	n, b;

	protect(n = NIL);
	while (vs != NIL) {
		if (NIL == oe) {
			b = cons(car(vs), cons(UNDEF, NIL));
		}
		else {
			b = car(oe);
			oe = cdr(oe);
		}
		n = cons(b, n);
		car(Protected) = n;
		vs = cdr(vs);
	}
	return nreverse(unprot(1));
}

cell clsconv(cell x) {
	cell	n;

	Env = carof(Glob);
	Envp = NIL;
	if (NIL == Env) Env = cons(UNDEF, NIL);
	n = cconv(x, Env, NIL);
	protect(n);
	Glob = zipenv(Env, Glob);
	return unprot(1);
}

/*
 * Compiler, literal pool
 */

cell	Obhash = NIL,
	Obarray = NIL,
	Obmap = NIL;
int	Obptr = 0;

int obslot(void) {
	int	i, j, k, m;
	byte	*s;
	cell	n;

	for (m = 0; m < 2; m++) {
		for (j = 0; j < 2; j++) {
			k = veclen(Obarray);
			s = string(Obmap);
			for (i=0; i<k; i++) {
				if (OBFREE == s[Obptr]) {
					s[Obptr] = OBALLOC;
					return Obptr;
				}
				Obptr++;
				if (Obptr >= k) Obptr = 0;
			}
			if (0 == j) gc();
		}
		if (k + CHUNKSIZE >= 64 * 1024) break;
		n = mkvec(k + CHUNKSIZE);
		memcpy(vector(n), vector(Obarray), k * sizeof(cell));
		Obarray = n;
		n = mkstr(NULL, k + CHUNKSIZE);
		memset(string(n), OBFREE, k+CHUNKSIZE);
		memcpy(string(n), string(Obmap), k);
		Obmap = n;
	}
	error("out of object space", UNDEF);
	return -1;
}

int obindex(cell x) {
	cell	n;
	int	i;

	if (pairp(x) || vectorp(x) || closurep(x))
		return obslot();
	n = htlookup(Obhash, x);
	if (n != UNDEF) {
		i = fixval(cdr(n));
		if (	string(Obmap)[i] != OBFREE &&
			match(x, vector(Obarray)[i])
		)
			return i;
		htrem(Obhash, x);
	}
	i = obslot();
	htadd(Obhash, x, mkfix(i));
	return i;
}

/*
 * Compiler, code generator
 */

cell	Emitbuf = NIL;
int	Here = 0;

void emit(int x) {
	cell	n;
	byte	*vp, *vn;
	int	i, k;

	if (Here >= stringlen(cdr(Emitbuf))) {
		protect(x);
		k = stringlen(cdr(Emitbuf));
		n = mkstr(NULL, CHUNKSIZE + k);
		vp = string(cdr(Emitbuf));
		vn = string(n);
		for (i = 0; i < k; i++) vn[i] = vp[i];
		cdr(Emitbuf) = n;
		unprot(1);
	}
	string(cdr(Emitbuf))[Here] = x;
	Here++;
}

#define emitop(op) emit(op)

void emitarg(int i) {
	if (i < 0 || i > 65535)
		error("bytecode argument out of range", mkfix(i));
	emit(i >> 8);
	emit(i & 255);
}

void emitq(cell x) {
	int	i;

	i = obindex(x);
	vector(Obarray)[i] = x;
	emitop(OP_QUOTE);
	emitarg(i);
}

void patch(int a, int n) {
	if (n < 0 || n > 65535)
		error("bytecode argument out of range", mkfix(n));
	string(cdr(Emitbuf))[a] = n >> 8;
	string(cdr(Emitbuf))[a+1] = n & 255;
}

cell	Cts = NIL;

#define cpushval(x) (Cts = cons(mkfix(x), Cts))

cell cpopval(void) {
	cell	n;

	if (NIL == Cts)
		error("oops: compile stack underflow", UNDEF);
	n = car(Cts);
	Cts = cdr(Cts);
	return fixval(n);
}

void swap(void) {
	cell	x;

	if (NIL == Cts || NIL == cdr(Cts))
		error("oops: compile stack underflow", UNDEF);
	x = car(Cts);
	car(Cts) = cadr(Cts);
	cadr(Cts) = x;
}

int subr0(cell x) {
	if (x == P_cmdline)	return OP_CMDLINE;
	if (x == P_errport)	return OP_ERRPORT;
	if (x == P_gc)		return OP_GC;
	if (x == P_gensym)	return OP_GENSYM;
	if (x == P_inport)	return OP_INPORT;
	if (x == P_obtab)	return OP_OBTAB;
	if (x == P_outport)	return OP_OUTPORT;
	if (x == P_quit)	return OP_QUIT;
	if (x == P_symtab)	return OP_SYMTAB;
	return -1;
}

int subr1(cell x) {
	if (x == P_abs)		return OP_ABS;
	if (x == P_alphac)	return OP_ALPHAC;
	if (x == P_atom)	return OP_ATOM;
	if (x == P_caar)	return OP_CAAR;
	if (x == P_cadr)	return OP_CADR;
	if (x == P_car)		return OP_CAR;
	if (x == P_catchstar)	return OP_CATCHSTAR;
	if (x == P_cdar)	return OP_CDAR;
	if (x == P_cddr)	return OP_CDDR;
	if (x == P_cdr)		return OP_CDR;
	if (x == P_char)	return OP_CHAR;
	if (x == P_charp)	return OP_CHARP;
	if (x == P_charval)	return OP_CHARVAL;
	if (x == P_close_port)	return OP_CLOSE_PORT;
	if (x == P_constp)	return OP_CONSTP;
	if (x == P_ctagp)	return OP_CTAGP;
	if (x == P_delete)	return OP_DELETE;
	if (x == P_dump_image)	return OP_DUMP_IMAGE;
	if (x == P_downcase)	return OP_DOWNCASE;
	if (x == P_dump_image)	return OP_DUMP_IMAGE;
	if (x == P_eofp)	return OP_EOFP;
	if (x == P_eval)	return OP_EVAL;
	if (x == P_existsp)	return OP_EXISTSP;
	if (x == P_fixp)	return OP_FIXP;
	if (x == P_flush)	return OP_FLUSH;
	if (x == P_format)	return OP_FORMAT;
	if (x == P_funp)	return OP_FUNP;
	if (x == P_inportp)	return OP_INPORTP;
	if (x == P_liststr)	return OP_LISTSTR;
	if (x == P_listvec)	return OP_LISTVEC;
	if (x == P_load)	return OP_LOAD;
	if (x == P_lowerc)	return OP_LOWERC;
	if (x == P_mx)		return OP_MX;
	if (x == P_mx1)		return OP_MX1;
	if (x == P_not)		return OP_NULL;
	if (x == P_null)	return OP_NULL;
	if (x == P_numeric)	return OP_NUMERIC;
	if (x == P_open_infile) return OP_OPEN_INFILE;
	if (x == P_outportp)	return OP_OUTPORTP;
	if (x == P_pair)	return OP_PAIR;
	if (x == P_set_inport)	return OP_SET_INPORT;
	if (x == P_set_outport) return OP_SET_OUTPORT;
	if (x == P_ssize)	return OP_SSIZE;
	if (x == P_stringp)	return OP_STRINGP;
	if (x == P_strlist)	return OP_STRLIST;
	if (x == P_symbol)	return OP_SYMBOL;
	if (x == P_symbolp)	return OP_SYMBOLP;
	if (x == P_symname)	return OP_SYMNAME;
	if (x == P_syscmd)	return OP_SYSCMD;
	if (x == P_untag)	return OP_UNTAG;
	if (x == P_upcase)	return OP_UPCASE;
	if (x == P_upperc)	return OP_UPPERC;
	if (x == P_veclist)	return OP_VECLIST;
	if (x == P_vectorp)	return OP_VECTORP;
	if (x == P_vsize)	return OP_VSIZE;
	if (x == P_whitec)	return OP_WHITEC;
	return -1;
}

int subr2(cell x) {
	if (x == P_cons)	return OP_CONS;
	if (x == P_div)		return OP_DIV;
	if (x == P_eq)		return OP_EQ;
	if (x == P_nreconc)	return OP_NRECONC;
	if (x == P_reconc)	return OP_RECONC;
	if (x == P_rem)		return OP_REM;
	if (x == P_rename)	return OP_RENAME;
	if (x == P_sless)	return OP_SLESS;
	if (x == P_slteq)	return OP_SLTEQ;
	if (x == P_sequal)	return OP_SEQUAL;
	if (x == P_sgrtr)	return OP_SGRTR;
	if (x == P_sgteq)	return OP_SGTEQ;
	if (x == P_setcar)	return OP_SETCAR;
	if (x == P_setcdr)	return OP_SETCDR;
	if (x == P_sfill)	return OP_SFILL;
	if (x == P_siless)	return OP_SILESS;
	if (x == P_silteq)	return OP_SILTEQ;
	if (x == P_siequal)	return OP_SIEQUAL;
	if (x == P_sigrtr)	return OP_SIGRTR;
	if (x == P_sigteq)	return OP_SIGTEQ;
	if (x == P_sref)	return OP_SREF;
	if (x == P_throwstar)	return OP_THROWSTAR;
	if (x == P_vfill)	return OP_VFILL;
	if (x == P_vref)	return OP_VREF;
	return -1;
}

int subr3(cell x) {
	if (x == P_sset)	return OP_SSET;
	if (x == P_substr)	return OP_SUBSTR;
	if (x == P_subvec)	return OP_SUBVEC;
	if (x == P_vset)	return OP_VSET;
	return -1;
}

int osubr0(cell x) {
	if (x == P_peekc)	return OP_PEEKC;
	if (x == P_read)	return OP_READ;
	if (x == P_readc)	return OP_READC;
	return -1;
}

int osubr1(cell x) {
	if (x == P_error)		return OP_ERROR;
	if (x == P_mkstr)		return OP_MKSTR;
	if (x == P_mkvec)		return OP_MKVEC;
	if (x == P_numstr)		return OP_NUMSTR;
	if (x == P_open_outfile)	return OP_OPEN_OUTFILE;
	if (x == P_prin)		return OP_PRIN;
	if (x == P_princ)		return OP_PRINC;
	if (x == P_strnum)		return OP_STRNUM;
	if (x == P_writec)		return OP_WRITEC;
	return -1;
}

int lsubr0(cell x) {
	if (x == P_times)	return OP_TIMES;
	if (x == P_plus)	return OP_PLUS;
	if (x == P_conc)	return OP_CONC;
	if (x == P_nconc)	return OP_NCONC;
	if (x == P_sconc)	return OP_SCONC;
	if (x == P_vconc)	return OP_VCONC;
	return -1;
}

int lsubr1(cell x) {
	if (x == P_bitop)	return OP_BITOP;
	if (x == P_max)		return OP_MAX;
	if (x == P_min)		return OP_MIN;
	if (x == P_minus)	return OP_MINUS;
	if (x == P_less)	return OP_LESS;
	if (x == P_lteq)	return OP_LTEQ;
	if (x == P_equal)	return OP_EQUAL;
	if (x == P_grtr)	return OP_GRTR;
	if (x == P_gteq)	return OP_GTEQ;
	if (x == P_cless)	return OP_CLESS;
	if (x == P_clteq)	return OP_CLTEQ;
	if (x == P_cequal)	return OP_CEQUAL;
	if (x == P_cgrtr)	return OP_CGRTR;
	if (x == P_cgteq)	return OP_CGTEQ;
	return -1;
}

int subrp(cell x) {
	return	subr0(x) >= 0 ||
		subr1(x) >= 0 ||
		subr2(x) >= 0 ||
		subr3(x) >= 0 ||
		osubr0(x) >= 0 ||
		osubr1(x) >= 0 ||
		lsubr0(x) >= 0 ||
		lsubr1(x) >= 0;
}

void compexpr(cell x, int t);

void compprog(cell x, int t) {
	x = cdr(x);
	if (NIL == x) {
		emitq(NIL);
		return;
	}
	while (cdr(x) != NIL) {
		compexpr(car(x), 0);
		x = cdr(x);
	}
	compexpr(car(x), t);
}

void compsetq(cell x) {
	compexpr(caddr(x), 0);
	if (caadr(x) == I_ref) {
		emitop(OP_SETREF);
		emitarg(fixval(cadadr(x)));
	}
	else if (caadr(x) == I_arg) {
		emitop(OP_SETARG);
		emitarg(fixval(cadadr(x)));
	}
	else {
		error("oops: unknown location in setq", x);
	}
}

void compif(cell x, int t, int star) {
	compexpr(cadr(x), 0);
	emitop(star? OP_BRT: OP_BRF);
	cpushval(Here);
	emitarg(0);
	compexpr(caddr(x), t);
	if (cdddr(x) != NIL) {
		emitop(OP_JMP);
		cpushval(Here);
		emitarg(0);
		swap();
		patch(cpopval(), Here);
		compexpr(cadddr(x), t);
	}
	patch(cpopval(), Here);
}

void setupenv(cell m) {
	while (m != NIL) {
		if (caar(m) == I_e)
			emitop(OP_CPREF);
		else if (caar(m) == I_a)
			emitop(OP_CPARG);
		else
			error("oops: unknown location in closure", m);
		emitarg(fixval(cadar(m)));
		emitarg(fixval(caddar(m)));
		m = cdr(m);
	}
}

cell dottedp(cell x) {
	while (pairp(x)) x = cdr(x);
	return x != NIL;
}

void compcls(cell x) {
	int	a, na;
	cell	b, m;

	emitop(OP_JMP);
	cpushval(Here);
	emitarg(0);
	a = Here;
	na = length(flatargs(cadr(x)));
	if (dottedp(cadr(x))) {
		emitop(OP_ENTCOL);
		emitarg(na-1);
	}
	else {
		emitop(OP_ENTER);
		emitarg(na);
	}
	b = cons(S_prog, cdddr(x));
	protect(b);
	compexpr(b, 1);
	unprot(1);
	emitop(OP_RETURN);
	patch(cpopval(), Here);
	m = caddr(x);
	if (m != NIL) {
		emitop(OP_MKENV);
		emitarg(length(m));
		setupenv(m);
	}
	else {
		emitop(OP_PROPENV);
	}
	emitop(OP_CLOSURE);
	emitarg(a);
}

void compapply(cell x, int t) {
	cell	xs;

	xs = reverse(cddr(x));
	protect(xs);
	compexpr(car(xs), 0);
	for (xs = cdr(xs); xs != NIL; xs = cdr(xs)) {
		emitop(OP_PUSH);
		compexpr(car(xs), 0);
		emitop(OP_CONS);
	}
	emitop(OP_PUSH);
	unprot(1);
	compexpr(cadr(x), 0);
	emitop(t? OP_APPLIST: OP_APPLIS);
}

void compapp(cell x, int t) {
	cell	xs;

	xs = reverse(cdr(x));
	protect(xs);
	while (xs != NIL) {
		compexpr(car(xs), 0);
		emitop(OP_PUSH);
		xs = cdr(xs);
	}
	unprot(1);
	emitop(OP_PUSHVAL);
	emitarg(length(cdr(x)));
	compexpr(car(x), 0);
	emitop(t? OP_TAILAPP: OP_APPLY);
}

void compsubr0(cell x, int op) {
	ckargs(x, 0, 0);
	emitop(op);
}

void compsubr1(cell x, int op) {
	ckargs(x, 1, 1);
	compexpr(cadr(x), 0);
	emitop(op);
	if (OP_CATCHSTAR == op) emitop(OP_APPLY);
}

void compsubr2(cell x, int op) {
	ckargs(x, 2, 2);
	compexpr(caddr(x), 0);
	emitop(OP_PUSH);
	compexpr(cadr(x), 0);
	emitop(op);
}

void compsubr3(cell x, int op) {
	ckargs(x, 3, 3);
	compexpr(cadddr(x), 0);
	emitop(OP_PUSH);
	compexpr(caddr(x), 0);
	emitop(OP_PUSH);
	compexpr(cadr(x), 0);
	emitop(op);
}

void composubr0(cell x, int op) {
	ckargs(x, 0, 1);
	if (NIL == cdr(x))
		emitop(OP_INPORT);
	else
		compexpr(cadr(x), 0);
	emitop(op);
}

cell	Blank = NIL;
cell	Zero = NIL;
cell	One = NIL;
cell	Ten = NIL;

void composubr1(cell x, int op) {
	ckargs(x, 1, 2);
	if (NIL == cddr(x)) {
		if (OP_ERROR == op) {
			/**/
		}
		else if (OP_MKSTR == op) {
			emitq(Blank);
		}
		else if (OP_MKVEC == op) {
			emitq(NIL);
		}
		else if (OP_OPEN_OUTFILE == op) {
			emitq(NIL);
		}
		else if (OP_NUMSTR == op || OP_STRNUM == op) {
			emitq(Ten);
		}
		else if (OP_WRITEC == op ||
			 OP_PRIN == op ||
			 OP_PRINC == op)
		{
			emitop(OP_OUTPORT);
		}
	}
	else {
		if (OP_ERROR == op) op = OP_ERROR2;
		compexpr(caddr(x), 0);
	}
	emitop(OP_PUSH);
	compexpr(cadr(x), 0);
	emitop(op);
}

void complsubr0(cell x, int op) {
	if (NIL == cdr(x)) {
		if (OP_PLUS == op)
			emitq(Zero);
		else if (OP_TIMES == op)
			emitq(One);
		else if (OP_VCONC == op)
			emitq(Nullvec);
		else if (OP_SCONC == op)
			emitq(Nullstr);
		else if (OP_CONC == op)
			emitq(NIL);
		else if (OP_NCONC == op)
			emitq(NIL);
	}
	else if (NIL == cddr(x)) {
		compexpr(cadr(x), 0);
	}
	else if (OP_CONC == op || OP_SCONC == op ||
		 OP_VCONC == op || OP_NCONC == op)
	{
		x = reverse(cdr(x));
		protect(x);
		emitq(NIL);
		while (x != NIL) {
			emitop(OP_PUSH);
			compexpr(car(x), 0);
			emitop(OP_CONS);
			x = cdr(x);
		}
		unprot(1);
		emitop(op);
	}
	else {
		x = cdr(x);
		protect(x);
		compexpr(car(x), 0);
		x = cdr(x);
		while (x != NIL) {
			emitop(OP_PUSH);
			compexpr(car(x), 0);
			emitop(op);
			x = cdr(x);
		}
		unprot(1);
	}
}

void compbitop(cell x) {
	if (NIL == cddr(x) || NIL == cdddr(x))
		error("bitop: too few arguments", cdr(x));
	compexpr(cadr(x), 0);
	emitop(OP_PUSH);
	x = cddr(x);
	compexpr(car(x), 0);
	for (x = cdr(x); x != NIL; x = cdr(x)) {
		emitop(OP_PUSH);
		compexpr(car(x), 0);
		emitop(OP_BITOP);
	}
	emitop(OP_DROP);
}

void complsubr1(cell x, int op) {
	ckargs(x, 1, -1);
	if (OP_BITOP == op) {
		compbitop(x);
		return;
	}
	if (NIL == cddr(x)) {
		if (OP_MIN == op || OP_MAX == op) {
			compexpr(cadr(x), 0);
		}
		else if (OP_MINUS == op) {
			compexpr(cadr(x), 0);
			emitop(OP_NEGATE);
		}
		else {
			emitq(TRUE);
		}
	}
	else {
		if (op != OP_MINUS && op != OP_MIN && op != OP_MAX) {
			emitop(OP_PUSHTRUE);
		}
		x = cdr(x);
		compexpr(car(x), 0);
		for (x = cdr(x); x != NIL; x = cdr(x)) {
			emitop(OP_PUSH);
			compexpr(car(x), 0);
			emitop(op);
		}
		if (op != OP_MINUS && op != OP_MIN && op != OP_MAX)
			emitop(OP_POP);
	}
}

void compexpr(cell x, int t) {
	int	op;
	cell	y;

	if (atomp(x)) {
		emitq(x);
	}
	else if (car(x) == S_quote) {
		emitq(cadr(x));
	}
	else if (car(x) == I_arg) {
		emitop(OP_ARG);
		emitarg(fixval(cadr(x)));
	}
	else if (car(x) == I_ref) {
		emitop(OP_REF);
		emitarg(fixval(cadr(x)));
		y = htlookup(Symhash, caddr(x));
		if (UNDEF == y)
			emitarg(0);
		else
			emitarg(fixval(cdr(y)));
	}
	else if (car(x) == S_if) {
		compif(x, t, 0);
	}
	else if (car(x) == S_ifstar) {
		compif(x, t, 1);
	}
	else if (car(x) == I_closure) {
		compcls(x);
	}
	else if (car(x) == S_prog) {
		compprog(x, t);
	}
	else if (car(x) == S_setq) {
		compsetq(x);
	}
	else if (car(x) == S_apply) {
		compapply(x, t);
	}
	else if (car(x) == S_macro) {
		compexpr(caddr(x), 0);
		emitop(OP_MACRO);
		y = htlookup(Symhash, cadr(x));
		if (UNDEF == y) error("oops: unknown name in MACRO", cadr(x));
		emitarg(fixval(cdr(y)));
	}
	else if ((op = subr0(car(x))) >= 0) {
		compsubr0(x, op);
	}
	else if ((op = subr1(car(x))) >= 0) {
		compsubr1(x, op);
	}
	else if ((op = subr2(car(x))) >= 0) {
		compsubr2(x, op);
	}
	else if ((op = subr3(car(x))) >= 0) {
		compsubr3(x, op);
	}
	else if ((op = osubr0(car(x))) >= 0) {
		composubr0(x, op);
	}
	else if ((op = osubr1(car(x))) >= 0) {
		composubr1(x, op);
	}
	else if ((op = lsubr0(car(x))) >= 0) {
		complsubr0(x, op);
	}
	else if ((op = lsubr1(car(x))) >= 0) {
		complsubr1(x, op);
	}
	else { /* application */
		compapp(x, t);
	}
}

cell subprog(cell x, int k) {
	cell	n;
	byte	*sx, *sn;
	int	i, j;

	n = mkstr(NULL, k);
	sx = string(x);
	sn = string(n);
	j = 0;
	for (i=0; i<k; i++) {
		sn[j] = sx[i];
		j++;
	}
	return n;
}

cell compile(cell x) {
	cell	n;

	Emitbuf = mkatom(T_BYTECODE, mkstr(NULL, CHUNKSIZE));
	Here = 0;
	Cts = NIL;
	compexpr(x, 0);
	emitop(OP_HALT);
	n = mkatom(T_BYTECODE, subprog(cdr(Emitbuf), Here));
	Emitbuf = NIL;
	return n;
}

/*
 * Macro expander
 */

cell	Macros = NIL;

void newmacro(int id, cell fn) {
	cell	n, name;

	if (!closurep(fn)) expect("macro", "closure", fn);
	name = vector(Symbols)[id];
	n = assq(name, Macros);
	if (NIL == n) {
		n = cons(name, fn);
		Macros = cons(n, Macros);
	}
	else {
		cdr(n) = fn;
	}
}

cell expand(cell x, int r);

cell mapexp(cell x, int r) {
	cell	p, n, new;

	protect(x);
	protect(n = NIL);
	p = x;
	while (pairp(p)) {
		new = expand(car(p), r);
		n = cons(new, n);
		car(Protected) = n;
		p = cdr(p);
	}
	if (p != NIL) error("dotted list in program", x);
	n = nreverse(unprot(1));
	unprot(1);
	return n;
}

cell zip(cell a, cell b) {
	cell	n, p;

	protect(n = NIL);
	while (a != NIL && b != NIL) {
		p = cons(car(a), car(b));
		n = cons(p, n);
		car(Protected) = n;
		a = cdr(a);
		b = cdr(b);
	}
	unprot(1);
	return nreverse(n);
}

cell expanddef(cell x);

cell expandbody(cell x) {
	cell	n, vs, as;

	protect(vs = NIL);
	protect(as = NIL);
	while (	pairp(x) &&
		pairp(car(x)) &&
		(caar(x) == S_def ||
		 caar(x) == S_defun))
	{
		if (caar(x) == S_def) {
			n = car(x);
			vs = cons(cadr(n), vs);
			cadr(Protected) = vs;
			n = cons(caddr(n), NIL);
			as = cons(n, as);
			car(Protected) = as;
		}
		else {
			n = expanddef(car(x));
			protect(n);
			vs = cons(cadr(n), vs);
			caddr(Protected) = vs;
			n = cons(caddr(n), NIL);
			as = cons(n, as);
			cadr(Protected) = as;
			unprot(1);
		}
		x = cdr(x);
	}
	if (NIL == vs) {
		unprot(2);
		return x;
	}
	as = car(Protected) = nreverse(as);
	vs = cadr(Protected) = nreverse(vs);
	n = cons(zip(vs, as), x);
	n = cons(S_labels, n);
	n = cons(n, NIL);
	unprot(2);
	return n;
}

cell expanddef(cell x) {
	char	b[100];
	cell	n;

	if (!pairp(cadr(x))) {
		sprintf(b, "%s: expected signature", symname(car(x)));
		error(b, cadr(x));
	}
	n = cons(cdadr(x), expandbody(cddr(x)));
	n = cons(S_lambda, n);
	n = cons(n, NIL);
	n = cons(caadr(x), n);
	n = cons(car(x) == S_defun? S_def: S_macro, n);
	return n;
}

volatile int	Mxlev = 0;

cell eval(cell x, int r);

cell expand(cell x, int r) {
	cell	n, m;

	if (Mxlev < 0) error("interrupted", UNDEF);
	if (Mxlev > MXMAX)
		error("too many levels of macro expansion", UNDEF);
	if (atomp(x)) {
		return x;
	}
	if (car(x) == S_quote) {
		return x;
	}
	Mxlev++;
	if (car(x) == S_lambda) {
		protect(x);
		n = mapexp(cddr(x), r);
		n = cons(cadr(x), n);
		n = cons(car(x), n);
		unprot(1);
		Mxlev--;
		return n;
	}
	if (car(x) == S_defun || car(x) == S_defmac) {
		protect(x);
		x = expanddef(x);
		car(Protected) = x;
		x = expand(x, r);
		unprot(1);
		Mxlev--;
		return x;
	}
	if (	symbolp(car(x)) &&
		(m = assq(car(x), Macros)) != NIL)
	{
		protect(x);
		n = cons(cdr(x), NIL);
		n = cons(S_quote, n);
		n = cons(n, NIL);
		n = cons(cdr(m), n);
		n = cons(S_apply, n);
		x = eval(n, 1);
		car(Protected) = x;
		if (r) x = expand(x, r);
		unprot(1);
		Mxlev--;
		return x;
	}
	x = mapexp(x, r);
	Mxlev--;
	return x;
}

/*
 * Inline functions, arithmetics
 */

void fixover(char *who, cell x, cell y) {
	char	b[100];

	sprintf(b, "%s: fixnum overflow", who);
	error(b, cons(x, cons(y, NIL)));
}

cell add(cell x, cell y) {
	if (!fixp(x)) expect("+", "fixnum", x);
	if (!fixp(y)) expect("+", "fixnum", y);
	if (add_ovfl(fixval(x), fixval(y))) fixover("+", x, y);
	return mkfix(fixval(x) + fixval(y));
}

cell xsub(cell x, cell y) {
	if (!fixp(x)) expect("-", "fixnum", x);
	if (!fixp(y)) expect("-", "fixnum", y);
	if (sub_ovfl(fixval(y), fixval(x))) fixover("+", y, x);
	return mkfix(fixval(y) - fixval(x));
}

cell mul(cell x, cell y) {
	int	a, b;

	if (!fixp(x)) expect("*", "fixnum", x);
	if (!fixp(y)) expect("*", "fixnum", y);
	a = fixval(x);
	b = fixval(y);
	/*
	 * Overflow of a*b is undefined, sooo
	 */
	/* Shortcuts, also protect later division */
	if (0 == a || 0 == b) return Zero;
	if (1 == a) return y;
	if (1 == b) return x;
	/* abs(INT_MIN) is undefined using two's complement, so */
	if (INT_MIN == a || INT_MIN == b) fixover("*", x, y);
	/* Catch the rest */
	/* Bug: result may not be INT_MIN */
	if (abs(a) > INT_MAX / abs(b)) fixover("*", x, y);
	return mkfix(a * b);
}

cell intdiv(cell x, cell y) {
	if (!fixp(x)) expect("div", "fixnum", x);
	if (!fixp(y)) expect("div", "fixnum", y);
	if (0 == fixval(y)) error("div: divide by zero", UNDEF);
	return mkfix(fixval(x) / fixval(y));
}

cell intrem(cell x, cell y) {
	if (!fixp(x)) expect("rem", "fixnum", x);
	if (!fixp(y)) expect("rem", "fixnum", y);
	if (0 == fixval(y)) error("rem: divide by zero", UNDEF);
	return mkfix(fixval(x) % fixval(y));
}

#define stackset(n,v)	(vector(Rts)[n] = (v))

void grtr(cell x, cell y) {
	if (!fixp(x)) expect(">", "fixnum", x);
	if (!fixp(y)) expect(">", "fixnum", y);
	if (fixval(y) <= fixval(x)) stackset(Sp-1, NIL);
}

void gteq(cell x, cell y) {
	if (!fixp(x)) expect(">=", "fixnum", x);
	if (!fixp(y)) expect(">=", "fixnum", y);
	if (fixval(y) < fixval(x)) stackset(Sp-1, NIL);
}

void less(cell x, cell y) {
	if (!fixp(x)) expect("<", "fixnum", x);
	if (!fixp(y)) expect("<", "fixnum", y);
	if (fixval(y) >= fixval(x)) stackset(Sp-1, NIL);
}

void lteq(cell x, cell y) {
	if (!fixp(x)) expect("<=", "fixnum", x);
	if (!fixp(y)) expect("<=", "fixnum", y);
	if (fixval(y) > fixval(x)) stackset(Sp-1, NIL);
}

void equal(cell x, cell y) {
	if (!fixp(x)) expect("=", "fixnum", x);
	if (!fixp(y)) expect("=", "fixnum", y);
	if (fixval(y) != fixval(x)) stackset(Sp-1, NIL);
}

cell bitop(cell x, cell y, cell o) {
	uint	op, a, b;
	int	i;

	if (!fixp(o)) expect("bitop", "fixnum", o);
	if (!fixp(x)) expect("bitop", "fixnum", x);
	if (!fixp(y)) expect("bitop", "fixnum", y);
	op = fixval(o);
	b = fixval(x);
	a = i = fixval(y);
	switch (op) {
	case  0: a =  0;        break;
	case  1: a =   a &  b;  break;
	case  2: a =   a & ~b;  break;
	case  3: /* a =   a; */ break;
	case  4: a =  ~a &  b;  break;
	case  5: a =        b;  break;
	case  6: a =   a ^  b;  break;
	case  7: a =   a |  b;  break;
	case  8: a = ~(a |  b); break;
	case  9: a = ~(a ^  b); break;
	case 10: a =       ~b;  break;
	case 11: a =   a | ~b;  break;
	case 12: a =  ~a;       break;
	case 13: a =  ~a |  b;  break;
	case 14: a = ~(a &  b); break;
	case 15: a = ~0;        break;
	case 16: a = a  <<  b;  break;
	case 17: a = i  >>  b;  break;
	case 18: a = a  >>  b;  break;
	default: error("bitop: invalid opcode", o);
		 break;
	}
	return mkfix(a);
}

/*
 * Inline functions, characters
 */

void cless(cell x, cell y) {
	if (!charp(x)) expect("c<", "char", x);
	if (!charp(y)) expect("c<", "char", y);
	if (charval(y) >= charval(x)) stackset(Sp-1, NIL);
}

void clteq(cell x, cell y) {
	if (!charp(x)) expect("c<=", "char", x);
	if (!charp(y)) expect("c<=", "char", y);
	if (charval(y) > charval(x)) stackset(Sp-1, NIL);
}

void cequal(cell x, cell y) {
	if (!charp(x)) expect("c=", "char", x);
	if (!charp(y)) expect("c=", "char", y);
	if (charval(y) != charval(x)) stackset(Sp-1, NIL);
}

void cgrtr(cell x, cell y) {
	if (!charp(x)) expect("c>", "char", x);
	if (!charp(y)) expect("c>", "char", y);
	if (charval(y) <= charval(x)) stackset(Sp-1, NIL);
}

void cgteq(cell x, cell y) {
	if (!charp(x)) expect("c>=", "char", x);
	if (!charp(y)) expect("c>=", "char", y);
	if (charval(y) < charval(x)) stackset(Sp-1, NIL);
}

#define whitespc(c) \
	(' '  == (c) || \
	 '\t' == (c) || \
	 '\n' == (c) || \
	 '\r' == (c) || \
	 '\f' == (c))

/*
 * Inline functions, strings
 */

int scomp(cell x, cell y) {
	int	kx, ky;

	kx = stringlen(x);
	ky = stringlen(y);
	if (kx == ky) return memcmp(string(x), string(y), kx);
	return memcmp(string(x), string(y), 1+(kx<ky? kx: ky));
}

int memcmp_ci(char *a, char *b, int k) {
	int	i, d;

	for (i=0; i<k; i++) {
		d = tolower(a[i]) - tolower(b[i]);
		if (d) return d;
	}
	return 0;
}

int scomp_ci(cell x, cell y) {
	int	kx, ky;

	kx = stringlen(x);
	ky = stringlen(y);
	if (kx == ky)
		return memcmp_ci((char *) string(x),
				(char *) string(y), kx);
	return memcmp_ci((char *) string(x), (char *) string(y),
			1+(kx<ky? kx: ky));
}

cell sless(cell x, cell y) {
	if (!string(x)) expect("s<", "string", x);
	if (!string(y)) expect("s<", "string", y);
	return scomp(x, y) < 0? TRUE: NIL;
}

cell slteq(cell x, cell y) {
	if (!string(x)) expect("s<=", "string", x);
	if (!string(y)) expect("s<=", "string", y);
	return scomp(x, y) <= 0? TRUE: NIL;
}

cell sequal(cell x, cell y) {
	if (!string(x)) expect("s=", "string", x);
	if (!string(y)) expect("s=", "string", y);
	if (stringlen(x) != stringlen(y)) return NIL;
	return scomp(x, y) == 0? TRUE: NIL;
}

cell sgrtr(cell x, cell y) {
	if (!string(x)) expect("s>", "string", x);
	if (!string(y)) expect("s>", "string", y);
	return scomp(x, y) > 0? TRUE: NIL;
}

cell sgteq(cell x, cell y) {
	if (!string(x)) expect("s>=", "string", x);
	if (!string(y)) expect("s>=", "string", y);
	return scomp(x, y) >= 0? TRUE: NIL;
}

cell siless(cell x, cell y) {
	if (!string(x)) expect("si<", "string", x);
	if (!string(y)) expect("si<", "string", y);
	return scomp_ci(x, y) < 0? TRUE: NIL;
}

cell silteq(cell x, cell y) {
	if (!string(x)) expect("si<=", "string", x);
	if (!string(y)) expect("si<=", "string", y);
	return scomp_ci(x, y) <= 0? TRUE: NIL;
}

cell siequal(cell x, cell y) {
	if (!string(x)) expect("si=", "string", x);
	if (!string(y)) expect("si=", "string", y);
	if (stringlen(x) != stringlen(y)) return NIL;
	return scomp_ci(x, y) == 0? TRUE: NIL;
}

cell sigrtr(cell x, cell y) {
	if (!string(x)) expect("si>", "string", x);
	if (!string(y)) expect("si>", "string", y);
	return scomp_ci(x, y) > 0? TRUE: NIL;
}

cell sigteq(cell x, cell y) {
	if (!string(x)) expect("si>=", "string", x);
	if (!string(y)) expect("si>=", "string", y);
	return scomp_ci(x, y) >= 0? TRUE: NIL;
}

cell b_mkstr(cell x, cell a) {
	cell	n;
	int	i, c, k;
	byte	*s;

	if (!fixp(x)) expect("mkstr", "fixnum", x);
	if (!charp(a)) expect("mkstr", "char", a);
	c = charval(a);
	k = fixval(x);
	n = mkstr(NULL, k);
	s = string(n);
	for (i=0; i<k; i++) s[i] = c;
	return n;
}

cell sconc(cell x) {
	cell	p, n;
	int	k, m;
	byte	*s;

	k = 0;
	for (p = x; p != NIL; p = cdr(p)) {
		if (!stringp(car(p)))
			expect("sconc", "string", car(p));
		k += stringlen(car(p))-1;
	}
	n = mkstr(NULL, k);
	s = string(n);
	k = 0;
	for (p = x; p != NIL; p = cdr(p)) {
		m = stringlen(car(p));
		memcpy(&s[k], string(car(p)), m);
		k += m-1;
	}
	return n;
}

cell sref(cell s, cell n) {
	int	i;

	if (!stringp(s)) expect("sref", "string", s);
	if (!fixp(n)) expect("sref", "fixnum", n);
	i = fixval(n);
	if (i < 0 || i >= stringlen(s)-1)
		error("sref: index out of range", n);
	return mkchar(string(s)[i]);
}

void sset(cell s, cell n, cell r) {
	int	i;

	if (!stringp(s)) expect("sset", "string", s);
	if (constp(s)) error("sset: immutable", s);
	if (!fixp(n)) expect("sset", "fixnum", n);
	if (!charp(r)) expect("sset", "char", r);
	i = fixval(n);
	if (i < 0 || i >= stringlen(s)-1)
		error("sset: index out of range", n);
	string(s)[i] = charval(r);
}

cell substr(cell s, cell n0, cell n1) {
	int	k, k0, k1, i, j;
	cell	n;
	byte	*s0, *s1;

	if (!stringp(s)) expect("substr", "string", s);
	if (!fixp(n0)) expect("substr", "fixnum", n0);
	if (!fixp(n1)) expect("substr", "fixnum", n1);
	k0 = fixval(n0);
	k1 = fixval(n1);
	if (k0 < 0 || k1 < 0 || k0 > k1 || k1 >= stringlen(s))
		error("substr: invalid range", cons(n0, cons(n1, NIL)));
	k = k1-k0;
	n = mkstr(NULL, k);
	j = 0;
	s0 = string(s);
	s1 = string(n);
	for (i=k0; i<k1; i++) {
		s1[j] = s0[i];
		j++;
	}
	s1[j] = 0;
	return n;
}

void sfill(cell x, cell a) {
	int	c, i, k;
	byte	*s;

	if (!stringp(x)) expect("sfill", "string", x);
	if (constp(x)) error("sfill: immutable", x);
	if (!charp(a)) expect("sfill", "char", a);
	c = charval(a);
	k = stringlen(x)-1;
	s = string(x);
	for (i=0; i<k; i++) s[i] = c;
}

/*
 * Inline functions, vectors
 */

cell b_mkvec(cell x, cell a) {
	cell	n;
	int	i, k;
	cell	*v;

	if (!fixp(x)) expect("mkvec", "fixnum", x);
	k = fixval(x);
	n = mkvec(k);
	v = vector(n);
	for (i=0; i<k; i++) v[i] = a;
	return n;
}

cell vconc(cell x) {
	cell	p, n, *v;
	int	k, m;

	k = 0;
	for (p = x; p != NIL; p = cdr(p)) {
		if (!vectorp(car(p)))
			expect("vconc", "vector", car(p));
		k += veclen(car(p));
	}
	n = mkvec(k);
	v = vector(n);
	k = 0;
	for (p = x; p != NIL; p = cdr(p)) {
		m = veclen(car(p));
		memcpy(&v[k], vector(car(p)), m*sizeof(cell));
		k += m;
	}
	return n;
}

cell vref(cell x, cell n) {
	int	i;

	if (!vectorp(x)) expect("vref", "vector", x);
	if (!fixp(n)) expect("vref", "fixnum", n);
	i = fixval(n);
	if (i < 0 || i >= veclen(x))
		error("vref: index out of range", n);
	return vector(x)[i];
}

void vfill(cell x, cell a) {
	int	i, k;
	cell	*v;

	if (!vectorp(x)) expect("vfill", "vector", x);
	if (constp(x)) error("vfill: immutable", x);
	k = veclen(x);
	v = vector(x);
	for (i=0; i<k; i++) v[i] = a;
}

void vset(cell v, cell n, cell r) {
	int	i;

	if (!vectorp(v)) expect("vset", "vector", v);
	if (constp(v)) error("vset: immutable", v);
	if (!fixp(n)) expect("vset", "fixnum", n);
	i = fixval(n);
	if (i < 0 || i >= veclen(v))
		error("vset: index out of range", n);
	vector(v)[i] = r;
}

cell subvec(cell v, cell n0, cell n1) {
	int	k, k0, k1, i, j;
	cell	n;
	cell	*v0, *v1;

	if (!vectorp(v)) expect("subvec", "vector", v);
	if (!fixp(n0)) expect("subvec", "fixnum", n0);
	if (!fixp(n1)) expect("subvec", "fixnum", n1);
	k0 = fixval(n0);
	k1 = fixval(n1);
	if (k0 < 0 || k1 < 0 || k0 > k1 || k1 > veclen(v))
		error("subvec: invalid range", cons(n0, cons(n1, NIL)));
	k = k1-k0;
	n = mkvec(k);
	j = 0;
	v0 = vector(v);
	v1 = vector(n);
	for (i=k0; i<k1; i++) {
		v1[j] = v0[i];
		j++;
	}
	return n;
}

/*
 * Inline functions, file I/O
 */

cell existsp(char *s) {
	FILE	*f;

	f = fopen(s, "r");
	if (f != NULL) fclose(f);
	return NULL == f? NIL: TRUE;
}

cell openfile(cell x, int mode) {
	int	p;

	switch (mode) {
	case 0:
		p = open_inport((char *) string(x));
		break;
	case 1:
		p = open_outport((char *) string(x), 0);
		break;
	case 2:
		p = open_outport((char *) string(x), 1);
		break;
	}
	if (p < 0) {
		if (0 == mode)
			error("open-infile: cannot open", x);
		else
			error("open-outfile: cannot open", x);
	}
	return mkport(p, 0 == mode? T_INPORT: T_OUTPORT);
}

cell b_readc(cell p, int rej) {
	int	pp, c;

	pp = Inport;
	if (p != pp) set_inport(p);
	c = readc();
	if (rej) rejectc(c);
	if (p != pp) set_inport(pp);
	if (EOF == c) return EOFMARK;
	return mkchar(c);
}

cell b_read(cell ps) {
	int	pp;
	cell	n;

	if (stringp(ps)) {
		Instr = (char *) string(ps);
		Rejected = -1;
		n = xread();
		Instr = NULL;
		if (Readerr) return mkstr(Readerr, strlen(Readerr));
		return cons(n, NIL);
	}
	ps = portno(ps);
	pp = Inport;
	if (ps != pp) set_inport(ps);
	n = xread();
	if (ps != pp) set_inport(pp);
	return n;
}

void b_prin(cell x, int p, int sl) {
	int	pp;

	pp = Outport;
	if (p != pp) set_outport(p);
	prex(sl, x, 0);
	if (p != pp) set_outport(pp);
}

cell format(cell x) {
	cell	n;

	Outstr = mkstr(NULL, 1000);
	Outmax = 1000;
	Outptr = 0;
	prex(1, x, 0);
	n = mkstr(NULL, Outptr);
	memcpy(string(n), string(Outstr), Outptr+1);
	Outstr = NIL;
	return n;
}

void b_writec(int c, cell p) {
	int	pp;

	pp = Outport;
	if (p != pp) set_outport(p);
	writec(c);
	if (p != pp) set_outport(pp);
}

void b_rename(int old, int new) {
	if (!stringp(old)) expect("rename", "string", old);
	if (!stringp(new)) expect("rename", "string", new);
	if (rename((char *) string(old), (char *) string(new)) < 0)
		error("rename: cannot rename",
			cons(old, cons(new, NIL)));
}

/*
 * Inline functions, lists
 */

cell lconc(cell x) {
	cell	p, q, n, m;
	int	k;

	if (NIL == cdr(x)) return car(x);
	protect(n = cons(NIL, NIL));
	k = 0;
	for (p = x; cdr(p) != NIL; p = cdr(p)) {
		if (NIL == car(p)) continue;
		for (q = car(p); q != NIL; q = cdr(q)) {
			if (!pairp(q))
				expect("conc", "list", car(p));
			if (k != 0) {
				m = cons(NIL, NIL);
				cdr(n) = m;
				n = cdr(n);
			}
			car(n) = car(q);
			k++;
		}
	}
	m = unprot(1);
	if (0 == k) return car(p);
	cdr(n) = car(p);
	return m;
}

cell nlconc(cell x) {
	cell	p, q;

	while (pairp(cdr(x)) && NIL == car(x)) x = cdr(x);
	if (NIL == cdr(x)) return car(x);
	for (p = x; cdr(p) != NIL; p = cdr(p)) {
		if (NIL == car(p)) continue;
		if (constp(car(p))) error("nconc: immutable", car(p));
		for (q = car(p); cdr(q) != NIL; q = cdr(q)) {
			if (!pairp(q))
				expect("nconc", "list", car(p));
		}
		while (pairp(cdr(p)) && NIL == cadr(p))
			p = cdr(p);
		if (NIL == cdr(p)) break;
		cdr(q) = cadr(p);
	}
	return car(x);
}

/*
 * Inline functions, type conversion
 */

cell b_symbol(cell x) {
	cell	y, n, k;

	y = findsym((char *) string(x));
	if (y != NIL) return y;
	/*
	 * Cannot pass content to mksym(), because
	 * string(x) may move during GC.
	 */
	k = stringlen(x);
	n = mksym("", k-1);
	memcpy(symname(n), string(x), k);
	return intern(n);
}

cell b_symname(cell x) {
	cell    n, k;

	/*
	 * Cannot pass name to mkstr(), because
	 * symname(x) may move during GC.
	*/
	k = symlen(x);
	n = mkstr(NULL, k-1);
	Tag[n] |= CONST_TAG;
	memcpy(string(n), symname(x), k);
	return n;
}

cell liststr(cell x) {
	cell	n, v;
	int	k;
	byte	*s;

	k = 0;
	for (n = x; n != NIL; n = cdr(n))
		k++;
	v = mkstr(NULL, k);
	s = string(v);
	for (n = x; n != NIL; n = cdr(n)) {
		if (atomp(n)) error("liststr: dotted list", x);
		if (!charp(car(n))) expect("liststr", "char", car(n));
		*s = charval(car(n));
		s++;
	}
	return v;
}

cell listvec(cell x, int veclit) {
	cell	n, v;
	int	k;
	cell	*p;
	char	*msg;

	msg = veclit? "vector literal contains a dot":
		      "listvec: dotted list";
	k = 0;
	for (n = x; n != NIL; n = cdr(n))
		k++;
	if (0 == k) return Nullvec;
	v = mkvec(k);
	if (veclit) tag(v) |= CONST_TAG;
	p = vector(v);
	for (n = x; n != NIL; n = cdr(n)) {
		if (atomp(n)) error(msg, x);
		*p = car(n);
		p++;
	}
	return v;
}

cell strlist(cell x) {
	cell	a, new;
	int	k, i;

	k = stringlen(x)-1;
	if (0 == k) return NIL;
	protect(a = cons(NIL, NIL));
	for (i=0; i<k; i++) {
		new = mkchar(string(x)[i]);
		car(a) = new;
		if (i < k-1) {
			new = cons(NIL, NIL);
			cdr(a) = new;
			a = cdr(a);
		}
	}
	return unprot(1);
}

cell veclist(cell x) {
	cell	a, new;
	int	k, i;

	k = veclen(x);
	if (0 == k) return NIL;
	protect(a = cons(NIL, NIL));
	for (i=0; i<k; i++) {
		car(a) = vector(x)[i];
		if (i < k-1) {
			new = cons(NIL, NIL);
			cdr(a) = new;
			a = cdr(a);
		}
	}
	return unprot(1);
}

cell numstr(cell x, int r) {
	char	*p;

	if (r < 2 || r > 36)
		error("numstr: bad radix", mkfix(r));
	p = ntoa(fixval(x), r);
	return mkstr(p, strlen(p));
}

cell strnum(char *s, int r) {
	if (r < 2 || r > 36)
		error("strnum: bad radix", mkfix(r));
	return scanfix(s, r, 0);
}

/*
 * Inline functions, LOAD
 */

void	begin_rec(void);
void	end_rec(void);

void loadfile(char *s) {
	int	ldport, rdport, oline;
	cell	x;

	ldport = open_inport(s);
	if (ldport < 0)
		error("load: cannot open file",
			mkstr(s, strlen(s)));
	lock_port(ldport);
	rdport = Inport;
	oline = Line;
	Files = cons(mkstr(s, strlen(s)), Files);
	Line = 1;
	begin_rec();
	for (;;) {
		set_inport(ldport);
		x = xread();
		set_inport(rdport);
		if (EOFMARK == x) break;
		eval(x, 0);
	}
	end_rec();
	Files = cdr(Files);
	Line = oline;
	close_port(ldport);
}

void load(cell x) {
	char	path[TOKLEN+1];

	if (!stringp(x))
		expect("load", "string", x);
	if (stringlen(x) > TOKLEN)
		error("load: path too long", x);
	strcpy(path, (char *) string(x));
	loadfile(path);
}

/*
 * Heap image I/O
 */

struct imghdr {
	char	magic[5];		/* "LISP9"	*/
	char	version[8];		/* "yyyymmdd"	*/
	char	cell_size[1];		/* size + '0'	*/
	char	byte_order[4];		/* e.g. "4321"	*/
	char	pad[14];
};

char *xfwrite(void *buf, int siz, int n, FILE *f) {
	if (fwrite(buf, siz, n, f) != n)
		return "image file write error";
	return NULL;
}

void saveimg(char *path) {
	char	b[TOKLEN+1], *p;

	if (strlen(path)+7 >= TOKLEN)
		error("image path too long", UNDEF);
	strcpy(b, path);
	p = strrchr(b, '.');
	if (NULL == p) p = b+strlen(b);
	*p = 0;
	strcat(b, ".oimage");
	remove(b);
	rename(path, b);
}

cell *Imagevars[];

char *dumpimg(char *path) {
	FILE		*f;
	cell		n, **v;
	int		i;
	struct imghdr	m;
	char		*s;

	saveimg(path);
	f = fopen(path, "wb");
	if (NULL == f) return "cannot create image file";
	memset(&m, '_', sizeof(m));
	strncpy(m.magic, "LISP9", sizeof(m.magic));
	strncpy(m.version, VERSION, sizeof(m.version));
	m.cell_size[0] = sizeof(cell)+'0';
	n = 0x31323334L;
	memcpy(m.byte_order, &n, 4);
	if ((s = xfwrite(&m, sizeof(m), 1, f)) != NULL) {
		fclose(f);
		return s;
	}
	i = NNODES;
	if ((s = xfwrite(&i, sizeof(int), 1, f)) != NULL) {
		fclose(f);
		return s;
	}
	i = NVCELLS;
	if ((s = xfwrite(&i, sizeof(int), 1, f)) != NULL) {
		fclose(f);
		return s;
	}
	i = 0;
	v = Imagevars;
	while (v && v[i]) {
		if ((s = xfwrite(v[i], sizeof(cell), 1, f)) != NULL) {
			fclose(f);
			return s;
		}
		i++;
	}
	if (	fwrite(Car, 1, sizeof(cell) * NNODES, f)
		 != sizeof(cell) * NNODES ||
		fwrite(Cdr, 1, sizeof(cell) * NNODES, f)
		 != sizeof(cell) * NNODES ||
		fwrite(Tag, 1, NNODES, f) != NNODES||
		fwrite(Vectors, 1, sizeof(cell) * NVCELLS, f)
		 != sizeof(cell) * NVCELLS)
	{
		fclose(f);
		return "image dump failed";
	}
	fclose(f);
	return NULL;
}

char *xfread(void *buf, int siz, int n, FILE *f) {
	if (fread(buf, siz, n, f) != n)
		return "image file read error";
	return NULL;
}

char *loadimg(char *path) {
	FILE		*f;
	cell		n, **v;
	int		i;
	struct imghdr	m;
	int		image_nodes, image_vcells;
	char		*s;

	f = fopen(path, "rb");
	if (NULL == f)
		return "could not open file";
	if ((s = xfread(&m, sizeof(m), 1, f)) != NULL)
		return s;
	if (memcmp(m.magic, "LISP9", sizeof(m.magic))) {
		fclose(f);
		return "imghdr match failed";
	}
	if (memcmp(m.version, VERSION, sizeof(m.version))) {
		fclose(f);
		return "wrong image version";
	}
	if (m.cell_size[0]-'0' != sizeof(cell)) {
		fclose(f);
		return "wrong cell size";
	}
	memcpy(&n, m.byte_order, sizeof(cell));
	if (n != 0x31323334L) {
		fclose(f);
		return "wrong byte order";
	}
	memset(Tag, 0, NNODES);
	if ((s = xfread(&image_nodes, sizeof(int), 1, f)) != NULL)
		return s;
	if ((s = xfread(&image_vcells, sizeof(int), 1, f)) != NULL)
		return s;
	if (image_nodes != NNODES) {
		fclose(f);
		return "wrong node pool size";
	}
	if (image_vcells != NVCELLS) {
		fclose(f);
		return "wrong vector pool size";
	}
	v = Imagevars;
	i = 0;
	while (v && v[i]) {
		if ((s = xfread(v[i], sizeof(cell), 1, f)) != NULL)
			return s;
		i++;
	}
	if (	(fread(Car, 1, sizeof(cell) * NNODES, f)
		  != sizeof(cell) * NNODES ||
		 fread(Cdr, 1, sizeof(cell) * NNODES, f)
		  != sizeof(cell) * NNODES ||
		 fread(Tag, 1, NNODES, f) != NNODES ||
		 fread(Vectors, 1, sizeof(cell) * NVCELLS, f)
		  != sizeof(cell) * NVCELLS ||
		 fgetc(f) != EOF))
	{
		fclose(f);
		return "wrong file size";
	}
	fclose(f);
	return NULL;
}

void dump_image(cell s) {
	char	*rc;

	rc = dumpimg((char *) string(s));
	if (rc != NULL) {
		remove((char *) string(s));
		error(rc, s);
	}
	bindset(S_imagefile, s);
}

/*
 * Inline functions, misc
 */

cell b_gc(void) {
	cell	n;

	gcv();
	n = cons(mkfix(NVCELLS-Freevec), NIL);
	protect(n);
	n = mkfix(length(Freelist));
	return cons(n, unprot(1));
}

cell gensym(void) {
	static int	id = 0;
	char		b[100];

	id++;
	sprintf(b, "G%d", id);
	return mksym(b, strlen(b));
}

cell untag(cell x) {
	if (specialp(x)) return x;
	if (tag(x) & VECTOR_TAG) return NIL;
	if (closurep(x)) return cdr(cadddr(x));
	return cdr(x);
}

/*
 * Abstract machine
 */

cell	Prog = NIL;

int	Ip = 0;

cell	Acc = NIL;

int	Sz = CHUNKSIZE;

cell	Rts = NIL;
int	Sp = -1,
	Fp = -1;

cell	E0 = NIL,
	Ep = NIL;

#define ins()		(string(cdr(Prog))[Ip])

#define op1()		fetcharg(string(cdr(Prog)), Ip+1)
#define op2()		fetcharg(string(cdr(Prog)), Ip+3)

#define skip(n)		(Ip += (n))
#define clear(n)	(Sp -= (n))

#define box(x)		cons((x), NIL)
#define boxref(x)	car(x)
#define boxset(x,v)	(car(x) = (v))

#define stackref(n)	(vector(Rts)[n])
#define stackset(n,v)	(vector(Rts)[n] = (v))

#define envbox(n)	(vector(Ep)[n])
#define argbox(n)	(stackref(Fp-(n)))

#define argref(n)	boxref(argbox(n))
#define arg(n)		boxref(stackref(Sp-(n)))

void stkalloc(int k) {
	cell	n, *vs, *vn;
	int	i;

	if (Sp + k >= Sz) {
		/* allocate multiples of CHUNKSIZE */
		if (k >= CHUNKSIZE) {
			k = Sp+k-Sz;
			k = CHUNKSIZE * (1 + (k / CHUNKSIZE));
		}
		else {
			k = CHUNKSIZE;
		}
		n = mkvec(Sz + k);
		vs = vector(Rts);
		vn = vector(n);
		for (i=0; i<=Sp; i++) vn[i] = vs[i];
		Sz += k;
		Rts = n;
	}
}

void push(cell x) {
	Tmp = x;
	stkalloc(1);
	Tmp = NIL;
	Sp++;
	stackset(Sp, x);
}

cell pop(void) {
	if (Sp < 0) error("oops: stack underflow", UNDEF);
	Sp--;
	return stackref(Sp+1);
}

cell closure(cell i, cell e) {
	cell	c;

	c = cons(Prog, NIL);
	c = cons(e, c);
	protect(c);
	c = cons(mkfix(i), c);
	unprot(1);
	return mkatom(T_CLOSURE, c);
}

#define closure_ip(c)	cadr(c)
#define closure_env(c)	caddr(c)
#define closure_prog(c)	cadddr(c)

int apply(int tail) {
	int	n, m, pn, pm, i;
	cell	k, e;

	if (!closurep(Acc))
		error("application of non-function", Acc);
	if (tail) {
		Ep = closure_env(Acc);
		Prog = closure_prog(Acc);
		m = fixval(stackref(Sp));
		n = fixval(stackref(Sp-m-4));
		pm = Sp-m;
		pn = Sp-m-n-4;
		if (n == m) {
			for (i=0; i<=m; i++)
				stackset(pn+i, stackref(pm+i));
			Fp = fixval(stackref(Sp-m-1));
			Sp -= n+2;
		}
		else {
			e = stackref(Sp-m-3);
			k = stackref(Sp-m-2);
			Fp = fixval(stackref(Sp-m-1));
			for (i=0; i<=m; i++)
				stackset(pn+i, stackref(pm+i));
			Sp -= n+2;
			stackset(Sp-1, e);
			stackset(Sp,   k);
		}
	}
	else {
		push(Ep);
		push(cons(mkfix(Ip+1), Prog));
		Ep = closure_env(Acc);
		Prog = closure_prog(Acc);
	}
	return fixval(closure_ip(Acc));
}

int conses(cell n) {
	int	k;

	for (k = 0; pairp(n); n = cdr(n))
		k++;
	return k;
}

int applis(int tail) {
	cell	a, p, new;
	int	k, i;

	a = boxref(stackref(Sp));
	if (!pairp(a) && a != NIL) error("apply: expected list", a);
	k = conses(a);
	stkalloc(k);
	Sp += k;
	i = Sp-1;
	for (p = a; p != NIL; p = cdr(p)) {
		if (atomp(p)) error("apply: dotted list", a);
		new = box(car(p));
		stackset(i, new);
		i--;
	}
	new = mkfix(k);
	stackset(Sp, new);
	return apply(tail);
}

int ret(void) {
	int	r, n;
	cell	*v;

	v = vector(Rts);
	Fp = fixval(v[Sp]);
	r = v[Sp-1];
	Prog = cdr(r);
	Ep = v[Sp-2];
	n = fixval(v[Sp-3]);
	Sp -= n+4;
	return fixval(car(r));
}

void entcol(int fix) {
	int	n, na, i, s, d;
	cell	a, x, new;

	na = fixval(stackref(Sp-2));
	if (na < fix)
		error("too few arguments", UNDEF);
	protect(a = NIL);
	i = Sp-fix-3;
	for (n = na-fix; n; n--) {
		x = cons(boxref(stackref(i)), NIL);
		if (NIL == a) {
			a = x;
			car(Protected) = a;
		}
		else {
			cdr(a) = x;
			a = cdr(a);
		}
		i--;
	}
	a = unprot(1);
	if (na > fix) {
		new = box(a);
		stackset(Sp-fix-3, new);
	}
	else {
		push(NIL);
		s = Sp - na - 3;
		d = Sp - na - 2;
		for (i = na + 2; i >= 0; i--)
			stackset(d+i, stackref(s+i));
		new = mkfix(1+fix);
		stackset(Sp-2, new);
		new = box(NIL);
		stackset(Sp-fix-3, new);
	}
	push(mkfix(Fp));
	Fp = Sp-4;
}

cell mkctag(void) {
	cell	n;

	n = cons(Ep, Prog);
	Tmp = n; n = cons(mkfix(Fp), n);
	Tmp = n; n = cons(mkfix(Sp), n);
	Tmp = n; n = cons(mkfix(Ip+2), n);
	Tmp = NIL;
	return mkatom(T_CATCHTAG, n);
}

int throw(cell ct, cell v) {
	if (!ctagp(ct)) expect("throw", "catch tag", ct);
	ct = cdr(ct);
	Ip = fixval(car(ct)); ct = cdr(ct);
	Sp = fixval(car(ct)); ct = cdr(ct);
	Fp = fixval(car(ct)); ct = cdr(ct);
	Ep = car(ct);         ct = cdr(ct);
	Prog = ct;
	Acc = v;
	return Ip;
}

int throwerr(cell ct) {
	cell	n;

	n = assq(S_errval, Glob);
	n = NIL == n? NIL: cadr(n);
	return throw(ct, n);
}

volatile int	Run = 0;
cell	Argv;

void run(cell x) {
	Acc = NIL;
	Prog = x;
	Ip = 0;
	if (setjmp(Errtag) != 0)
		Ip = throwerr(Handler);
	for (Run=1; Run;) {
	switch (ins()) {
	case OP_APPLIS:
		Ip = applis(0);
		break;
	case OP_APPLIST:
		Ip = applis(1);
		break;
	case OP_TAILAPP:
		Ip = apply(1);
		break;
	case OP_APPLY:
		Ip = apply(0);
		break;
	case OP_QUOTE:
		Acc = vector(Obarray)[op1()];
		skip(ISIZE1);
		break;
	case OP_ARG:
		Acc = argref(op1());
		skip(ISIZE1);
		break;
	case OP_REF:
		Acc = boxref(envbox(op1()));
		if (UNDEF == Acc)
			error("undefined symbol", vector(Symbols)[op2()]);
		if (Tp >= NTRACE) Tp = 0;
		Trace[Tp++] = op2();
		skip(ISIZE2);
		break;
	case OP_DROP:
		Sp--;
		skip(ISIZE0);
		break;
	case OP_POP:
		Acc = stackref(Sp);
		Sp--;
		skip(ISIZE0);
		break;
	case OP_PUSH:
		push(cons(Acc, NIL));
		skip(ISIZE0);
		break;
	case OP_PUSHTRUE:
		push(TRUE);
		skip(ISIZE0);
		break;
	case OP_PUSHVAL:
		push(mkfix(op1()));
		skip(ISIZE1);
		break;
	case OP_JMP:
		Ip = op1();
		break;
	case OP_BRF:
		if (NIL == Acc)
			Ip = op1();
		else
			skip(ISIZE1);
		break;
	case OP_BRT:
		if (NIL == Acc)
			skip(ISIZE1);
		else
			Ip = op1();
		break;
	case OP_HALT:
		return;
	case OP_CATCHSTAR:
		push(box(mkctag()));
		push(mkfix(1));
		skip(ISIZE0);
		break;
	case OP_THROWSTAR:
		Ip = throw(Acc, arg(0));
		break;
	case OP_MKENV:
		Acc = mkvec(op1());
		skip(ISIZE1);
		break;
	case OP_PROPENV:
		Acc = Ep;
		skip(ISIZE0);
		break;
	case OP_CPARG:
		vector(Acc)[op2()] = argbox(op1());
		skip(ISIZE2);
		break;
	case OP_CPREF:
		vector(Acc)[op2()] = envbox(op1());
		skip(ISIZE2);
		break;
	case OP_CLOSURE:
		Acc = closure(op1(), Acc);
		skip(ISIZE1);
		break;
	case OP_ENTER:
		if (fixval(stackref(Sp-2)) != op1())
			error("wrong number of arguments", UNDEF);
		push(mkfix(Fp));
		Fp = Sp-4;
		skip(ISIZE1);
		break;
	case OP_ENTCOL:
		entcol(op1());
		skip(ISIZE1);
		break;
	case OP_RETURN:
		Ip = ret();
		break;
	case OP_SETARG:
		boxset(argbox(op1()), Acc);
		skip(ISIZE1);
		break;
	case OP_SETREF:
		boxset(envbox(op1()), Acc);
		skip(ISIZE1);
		break;
	case OP_MACRO:
		newmacro(op1(), Acc);
		skip(ISIZE1);
		break;
	case OP_CMDLINE:
		Acc = Argv;
		skip(ISIZE0);
		break;
	case OP_QUIT:
		exit(EXIT_SUCCESS);
		skip(ISIZE0);
		break;
	case OP_OBTAB:
		Acc = Obarray;
		skip(ISIZE0);
		break;
	case OP_SYMTAB:
		Acc = Symbols;
		skip(ISIZE0);
		break;
	case OP_ERROR:
		if (!stringp(Acc)) expect("error", "string", Acc);
		error((char *) string(Acc), UNDEF);
		skip(ISIZE0);
		break;
	case OP_ERROR2:
		if (!stringp(Acc)) expect("error", "string", Acc);
		error((char *) string(Acc), arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_ERRPORT:
		Acc = mkport(Errport, T_OUTPORT);
		skip(ISIZE0);
		break;
	case OP_INPORT:
		Acc = mkport(Inport, T_INPORT);
		skip(ISIZE0);
		break;
	case OP_OUTPORT:
		Acc = mkport(Outport, T_OUTPORT);
		skip(ISIZE0);
		break;
	case OP_GC:
		Acc = b_gc();
		skip(ISIZE0);
		break;
	case OP_GENSYM:
		Acc = gensym();
		skip(ISIZE0);
		break;
	case OP_ABS:
		if (!fixp(Acc)) expect("abs", "fixnum", Acc);
		if (INT_MIN == fixval(Acc))
			error("abs: fixnum overflow", Acc);
		if (fixval(Acc) < 0) Acc = mkfix(-fixval(Acc));
		skip(ISIZE0);
		break;
	case OP_ALPHAC:
		if (!charp(Acc)) expect("alphac", "char", Acc);
		Acc = isalpha(charval(Acc))? TRUE: NIL;
		skip(ISIZE0);
		break;
	case OP_ATOM:
		Acc = pairp(Acc)? NIL: TRUE;
		skip(ISIZE0);
		break;
	case OP_CAR:
		if (!pairp(Acc)) expect("car", "pair", Acc);
		Acc = car(Acc);
		skip(ISIZE0);
		break;
	case OP_CDR:
		if (!pairp(Acc)) expect("cdr", "pair", Acc);
		Acc = cdr(Acc);
		skip(ISIZE0);
		break;
	case OP_CAAR:
		if (!pairp(Acc) || !pairp(car(Acc)))
			expect("caar", "nested pair", Acc);
		Acc = caar(Acc);
		skip(ISIZE0);
		break;
	case OP_CADR:
		if (!pairp(Acc) || !pairp(cdr(Acc)))
			expect("cadr", "nested pair", Acc);
		Acc = cadr(Acc);
		skip(ISIZE0);
		break;
	case OP_CDAR:
		if (!pairp(Acc) || !pairp(car(Acc)))
			expect("cdar", "nested pair", Acc);
		Acc = cdar(Acc);
		skip(ISIZE0);
		break;
	case OP_CDDR:
		if (!pairp(Acc) || !pairp(cdr(Acc)))
			expect("cddr", "nested pair", Acc);
		Acc = cddr(Acc);
		skip(ISIZE0);
		break;
	case OP_CHAR:
		if (!fixp(Acc)) expect("char", "fixnum", Acc);
		if (fixval(Acc) < 0 || fixval(Acc) > 255)
			error("char: value out of range", Acc);
		Acc = mkchar(fixval(Acc));
		skip(ISIZE0);
		break;
	case OP_CHARP:
		Acc = charp(Acc)? TRUE: NIL;
		skip(ISIZE0);
		break;
	case OP_CHARVAL:
		if (!charp(Acc)) expect("charval", "char", Acc);
		Acc = mkfix(charval(Acc));
		skip(ISIZE0);
		break;
	case OP_CLOSE_PORT:
		if (!inportp(Acc) && !outportp(Acc))
			expect("close-port", "port", Acc);
		close_port(portno(Acc));
		Acc = NIL;
		skip(ISIZE0);
		break;
	case OP_CONSTP:
		Acc = constp(Acc)? TRUE: NIL;
		skip(ISIZE0);
		break;
	case OP_CTAGP:
		Acc = ctagp(Acc)? TRUE: NIL;
		skip(ISIZE0);
		break;
	case OP_DELETE:
		if (!stringp(Acc)) expect("delete", "string", Acc);
		if (remove((char *) string(Acc)) < 0)
			error("delete: cannot delete", Acc);
		Acc = NIL;
		skip(ISIZE0);
		break;
	case OP_DOWNCASE:
		if (!charp(Acc)) expect("downcase", "char", Acc);
		Acc = mkchar(tolower(charval(Acc)));
		skip(ISIZE0);
		break;
	case OP_DUMP_IMAGE:
		if (!stringp(Acc)) expect("dump-image", "string", Acc);
		dump_image(Acc);
		Acc = TRUE;
		skip(ISIZE0);
		break;
	case OP_EOFP:
		Acc = (EOFMARK == Acc? TRUE: NIL);
		skip(ISIZE0);
		break;
	case OP_EVAL:
		Acc = eval(Acc, 1);
		skip(ISIZE0);
		break;
	case OP_EXISTSP:
		if (!stringp(Acc)) expect("existsp", "string", Acc);
		Acc = existsp((char *) string(Acc));
		skip(ISIZE0);
		break;
	case OP_FIXP:
		Acc = fixp(Acc)? TRUE: NIL;
		skip(ISIZE0);
		break;
	case OP_FLUSH:
		if (!outportp(Acc)) expect("flush", "outport", Acc);
		fflush(Ports[portno(Acc)]);
		skip(ISIZE0);
		break;
	case OP_FORMAT:
		Acc = format(Acc);
		skip(ISIZE0);
		break;
	case OP_FUNP:
		Acc = closurep(Acc)? TRUE: NIL;
		skip(ISIZE0);
		break;
	case OP_INPORTP:
		Acc = inportp(Acc)? TRUE: NIL;
		skip(ISIZE0);
		break;
	case OP_LISTSTR:
		if (!listp(Acc)) expect("liststr", "list", Acc);
		Acc = liststr(Acc);
		skip(ISIZE0);
		break;
	case OP_LISTVEC:
		if (!listp(Acc)) expect("listvec", "list", Acc);
		Acc = listvec(Acc, 0);
		skip(ISIZE0);
		break;
	case OP_LOAD:
		load(Acc);
		Acc = TRUE;
		skip(ISIZE0);
		break;
	case OP_LOWERC:
		if (!charp(Acc)) expect("lowerc", "char", Acc);
		Acc = islower(charval(Acc))? TRUE: NIL;
		skip(ISIZE0);
		break;
	case OP_MX:
		Acc = expand(Acc, 1);
		skip(ISIZE0);
		break;
	case OP_MX1:
		Acc = expand(Acc, 0);
		skip(ISIZE0);
		break;
	case OP_NEGATE:
		if (!fixp(Acc)) expect("-", "fixnum", Acc);
		if (INT_MIN == fixval(Acc))
			error("-: fixnum overflow", Acc);
		Acc = mkfix(-fixval(Acc));
		skip(ISIZE0);
		break;
	case OP_NULL:
		Acc = (NIL == Acc? TRUE: NIL);
		skip(ISIZE0);
		break;
	case OP_NUMSTR:
		if (!fixp(Acc)) expect("numstr", "fixnum", Acc);
		if (!fixp(arg(0))) expect("numstr", "fixnum", arg(0));
		Acc = numstr(Acc, fixval(arg(0)));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_NUMERIC:
		if (!charp(Acc)) expect("numeric", "char", Acc);
		Acc = isdigit(charval(Acc))? TRUE: NIL;
		skip(ISIZE0);
		break;
	case OP_OPEN_INFILE:
		if (!stringp(Acc)) expect("open-infile", "string", Acc);
		Acc = openfile(Acc, 0);
		skip(ISIZE0);
		break;
	case OP_OPEN_OUTFILE:
		if (!stringp(Acc)) expect("open-outfile", "string", Acc);
		Acc = openfile(Acc, NIL == arg(0)? 1: 2);
		clear(1);
		skip(ISIZE0);
		break;
	case OP_OUTPORTP:
		Acc = outportp(Acc)? TRUE: NIL;
		skip(ISIZE0);
		break;
	case OP_PAIR:
		Acc = pairp(Acc)? TRUE: NIL;
		skip(ISIZE0);
		break;
	case OP_PEEKC:
		if (!inportp(Acc)) expect("peekc", "inport", Acc);
		Acc = b_readc(portno(Acc), 1);
		skip(ISIZE0);
		break;
	case OP_READ:
		if (!inportp(Acc) && !stringp(Acc))
			expect("read", "inport", Acc);
		Acc = b_read(Acc);
		skip(ISIZE0);
		break;
	case OP_READC:
		if (!inportp(Acc)) expect("readc", "inport", Acc);
		Acc = b_readc(portno(Acc), 0);
		skip(ISIZE0);
		break;
	case OP_CONC:
		Acc = lconc(Acc);
		skip(ISIZE0);
		break;
	case OP_NCONC:
		Acc = nlconc(Acc);
		skip(ISIZE0);
		break;
	case OP_SCONC:
		Acc = sconc(Acc);
		skip(ISIZE0);
		break;
	case OP_SET_INPORT:
		if (!inportp(Acc)) expect("set-inport", "inport", Acc);
		Inport = portno(Acc);
		skip(ISIZE0);
		break;
	case OP_SET_OUTPORT:
		if (!outportp(Acc)) expect("set-outport", "outport", Acc);
		Outport = portno(Acc);
		skip(ISIZE0);
		break;
	case OP_SSIZE:
		if (!stringp(Acc)) expect("ssize", "string", Acc);
		Acc = mkfix(stringlen(Acc)-1);
		skip(ISIZE0);
		break;
	case OP_STRNUM:
		if (!stringp(Acc)) expect("strnum", "string", Acc);
		if (!fixp(arg(0))) expect("strnum", "fixnum", arg(0));
		Acc = strnum((char *) string(Acc), fixval(arg(0)));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_SYMBOLP:
		Acc = symbolp(Acc)? TRUE: NIL;
		skip(ISIZE0);
		break;
	case OP_SYMBOL:
		if (!stringp(Acc)) expect("symbol", "string", Acc);
		Acc = b_symbol(Acc);
		skip(ISIZE0);
		break;
	case OP_SYMNAME:
		if (!symbolp(Acc)) expect("symname", "symbol", Acc);
		Acc = b_symname(Acc);
		skip(ISIZE0);
		break;
	case OP_STRINGP:
		Acc = stringp(Acc)? TRUE: NIL;
		skip(ISIZE0);
		break;
	case OP_STRLIST:
		if (!stringp(Acc)) expect("strlist", "string", Acc);
		Acc = strlist(Acc);
		skip(ISIZE0);
		break;
	case OP_SYSCMD:
		if (!stringp(Acc)) expect("syscmd", "string", Acc);
		Acc = mkfix(system((char *) string(Acc)) >> 8);
		skip(ISIZE0);
		break;
	case OP_UNTAG:
		Acc = untag(Acc);
		skip(ISIZE0);
		break;
	case OP_UPCASE:
		if (!charp(Acc)) expect("upcase", "char", Acc);
		Acc = mkchar(toupper(charval(Acc)));
		skip(ISIZE0);
		break;
	case OP_UPPERC:
		if (!charp(Acc)) expect("upperc", "char", Acc);
		Acc = isupper(charval(Acc))? TRUE: NIL;
		skip(ISIZE0);
		break;
	case OP_VCONC:
		Acc = vconc(Acc);
		skip(ISIZE0);
		break;
	case OP_VECLIST:
		if (!vectorp(Acc)) expect("veclist", "vector", Acc);
		Acc = veclist(Acc);
		skip(ISIZE0);
		break;
	case OP_VECTORP:
		Acc = vectorp(Acc)? TRUE: NIL;
		skip(ISIZE0);
		break;
	case OP_VSIZE:
		if (!vectorp(Acc)) expect("vsize", "vector", Acc);
		Acc = mkfix(veclen(Acc));
		skip(ISIZE0);
		break;
	case OP_WHITEC:
		if (!charp(Acc)) expect("whitec", "char", Acc);
		Acc = whitespc(charval(Acc))? TRUE: NIL;
		skip(ISIZE0);
		break;
	case OP_BITOP:
		Acc = bitop(Acc, arg(0), arg(1));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_CLESS:
		cless(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_CLTEQ:
		clteq(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_CEQUAL:
		cequal(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_CGRTR:
		cgrtr(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_CGTEQ:
		cgteq(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_CONS:
		Acc = cons(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_DIV:
		Acc = intdiv(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_EQ:
		Acc = (Acc == arg(0))? TRUE: NIL;
		clear(1);
		skip(ISIZE0);
		break;
	case OP_EQUAL:
		equal(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_GRTR:
		grtr(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_GTEQ:
		gteq(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_LESS:
		less(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_LTEQ:
		lteq(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_MAX:
		if (fixval(arg(0)) > fixval(Acc)) Acc = arg(0);
		clear(1);
		skip(ISIZE0);
		break;
	case OP_MIN:
		if (fixval(arg(0)) < fixval(Acc)) Acc = arg(0);
		clear(1);
		skip(ISIZE0);
		break;
	case OP_MINUS:
		Acc = xsub(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_MKSTR:
		Acc = b_mkstr(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_MKVEC:
		Acc = b_mkvec(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_NRECONC:
		if (!listp(Acc)) expect("nreconc", "list", Acc);
		if (constp(Acc)) error("nreconc: immutable", Acc);
		Acc = nreconc(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_PLUS:
		Acc = add(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_PRIN:
		if (!outportp(arg(0))) expect("prin", "outport", arg(0));
		b_prin(Acc, portno(arg(0)), 1);
		clear(1);
		skip(ISIZE0);
		break;
	case OP_PRINC:
		if (!outportp(arg(0))) expect("princ", "outport", arg(0));
		b_prin(Acc, portno(arg(0)), 0);
		clear(1);
		skip(ISIZE0);
		break;
	case OP_RECONC:
		if (!listp(Acc)) expect("reconc", "list", Acc);
		Acc = reconc(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_REM:
		Acc = intrem(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_RENAME:
		b_rename(Acc, arg(0));
		Acc = NIL;
		clear(1);
		skip(ISIZE0);
		break;
	case OP_SETCAR:
		if (!pairp(Acc)) expect("setcar", "pair", Acc);
		if (constp(Acc)) error("setcar: immutable", Acc);
		car(Acc) = arg(0);
		clear(1);
		skip(ISIZE0);
		break;
	case OP_SETCDR:
		if (!pairp(Acc)) expect("setcdr", "pair", Acc);
		if (constp(Acc)) error("setcdr: immutable", Acc);
		cdr(Acc) = arg(0);
		clear(1);
		skip(ISIZE0);
		break;
	case OP_SLESS:
		Acc = sless(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_SLTEQ:
		Acc = slteq(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_SEQUAL:
		Acc = sequal(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_SGRTR:
		Acc = sgrtr(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_SGTEQ:
		Acc = sgteq(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_SILESS:
		Acc = siless(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_SILTEQ:
		Acc = silteq(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_SIEQUAL:
		Acc = siequal(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_SIGRTR:
		Acc = sigrtr(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_SIGTEQ:
		Acc = sigteq(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_SFILL:
		sfill(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_SREF:
		Acc = sref(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_SSET:
		sset(Acc, arg(0), arg(1));
		clear(2);
		skip(ISIZE0);
		break;
	case OP_SUBSTR:
		Acc = substr(Acc, arg(0), arg(1));
		clear(2);
		skip(ISIZE0);
		break;
	case OP_SUBVEC:
		Acc = subvec(Acc, arg(0), arg(1));
		clear(2);
		skip(ISIZE0);
		break;
	case OP_TIMES:
		Acc = mul(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_VFILL:
		vfill(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_VREF:
		Acc = vref(Acc, arg(0));
		clear(1);
		skip(ISIZE0);
		break;
	case OP_VSET:
		vset(Acc, arg(0), arg(1));
		clear(2);
		skip(ISIZE0);
		break;
	case OP_WRITEC:
		if (!charp(Acc)) expect("writec", "char", Acc);
		if (!outportp(arg(0))) expect("writec", "outport", arg(0));
		b_writec(charval(Acc), portno(arg(0)));
		clear(1);
		skip(ISIZE0);
		break;
	default:
		error("illegal instruction", mkfix(ins()));
		return;
	} }
	error("interrupted", UNDEF);
}

cell interpret(cell x) {
	cell	n;
	int	i;

	E0 = mkvec(length(Glob));
	i = 0;
	for (n = Glob; n != NIL; n = cdr(n)) {
		vector(E0)[i] = cdar(n);
		i++;
	}
	Ep = E0;
	run(x);
	return Acc;
}

void begin_rec(void) {
	protect(Prog);
	protect(Ep);
	protect(mkfix(Ip));
	protect(mkfix(Sp));
	protect(mkfix(Fp));
}

void end_rec(void) {
	Fp = fixval(unprot(1));
	Sp = fixval(unprot(1));
	Ip = fixval(unprot(1));
	Ep = unprot(1);
	Prog = unprot(1);
}

cell eval(cell x, int r) {
	Tmp = x;
	if (r) begin_rec();
	protect(x);
	Tmp = NIL;
	x = expand(x, 1);  car(Protected) = x;
	syncheck(x, 1);
	x = clsconv(x);    car(Protected) = x;
	x = compile(x);    car(Protected) = x;
	x = interpret(x);
	unprot(1);
	if (r) end_rec();
	return x;
}

/*
 * REPL
 */

volatile int	Intr = 0;

void kbdintr(int sig) {
	Run = 0;
	Intr = 1;
	Mxlev = -1;
}

int	Quiet = 0;

void initrts(void) {
	Rts = NIL;
	Rts = mkvec(CHUNKSIZE);
	Sz = CHUNKSIZE;
	Sp = -1;
	Fp = -1;
}

void repl(void) {
	cell	x;

	if (setjmp(Restart) && Quiet)
		exit(EXIT_FAILURE);
	if (!Quiet) signal(SIGINT, kbdintr);
	for (;;) {
		reset_stdports();
		clrtrace();
		initrts();
		bindset(S_errtag, NIL);
		Protected = NIL;
		Run = 0;
		Intr = 0;
		if (!Quiet) {
			prints("* ");
			flush();
		}
		x = xread();
		if (EOFMARK == x && !Intr) break;
		Mxlev = 0;
		x = eval(x, 0);
		bindset(S_starstar, x);
		print(x);
	}
	if (!Quiet) nl();
}

/*
 * Startup and initialization
 */

void init(void) {
	int	i;

	for (i=2; i<NPORTS; i++) Ports[i] = NULL;
	Ports[0] = stdin;  Port_flags[0] = LOCK_TAG;
	Ports[1] = stdout; Port_flags[1] = LOCK_TAG;
	Ports[2] = stderr; Port_flags[2] = LOCK_TAG;
	alloc_nodepool();
	alloc_vecpool();
	gcv();
	initrts();
	clrtrace();
	Nullvec = newvec(T_VECTOR, 0);
	Nullstr = newvec(T_STRING, 1);
	Blank = mkchar(' ');
	Zero = mkfix(0);
	One = mkfix(1);
	Ten = mkfix(10);
	Symbols = mkvec(CHUNKSIZE);
	Symhash = mkht(CHUNKSIZE);
	Obhash = mkht(CHUNKSIZE);
	Obarray = mkvec(CHUNKSIZE);
	Obmap = mkstr("", CHUNKSIZE);
	memset(string(Obmap), OBFREE, CHUNKSIZE);
	symref("?");
	I_a = symref("a");
	I_e = symref("e");
	I_arg = symref("%arg");
	I_closure = symref("%closure");
	I_ref = symref("%ref");
	S_apply = symref("apply");
	S_def = symref("def");
	S_defmac = symref("defmac");
	S_defun = symref("defun");
	S_errtag = symref("*errtag*");
	S_errval = symref("*errval*");
	S_if = symref("if");
	S_ifstar = symref("if*");
	S_imagefile = symref("*imagefile*");
	S_labels = symref("labels");
	S_lambda = symref("lambda");
	S_macro = symref("macro");
	S_prog = symref("prog");
	S_quiet = symref("*quiet*");
	S_quote = symref("quote");
	S_qquote = symref("qquote");
	S_unquote = symref("unquote");
	S_splice = symref("splice");
	S_starstar = symref("**");
	S_setq = symref("setq");
	S_start = symref("start");
	P_abs = symref("abs");
	P_alphac = symref("alphac");
	P_atom = symref("atom");
	P_bitop = symref("bitop");
	P_caar = symref("caar");
	P_cadr = symref("cadr");
	P_car = symref("car");
	P_catchstar = symref("catch*");
	P_cdar = symref("cdar");
	P_cddr = symref("cddr");
	P_cdr = symref("cdr");
	P_cequal = symref("c=");
	P_cgrtr = symref("c>");
	P_cgteq = symref("c>=");
	P_char = symref("char");
	P_charp = symref("charp");
	P_charval = symref("charval");
	P_cless = symref("c<");
	P_close_port = symref("close-port");
	P_clteq = symref("c<=");
	P_cmdline = symref("cmdline");
	P_conc = symref("conc");
	P_cons = symref("cons");
	P_constp = symref("constp");
	P_ctagp = symref("ctagp");
	P_delete = symref("delete");
	P_div = symref("div");
	P_downcase = symref("downcase");
	P_dump_image = symref("dump-image");
	P_eofp = symref("eofp");
	P_eq = symref("eq");
	P_equal = symref("=");
	P_error = symref("error");
	P_errport = symref("errport");
	P_eval = symref("eval");
	P_existsp = symref("existsp");
	P_fixp = symref("fixp");
	P_flush = symref("flush");
	P_format = symref("format");
	P_funp = symref("funp");
	P_gc = symref("gc");
	P_gensym = symref("gensym");
	P_grtr = symref(">");
	P_gteq = symref(">=");
	P_inport = symref("inport");
	P_inportp = symref("inportp");
	P_less = symref("<");
	P_liststr = symref("liststr");
	P_listvec = symref("listvec");
	P_load = symref("load");
	P_lowerc = symref("lowerc");
	P_lteq = symref("<=");
	P_max = symref("max");
	P_min = symref("min");
	P_minus = symref("-");
	P_mkstr = symref("mkstr");
	P_mkvec = symref("mkvec");
	P_mx = symref("mx");
	P_mx1 = symref("mx1");
	P_not = symref("not");
	P_nconc = symref("nconc");
	P_nreconc = symref("nreconc");
	P_null = symref("null");
	P_numeric = symref("numeric");
	P_numstr = symref("numstr");
	P_obtab = symref("obtab");
	P_open_infile = symref("open-infile");
	P_open_outfile = symref("open-outfile");
	P_outport = symref("outport");
	P_outportp = symref("outportp");
	P_pair = symref("pair");
	P_peekc = symref("peekc");
	P_plus = symref("+");
	P_prin = symref("prin");
	P_princ = symref("princ");
	P_quit = symref("quit");
	P_read = symref("read");
	P_readc = symref("readc");
	P_reconc = symref("reconc");
	P_rem = symref("rem");
	P_rename = symref("rename");
	P_sconc = symref("sconc");
	P_sequal = symref("s=");
	P_set_inport = symref("set-inport");
	P_set_outport = symref("set-outport");
	P_setcar = symref("setcar");
	P_setcdr = symref("setcdr");
	P_sfill = symref("sfill");
	P_sgrtr = symref("s>");
	P_sgteq = symref("s>=");
	P_siequal = symref("si=");
	P_sigrtr = symref("si>");
	P_sigteq = symref("si>=");
	P_siless = symref("si<");
	P_silteq = symref("si<=");
	P_sless = symref("s<");
	P_slteq = symref("s<=");
	P_sref = symref("sref");
	P_sset = symref("sset");
	P_ssize = symref("ssize");
	P_stringp = symref("stringp");
	P_strlist = symref("strlist");
	P_strnum = symref("strnum");
	P_substr = symref("substr");
	P_subvec = symref("subvec");
	P_symbol = symref("symbol");
	P_symbolp = symref("symbolp");
	P_symname = symref("symname");
	P_symtab = symref("symtab");
	P_syscmd = symref("syscmd");
	P_throwstar = symref("throw*");
	P_times = symref("*");
	P_untag = symref("untag");
	P_upcase = symref("upcase");
	P_upperc = symref("upperc");
	P_vconc = symref("vconc");
	P_veclist = symref("veclist");
	P_vectorp = symref("vectorp");
	P_vfill = symref("vfill");
	P_vref = symref("vref");
	P_vset = symref("vset");
	P_vsize = symref("vsize");
	P_whitec = symref("whitec");
	P_writec = symref("writec");
	bindnew(S_errtag, NIL);
	bindnew(S_errval, NIL);
	bindnew(S_imagefile, NIL);
	bindnew(S_quiet, NIL);
	bindnew(S_starstar, NIL);
	bindnew(S_start, NIL);
}

void start(void) {
	cell	n;

	if (setjmp(Restart)) return;
	if (!Quiet) signal(SIGINT, kbdintr);
	n = assq(S_start, Glob);
	if (NIL == n || closurep(cadr(n)) == 0) return;
	n = cons(cadr(n), NIL);
	eval(n, 0);
}

cell	*Imagevars[] = {
		&Freelist, &Freevec, &Symbols, &Symhash, &Symptr,
		&Rts, &Glob, &Macros, &Obhash, &Obarray, &Obmap, NULL };

cell	*GC_roots[] = {
		&Protected, &Symbols, &Symhash, &Prog, &Env, &Obhash,
		&Obarray, &Obmap, &Cts, &Emitbuf, &Glob, &Macros, &Rts,
		&Acc, &E0, &Ep, &Argv, &Tmp, &Tmp_car, &Tmp_cdr, &Files,
		&Outstr, &Nullvec, &Nullstr, &Blank, &Zero, &One, &Ten,
		NULL };

/*
 * Command line interface
 */

void usage(void) {
	prints("Usage: ls9 [-Lhqv?] [-i file | -] [-l file]\n");
	prints("           [-- argument ... | file argument ...]\n");
}

void longusage(void) {
	nl();
	usage();
	prints(	"\n"
		"-h         print help (also -v, -?)\n"
		"-L         print terms of use\n"
		"-i file    restart image from file (default: ");
	prints(	IMAGEFILE);
	prints(	")\n"
		"-i -       compile initial image from sources (");
	prints(	IMAGESRC);
	prints(	")\n"
		"           (-i must be the first option!)\n");
	prints(	"-l file    load program from file, can be repeated\n"
		"-q         quiet (no banner, no prompt, exit on errors)\n"
		"-- args    bind remaining arguments to (cmdline)\n"
		"file args  run program, args in (cmdline), implies -q\n"
		"\n");
	exit(EXIT_SUCCESS);
}

void terms(void) {
	nl();
	prints(	"LISP9 ");
	prints(VERSION);
	prints(	" by Nils M Holm, 2018,2019\n\n"
		"This program is in the public domain. In countries\n"
		"where the concept of the public domain does not exist,\n"
		"the Creative Commons Zero (CC0) license applies.\n"
		"See: https://creativecommons.org/publicdomain/zero/1.0/");
	nl();
	nl();
	exit(EXIT_SUCCESS);
}

char *cmdarg(char *s) {
	if (NULL == s) {
		usage();
		exit(EXIT_FAILURE);
	}
	return s;
}

cell	Argv = NIL;

cell argvec(char **argv) {
	int	i;
	cell	a, n;

	if (NULL == argv[0]) return NIL;
	a = cons(NIL, NIL);
	protect(a);
	for (i = 0; argv[i] != NULL; i++) {
		n = mkstr(argv[i], strlen(argv[i]));
		car(a) = n;
		if (argv[i+1] != NULL) {
			n = cons(NIL, NIL);
			cdr(a) = n;
			a = cdr(a);
		}
	}
	return unprot(1);
}

int main(int argc, char **argv) {
	int	i, j, k, usrimg, doload;
	char	*s;
	char	*imgfile;

	imgfile = IMAGEFILE;
	usrimg = 0;
	doload = 1;
	if (setjmp(Restart) != 0) exit(EXIT_FAILURE);
	init();
	i = 1;
	if (argc > 2 && strcmp(argv[1], "-i") == 0) {
		imgfile = argv[2];
		i = 3;
		usrimg = 1;
	}
	if (existsp(imgfile) != NIL) {
		s = loadimg(imgfile);
		if (s != NULL) fatal(s);
		bindset(S_imagefile,
			mkstr(imgfile, strlen(imgfile)));
	}
	else if (usrimg && strcmp(imgfile, "-") != 0) {
		fatal("cannot open image file");
	}
	else {
		if (setjmp(Restart) != 0)
			fatal("could not load library");
		loadfile(IMAGESRC);
	}
	if (setjmp(Restart) != 0) exit(EXIT_FAILURE);
	for (; i<argc; i++) {
		if (argv[i][0] != '-') break;
		if ('-' == argv[i][1]) {
			doload = 0;
			break;
		}
		k = strlen(argv[i]);
		for (j=1; j<k; j++) {
			switch (argv[i][j]) {
			case '?':
			case 'h':
			case 'v':
				longusage();
				break;
			case 'L':
				terms();
				break;
			case 'l':
				i++;
				loadfile(cmdarg(argv[i]));
				j = strlen(argv[i]);
				break;
			case 'q':
				Quiet = 1;
				break;
			default:
				usage();
				exit(EXIT_FAILURE);
			}
		}
	}
	bindset(S_quiet, Quiet? TRUE: NIL);
	if (!Quiet && NULL == argv[i]) {
		prints("LISP9 "); prints(VERSION); nl();
	}
	Argv = NULL == argv[i]? NIL: argvec(&argv[i+1]);
	start();
	if (setjmp(Restart) != 0) exit(EXIT_FAILURE);
	if (doload && argv[i] != NULL) {
		loadfile(argv[i]);
		exit(EXIT_SUCCESS);
	}
	repl();
	return 0;
}
