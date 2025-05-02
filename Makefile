PREFIX = ~/.local

USER = $(shell echo $$USER)
CFLAGS = -DUSER=\"$(USER)\"

screentimed: screentimed.c

clean:
	rm screentimed

install:
	install -s -D -t $(PREFIX)/bin screentimed
	install -D -t $(PREFIX)/bin screentime

.PHONY: clean install
