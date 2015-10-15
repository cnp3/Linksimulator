OPTIMIZE=-O2
CFLAGS=-Wall -Werror -Wshadow -Wextra $(OPTIMIZE) -std=gnu99
LDFLAGS=
OBJECTS=$(patsubst %.c,%.o,$(wildcard *.c))

link_sim: $(OBJECTS)

.PHONY: clean

clean:
	@rm -f $(OBJECTS) link_sim

rebuild: clean link_sim
