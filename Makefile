CC         ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS     ?= -O2 -g -Wall -Wextra
CPPFLAGS   += -Iinclude
LDFLAGS    ?=
LDLIBS     ?=
AR         ?= ar

# Driver core shared by the CLI and the GUI.
LIB_SRC := src/hidraw.c src/mouse.c src/keyboard.c src/util.c src/udev.c
LIB_OBJ := $(LIB_SRC:.c=.o)
LIB     := libattackshark.a

BIN_SRC := src/main.c
BIN     := attackshark

# GUI (optional: built only when GTK4 + libadwaita are available).
GUI_PKGS := gtk4 libadwaita-1
GUI_SRC  := $(wildcard src/gui/*.c)
GUI_OBJ  := $(GUI_SRC:.c=.o)
GUI      := attackshark-gui
HAVE_GTK := $(shell $(PKG_CONFIG) --exists $(GUI_PKGS) 2>/dev/null && echo yes)
ifeq ($(HAVE_GTK),yes)
GUI_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(GUI_PKGS))
GUI_LIBS   := $(shell $(PKG_CONFIG) --libs $(GUI_PKGS))
else
GUI_CFLAGS :=
GUI_LIBS   :=
endif

PREFIX     ?= /usr/local
BINDIR     ?= $(PREFIX)/bin
UDEVDIR    ?= /etc/udev/rules.d
SYSTEMDDIR ?= /etc/systemd/system
DESKTOPDIR ?= $(PREFIX)/share/applications
ICONDIR    ?= $(PREFIX)/share/icons/hicolor/scalable/apps
POLKITDIR  ?= /usr/share/polkit-1/actions

.PHONY: all gui clean install install-gui uninstall check test test-store compiledb

STORE_TEST_SRC := tests/test_gui_store.c src/gui/as-store.c src/gui/as-worker.c

all: $(BIN) $(if $(HAVE_GTK),$(GUI))

$(LIB): $(LIB_OBJ)
	$(AR) rcs $@ $^

$(BIN): src/main.o $(LIB)
	$(CC) $(LDFLAGS) -o $@ src/main.o $(LIB) $(LDLIBS) -lm

gui: $(GUI)

$(GUI): $(GUI_OBJ) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $(GUI_OBJ) $(LIB) $(GUI_LIBS) $(LDLIBS) -lm

src/gui/%.o: src/gui/%.c include/attackshark.h
	$(CC) $(CPPFLAGS) $(GUI_CFLAGS) $(CFLAGS) -c -o $@ $<

%.o: %.c include/attackshark.h src/internal.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	install -Dm644 udev/70-attackshark.rules $(DESTDIR)$(UDEVDIR)/70-attackshark.rules
	install -Dm644 systemd/attackshark.service $(DESTDIR)$(SYSTEMDDIR)/attackshark.service

install-gui: $(GUI)
	install -Dm755 $(GUI) $(DESTDIR)$(BINDIR)/$(GUI)
	install -Dm644 data/attackshark-gui.desktop \
		$(DESTDIR)$(DESKTOPDIR)/attackshark-gui.desktop
	install -Dm644 data/attackshark-gui.svg \
		$(DESTDIR)$(ICONDIR)/attackshark-gui.svg
	install -d $(DESTDIR)$(POLKITDIR)
	sed 's|@BINDIR@|$(BINDIR)|g' data/org.attackshark.permissions.policy.in \
		> $(DESTDIR)$(POLKITDIR)/org.attackshark.permissions.policy
	chmod 644 $(DESTDIR)$(POLKITDIR)/org.attackshark.permissions.policy

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)
	rm -f $(DESTDIR)$(UDEVDIR)/70-attackshark.rules
	rm -f $(DESTDIR)$(SYSTEMDDIR)/attackshark.service
	rm -f $(DESTDIR)$(BINDIR)/$(GUI)
	rm -f $(DESTDIR)$(DESKTOPDIR)/attackshark-gui.desktop
	rm -f $(DESTDIR)$(ICONDIR)/attackshark-gui.svg
	rm -f $(DESTDIR)$(POLKITDIR)/org.attackshark.permissions.policy

tests/test_codec: tests/test_codec.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_codec.c $(LIB) -lm

tests/test_detect: tests/test_detect.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_detect.c $(LIB) -lm

test: tests/test_codec tests/test_detect
	./tests/test_codec
	./tests/test_detect

tests/test_gui_store: $(STORE_TEST_SRC) $(LIB)
	$(CC) $(CPPFLAGS) -Isrc/gui $(GUI_CFLAGS) $(CFLAGS) -o $@ $(STORE_TEST_SRC) \
		$(LIB) $(GUI_LIBS) -lm

# Needs a connected mouse or keyboard: exercises enumeration and a write.
test-store: tests/test_gui_store
	./tests/test_gui_store

check: test $(BIN)
	./$(BIN) list

# Editor support: compile_commands.json for clangd/ccls.
compiledb:
	@printf '[\n' > compile_commands.json
	@set -e; first=1; \
	for f in $(LIB_SRC) $(BIN_SRC) $(GUI_SRC) $(STORE_TEST_SRC) tests/test_detect.c; do \
		case $$f in src/gui/*) extra="$(GUI_CFLAGS)";; \
			tests/*) extra="-Isrc/gui $(GUI_CFLAGS)";; *) extra="";; esac; \
		[ $$first = 1 ] || printf ',\n' >> compile_commands.json; \
		first=0; \
		printf '  {"directory": "%s", "file": "%s", "command": "%s $(CPPFLAGS) %s $(CFLAGS) -c -o /dev/null %s"}' \
			"$(CURDIR)" "$$f" "$(CC)" "$$extra" "$$f" >> compile_commands.json; \
	done; \
	printf '\n]\n' >> compile_commands.json

clean:
	rm -f $(LIB_OBJ) src/main.o src/gui/*.o tests/*.o $(LIB) $(BIN) $(GUI) \
		tests/test_codec tests/test_detect tests/test_gui_store
