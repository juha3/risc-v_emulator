all: rv32_emu

rv32_emu: main.o
	gcc -o rv32_emu main.o

main.o: main.c
	gcc -c main.c

clean:
	rm -rf *.o rv32_emu

