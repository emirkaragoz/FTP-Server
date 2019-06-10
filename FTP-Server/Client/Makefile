FLAGS= -c -Wall -ansi -pedantic -errors -std=gnu99

all: BibakBOXClient

BibakBOXClient: BibakBOXClient.o
	gcc BibakBOXClient.o -o BibakBOXClient -pthread

BibakBOXClient.o: BibakBOXClient.c
	gcc ${FLAGS} BibakBOXClient.c

clean:
	rm -rf *.o BibakBOXClient
