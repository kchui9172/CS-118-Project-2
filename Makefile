CC=gcc
OBJ=sender.o

%.o: %.c$(DEPS)
	$(CC) -c -o $@ $<

sender: $(OBJ)
	$(CC) -o $@ $^
