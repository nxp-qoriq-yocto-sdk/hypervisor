all: tunctl

bin:
	mkdir bin

tunctl: bin tunctl.c
	gcc -O -o bin/tunctl tunctl.c

clean:
	rm -rf bin
