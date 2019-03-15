CC = gcc
OBJS = commandExecuter.o mainShell.o
EXEC = miniShell
CFLAGS = -O3 -Wall -std=gnu99

$(EXEC): $(OBJS)
	$(CC) $(OBJS) -o $@ -lm

all: miniShell
commandExecuter.o: commandExecuter.c
	$(CC) $(CFLAGS) -c $*.c
mainShell.o: mainShell.c commandExecuter.c
	$(CC) $(CFLAGS) -c $*.c

clean:
	rm -f $(OBJS) $(EXEC)