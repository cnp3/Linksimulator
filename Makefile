# See gcc/clang manual to understand all flags
CFLAGS += -std=c99 # Define which version of the C standard to use
CFLAGS += -Wall # Enable the 'all' set of warnings
CFLAGS += -Werror # Treat all warnings as error
CFLAGS += -Wshadow # Warn when shadowing variables
CFLAGS += -Wextra # Enable additional warnings
CFLAGS += -O2 -D_FORTIFY_SOURCE=2 # Add canary code, i.e. detect buffer overflows
CFLAGS += -fstack-protector-all # Add canary code to detect stack smashing
CFLAGS += -D_XOPEN_SOURCE -D_POSIX_C_SOURCE=201112L # getopt, clock_getttime

SOURCES=$(wildcard *.c)
OBJECTS=$(SOURCES:.c=.o)

LDFLAGS= -rdynamic
ifneq ($(shell uname -s),Darwin) # Apple does not have clock_gettime
	LDFLAGS += -lrt              # hence does not need librealtime
endif

all: link_sim

debug: CFLAGS += -g -DDEBUG -Wno-unused-parameter -fno-omit-frame-pointer
debug: LDFLAGS += -lSegFault
debug: link_sim

link_sim: $(OBJECTS)

.PHONY: clean mrproper rebuild

clean:
	@rm -f $(OBJECTS)

mrproper:
	@rm -f link_sim

rebuild: clean mrproper link_sim
