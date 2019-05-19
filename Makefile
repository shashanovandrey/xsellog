TARGET_1    := xsellog
TARGET_2    := xsellogview
CC          ?= cc
INSTALL     := install
INSTALL_DIR := /usr/local/bin/

all: release

release:
	$(CC) -O2 -s -lxcb -lxcb-xfixes -lz -o $(TARGET_1) $(TARGET_1).c
	$(CC) -O2 -s -lxcb -o $(TARGET_2) $(TARGET_2).c

debug:
	$(CC) -g -ggdb -DDEBUG -Wpedantic -Wall -Wextra -Wconversion -lxcb -lxcb-xfixes -lz -o $(TARGET_1) $(TARGET_1).c
	$(CC) -g -ggdb -DDEBUG -Wpedantic -Wall -Wextra -Wconversion -lxcb -o $(TARGET_2) $(TARGET_2).c

clean:
	-rm -f ./$(TARGET_1)
	-rm -f ./$(TARGET_2)

install: release
	$(INSTALL) ./$(TARGET_1) $(INSTALL_DIR)
	$(INSTALL) ./$(TARGET_2) $(INSTALL_DIR)

uninstall: clean
	-rm -f $(INSTALL_DIR)$(TARGET_1)
	-rm -f $(INSTALL_DIR)$(TARGET_2)

.PHONY: all release debug clean install uninstall
