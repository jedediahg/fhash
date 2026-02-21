CC = gcc
CFLAGS = -O3 -Wall -Iinclude
LDFLAGS = -lsqlite3 -lcrypto -lavformat -lavcodec -lavutil
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

SRC = src/fhash.c src/utils.c src/hashing.c src/db.c
OBJ = $(SRC:.c=.o)
TARGET = fhash

.PHONY: all clean install uninstall debug

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

debug: CFLAGS += -g -O0
debug: all

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET)_dbg $(OBJ) *.db
