
DESTDIR ?= 
PREFIX  ?= /usr
COMPRESS_MAN ?= yes
STRIP_BINARY ?= yes
EXAMPLES ?= yes

CSECFLAGS ?= -fstack-protector-all -Wall --param ssp-buffer-size=4 -D_FORTIFY_SOURCE=2 -fstack-check -DPARANOID -std=gnu99
CFLAGS ?= -pipe -O2
CFLAGS += $(CSECFLAGS) $(shell pkg-config --cflags gtk+-2.0) -flto
DEBUGCFLAGS ?= -pipe -Wall -Werror -ggdb3 -export-dynamic -Wno-error=unused-variable -O0 -pipe $(CSECFLAGS) $(shell pkg-config --cflags gtk+-2.0)

CARCHFLAGS ?= -march=native

LIBS := -lm $(shell pkg-config --libs gtk+-2.0)
LDSECFLAGS ?= -Xlinker -zrelro
LDFLAGS += $(LDSECFLAGS) -pthread -flto
INC := $(INC)

INSTDIR = $(DESTDIR)$(PREFIX)

objs=\
pthreadex.o\
binary.o\
error.o\
malloc.o\
main.o\


binary=voltlogger_oscilloscope

.PHONY: doc

all: $(objs)
	$(CC) $(CARCHFLAGS) $(CFLAGS) $(LDFLAGS) $(objs) $(LIBS) -o $(binary)

%.o: %.c
	$(CC) $(CARCHFLAGS) $(CFLAGS) $(INC) $< -c -o $@

debug:
	$(CC) $(CARCHFLAGS) -D_DEBUG_SUPPORT $(DEBUGCFLAGS) $(INC) $(LDFLAGS) *.c $(LIBS) -o $(binary)


clean:
	rm -f $(binary) *.o

distclean: clean

doc:
	doxygen .doxygen

install:
	install -d "$(INSTDIR)/bin" "$(INSTDIR)/share/man/man1"
ifeq ($(STRIP_BINARY),yes)
	strip --strip-unneeded -R .comment -R .GCC.command.line -R .note.gnu.gold-version $(binary)
endif
	install -m 755 $(binary) "$(INSTDIR)"/bin/
	install -m 644 man/man1/voltlogger_oscilloscope.1 "$(INSTDIR)"/share/man/man1/
ifeq ($(COMPRESS_MAN),yes)
	rm -f "$(INSTDIR)"/share/man/man1/voltlogger_oscilloscope.1.gz
	gzip "$(INSTDIR)"/share/man/man1/voltlogger_oscilloscope.1
endif

deinstall:
	rm -f "$(INSTDIR)"/bin/$(binary)

