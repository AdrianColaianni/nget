build:
	gcc -o nget main.c
run: build
	./nget
clean:
	rm -rf nget
