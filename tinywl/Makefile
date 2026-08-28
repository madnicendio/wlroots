INSTALL_DIR = $(shell pwd)
CUSTOM_LIB_PATH=$(INSTALL_DIR)/lib64

export PKG_CONFIG_PATH := $(CUSTOM_LIB_PATH)/pkgconfig:$(PKG_CONFIG_PATH)

PKG_CONFIG?=pkg-config

PKGS="wlroots-0.21" wayland-server xkbcommon
CFLAGS_PKG_CONFIG!=$(PKG_CONFIG) --cflags $(PKGS)
CFLAGS+=$(CFLAGS_PKG_CONFIG)
LIBS!=$(PKG_CONFIG) --libs $(PKGS)
LDFLAGS += -Wl,-rpath,$(CUSTOM_LIB_PATH)

all: tinywl

tinywl.o: tinywl.c
	$(CC) -c $< -g -Werror $(CFLAGS) -I. -DWLR_USE_UNSTABLE -o $@
tinywl: tinywl.o
	$(CC) $^ -g -Werror $(CFLAGS) $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f tinywl tinywl.o

.PHONY: all clean
