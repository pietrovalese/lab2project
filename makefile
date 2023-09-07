# definizione del compilatore e dei flag di compilazione
# che vengono usate dalle regole implicite
CC=gcc
CFLAGS=-g -Wall -O -std=c11 -pthread
LDLIBS=-lm -lrt -pthread


# eseguibili da costruire
EXECS=archivio client1  #posso metterci gli altri eseguibili da creare
.PHONY: clean

# di default make cerca di realizzare il primo target 
all: $(EXECS)
	chmod u+x server.py
	chmod u+x client2.py

# non devo scrivere il comando associato ad ogni target 
# perché il default di make in questo caso va bene

archivio: archivio.o xerrori.o        #sono i requisiti per ogni eseguibile 
	$(CC) $(LDLIBS) -o $@ $^
client1: client1.o 
	$(CC) $(LDLIBS) -o $@ $^

archivio.o: archivio.c xerrori.h
	$(CC) $(CFLAGS) -c $< -o $@
xerrori.o: xerrori.c xerrori.h
	$(CC) $(CFLAGS) -c $< -o $@
client1.o: client1.c 
	$(CC) $(CFLAGS) -c $< -o $@

# target che cancella eseguibili e file oggetto
clean:
	rm -f $(EXECS) *.o *. *.log
	clear

kill:
	pkill -INT -f server.py
