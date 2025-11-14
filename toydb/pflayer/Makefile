#PUBLICDIR= /usr0/cs564/public/project
SRC= buf.c hash.c pf.c splayer.c
OBJ= buf.o hash.o pf.o splayer.o
HDR = pftypes.h pf.h 

CFLAGS= -Wall -std=c99 -pedantic

pflayer.o: $(OBJ)
	ld -r -o pflayer.o $(OBJ)

tests: testhash testpf testpf_policy

testpf: testpf.o pflayer.o
	cc -o testpf testpf.o pflayer.o

testhash: testhash.o pflayer.o
	cc -o testhash testhash.o pflayer.o

testpf_policy: testpf_policy.o pflayer.o
	cc -o testpf_policy testpf_policy.o pflayer.o

testsp: testsp.o pflayer.o
	cc -o testsp testsp.o pflayer.o

$(OBJ): $(HDR)

testhash.o: $(HDR)

testpf.o: $(HDR)

lint: 
	lint $(SRC)

install: pflayer.o 


.PHONY: clean tests lint install

clean:
	rm -f *.o pflayer.o testpf testhash
