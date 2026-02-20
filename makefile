CC = gcc
CFLAGS = -O3 -Wall
LDFLAGS = -lsqlite3 -lcrypto -lavformat -lavcodec -lavutil
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

SRC = src/fhash.c
OBJ = $(SRC:.c=.o)
TARGET = fhash

.PHONY: all clean install uninstall debug

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

debug:
	$(CC) -g -O0 $(SRC) -o $(TARGET)_dbg $(LDFLAGS)

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET)_dbg *.db
