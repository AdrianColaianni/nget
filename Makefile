build:
	gcc -Wall -o nget main.c
run: build
	./nget 127.0.0.1 4444
debug:
	gcc -Wall -g -Og -o nget main.c
	gdb nget
clean:
	rm -rf nget
