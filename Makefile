A=lisp9.tgz
R=lisp9-20200220.tgz
CFLAGS=	-g -O2

all:	ls9 ls9.image # prolog # lisp9.ps

ls9:	ls9.c
	$(CC) $(CFLAGS) -o ls9 ls9.c

ls9.image:	ls9 ls9.ls9
	rm -f ls9.image
	echo "(dump-image \"ls9.image\")" | ./ls9 -q

lisp9.tr:	lisp9.txt
	./ls9 src/print.ls9 -T -C -p 60 -l 6 -m -4 -t "LISP9 REFERENCE MANUAL" \
		lisp9.txt >lisp9.tr

lisp9.ps:	lisp9.tr
	groff -Tps -P-p11i,8.5i lisp9.tr >lisp9.ps

test:	ls9 ls9.image
	./ls9 test.ls9

ptest:	ls9 prolog
	./ls9 -i prolog -- -q <src/test.pl9 > src/test.out
	diff -u src/test.OK src/test.out && rm src/test.out

zebra:	ls9 prolog src/zebra.pl9
	echo "prlist([])." \
	     "prlist([H|T]) :- write(H), nl, prlist(T)." \
	     ":- nl, zebra(H), !, prlist(H), fail." \
	     | ./ls9 -i prolog -- -q -c src/zebra.pl9

prolog:	ls9 src/prolog.ls9 src/prolog.pl9
	echo "(defun (start) (prolog) (quit)) (dump-image \"prolog\")" \
		| ./ls9 -ql src/prolog.ls9

arc:	clean
	tar cf - * | gzip -c9 >$A

dist:   clean
	cd ..; tar -cvf - `cat lisp9/_nodist` lisp9 | gzip -9c >$R
	mv ../$R .

csums:
	csum -u <_csums >_csums.new
	mv _csums.new _csums

mksums:	clean
	find . -type f | grep -v _csums |grep -v $A | csum >_csums

clean:
	rm -f ls9 ls9.image *.oimage prolog lisp9.ps lisp9.tr lisp9.ps \
		$A $R a.out *.core
