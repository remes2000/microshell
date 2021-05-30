microshell: microshell.o
	gcc -ansi -Wall -o microshell microshell.o -lreadline

microshell.o: microshell.c
	gcc -ansi -Wall -c -o microshell.o microshell.c

clean:
	rm -f microshell.o microshell