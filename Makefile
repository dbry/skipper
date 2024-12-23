# Skipper Makefile

CC := gcc

utils := skipper tensor-gen bin2c

all: $(utils)

skipper: skipper.c biquad.c lzwlib.c skipper.h biquad.h lzwlib.h 4d-tensor.h
	$(CC) skipper.c biquad.c lzwlib.c -O3 -lm -o skipper

tensor-gen: tensor-gen.c lzwlib.c skipper.h lzwlib.h
	$(CC) tensor-gen.c lzwlib.c -lm -o tensor-gen

bin2c: bin2c.c
	$(CC) bin2c.c lzwlib.c -lm -o bin2c

clean:
	rm -f skipper tensor-gen bin2c
