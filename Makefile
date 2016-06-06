adaptiva: adaptiva.c
	$(CC) -pedantic -Werror -Wall -O2 -std=c99 adaptiva.c -o adaptiva
clean:
	rm adaptiva
