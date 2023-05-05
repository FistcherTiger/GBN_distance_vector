CFLAGS = -Wall

all: gbnnode dvnode cnnode

gbnnode : gbnnode.o udp.o
	cc -Wall -o gbnnode gbnnode.o udp.o -lpthread

gbnnode.o : gbnnode.c gbnnode.h
	cc -Wall -c gbnnode.c

udp.o : udp.c udp.h
	cc -Wall -c udp.c

dvnode : dvnode.o vector.o
	cc -Wall -o dvnode dvnode.o vector.o -lpthread

dvnode.o : dvnode.c dvnode.h
	cc -Wall -c dvnode.c

cnnode : cnnode.o vector.o
	cc -Wall -o cnnode cnnode.o vector.o -lpthread

cnnode.o : cnnode.c cnnode.h
	cc -Wall -c cnnode.c

vector.o : vector.c vector.h
	cc -Wall -c vector.c

.PHONY : clean
clean :
	rm -rf *.o gbnnode dvnode cnnode

.PHONY : remove
remove :
	rm -rf *.o *.c *.h gbnnode dvnode cnnode
