# funlisp - Makefile
#
# Stephen Brennan <stephen@brennan.io>

.PHONY: all clean

# Debug mode (make D=1): adds "debugging symbols" etc
D=0
ifeq ($(D),1)
FLAGS += -g -DDEBUG
endif

CC=gcc
CFLAGS= -std=c89 -Wall -Wextra -fPIC -Iinc -c
SRCS=$(wildcard src/*.c)
OBJS=$(patsubst src/%.c,obj/%.o,$(SRCS))

all: bin/libfunlisp.a

obj/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

bin/libfunlisp.a: $(OBJS)
	ar rcs $@ $^

clean:
	rm -rf obj/* bin/*

depend: $(SRCS)
	$(CC) $(CFLAGS) -MM obj $(SRCS) | sed 's!^\(.*\):!obj/\1:!' > depend

include depend
