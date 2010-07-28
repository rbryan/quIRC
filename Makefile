# Makefile for quIRC

CC ?= gcc
CFLAGS ?= -Wall
VERSION := `git describe --tags`
PREFIX ?= /usr/local

all: quirc

install: all
	install -sD quirc $(PREFIX)/bin/quirc

quirc: quirc.c ttyraw.o ttyraw.h ttyesc.o ttyesc.h irc.o irc.h bits.o bits.h colour.o colour.h buffer.o buffer.h numeric.h
	-./gitversion
	$(CC) $(CFLAGS) -o quirc quirc.c ttyraw.o ttyesc.o irc.o bits.o colour.o buffer.o

# TODO use funky make cleverness for these rules as they're all basically the same

ttyraw.o: ttyraw.c ttyraw.h
	$(CC) $(CFLAGS) -o ttyraw.o -c ttyraw.c

ttyesc.o: ttyesc.c ttyesc.h
	$(CC) $(CFLAGS) -o ttyesc.o -c ttyesc.c

irc.o: irc.c irc.h
	$(CC) $(CFLAGS) -o irc.o -c irc.c

bits.o: bits.c bits.h
	$(CC) $(CFLAGS) -o bits.o -c bits.c

colour.o: colour.c colour.h
	$(CC) $(CFLAGS) -o colour.o -c colour.c

buffer.o: buffer.c buffer.h
	$(CC) $(CFLAGS) -o buffer.o -c buffer.c

dist: all
	-mkdir quirc_$(VERSION)
	for p in $$(ls); do cp $$p quirc_$(VERSION)/$$p; done;
	rm quirc_$(VERSION)/*.tar.gz
	tar -cvvf quirc_$(VERSION).tar quirc_$(VERSION)/
	gzip quirc_$(VERSION).tar
	rm -r quirc_$(VERSION)

