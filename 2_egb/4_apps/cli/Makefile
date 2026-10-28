CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11
TARGET := ega-cli

.PHONY: all clean install

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $@ $<

install: $(TARGET)
	install -m 0755 $(TARGET) /usr/local/bin/$(TARGET)

clean:
	rm -f $(TARGET)
