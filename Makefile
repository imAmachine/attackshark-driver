CC      ?= cc
CFLAGS  ?= -O2 -g -Wall -Wextra
CPPFLAGS += -Iinclude
LDFLAGS ?=
LDLIBS  ?=

SRC := src/main.c src/hidraw.c src/mouse.c src/keyboard.c src/util.c
OBJ := $(SRC:.c=.o)
BIN := attackshark

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
UDEVDIR ?= /etc/udev/rules.d
SYSTEMDDIR ?= /etc/systemd/system

.PHONY: all clean install uninstall check

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS) -lm

%.o: %.c include/attackshark.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	install -Dm644 udev/99-attackshark.rules $(DESTDIR)$(UDEVDIR)/99-attackshark.rules
	install -Dm644 systemd/attackshark.service $(DESTDIR)$(SYSTEMDDIR)/attackshark.service

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)
	rm -f $(DESTDIR)$(UDEVDIR)/99-attackshark.rules
	rm -f $(DESTDIR)$(SYSTEMDDIR)/attackshark.service

tests/test_codec: tests/test_codec.c src/mouse.c src/keyboard.c src/hidraw.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ -lm

test: tests/test_codec
	./tests/test_codec

check: test $(BIN)
	./$(BIN) list

clean:
	rm -f $(OBJ) $(BIN) tests/test_codec
