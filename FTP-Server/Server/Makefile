FLAGS= -c -Wall -ansi -pedantic -errors -std=gnu99

all: BibakBOXServer 

BibakBOXServer: BibakBOXServer.o
	gcc BibakBOXServer.o -o BibakBOXServer -pthread

BibakBOXServer.o: BibakBOXServer.c
	gcc ${FLAGS} BibakBOXServer.c

clean:
	rm -rf *.o BibakBOXServer
