all: dccserver

dccserver: dccserver.c
	cc -g -Wall -o dccserver dccserver.c

clean:
	-rm -f dccserver dccserver.core
