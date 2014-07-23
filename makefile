prefix ?= /usr/local

OUTPUT := pbuffer

CXXFLAGS ?= -Wall -Wextra -Wno-unused-parameter -O3 -g
LDFLAGS ?=
CPPFLAGS += -D_FILE_OFFSET_BITS=64
CXXFLAGS += -std=c++11

ifdef gprof
CXXFLAGS += -pg
OUTPUT := $(OUTPUT)_gprof
endif

ifdef san
CFLAGS += -g -fsanitize=$(san) -fno-omit-frame-pointer
OUTPUT := $(OUTPUT)_san_$(san)
endif

all: $(OUTPUT)

VERSION_STRING := $(shell cat version 2>/dev/null || git describe --tags --always --dirty=-m 2>/dev/null || date "+%F %T %z" 2>/dev/null)
ifdef VERSION_STRING
CVFLAGS := -DVERSION_STRING='"${VERSION_STRING}"'
endif

$(OUTPUT): pbuffer.cpp
	g++ pbuffer.cpp $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $(CVFLAGS) $(AFLAGS) -o $(OUTPUT) $(LOADLIBES) $(LDLIBS)

.PHONY: all install uninstall clean dumpversion

dumpversion:
	@echo $(VERSION_STRING)

clean:
	rm -f $(OUTPUT) pbuffer.1

install: pbuffer
	install -D -m 755 pbuffer $(DESTDIR)$(prefix)/bin/pbuffer

uninstall:
	rm -f $(DESTDIR)$(prefix)/bin/pbuffer $(DESTDIR)$(prefix)/share/man/man1/pbuffer.1

HELP2MANOK := $(shell help2man --version 2>/dev/null)
ifdef HELP2MANOK
all: pbuffer.1

pbuffer.1: pbuffer
	help2man -s 1 -N ./pbuffer -n "Pipe Buffer" -o pbuffer.1

install: install-man

.PHONY: install-man

install-man: pbuffer.1
	install -D -m 644 pbuffer.1 $(DESTDIR)$(prefix)/share/man/man1/pbuffer.1
	-mandb -pq

else
$(shell echo "Install help2man for man page generation" >&2)
endif
