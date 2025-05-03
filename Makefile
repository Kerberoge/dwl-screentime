PREFIX = ~/.local

USER = $(shell echo $$USER)
CFLAGS = -DUSER=\"$(USER)\"

screentimed: config.h screentimed.c
	$(CC) $(CFLAGS) screentimed.c -o $@

clean:
	rm screentimed

install:
	install -s -D -t $(PREFIX)/bin screentimed
	install -D -t $(PREFIX)/bin screentime

.PHONY: clean install
