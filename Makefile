all: receiver sender

receiver: receiver.o
	gcc -o receiver receiver.c

sender: sender.o
	gcc -o sender sender.c

clean:
	$(RM) sender
	$(RM) receiver
