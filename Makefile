# Build wlroots using meson

ROOT_DIR=$(shell pwd)
INSTALL_DIR := $(ROOT_DIR)/install
BUILD_DIR := $(ROOT_DIR)/build

.PHONY: clean all run_compositor

$(BUILD_DIR)/libwlroots-0.21.so:
	meson setup build --prefix $(INSTALL_DIR)
	meson compile -C build

$(INSTALL_DIR)/lib64/libwlroots-0.21.so: $(BUILD_DIR)/libwlroots-0.21.so
	meson install -C build

tinywl/tinywl: tinywl/tinywl.c $(INSTALL_DIR)/lib64/libwlroots-0.21.so
	make INSTALL_DIR=$(INSTALL_DIR) -C tinywl

run_compositor: tinywl/tinywl
	tinywl/tinywl

clean:
	make -C tinywl INSTALL_DIR=$(INSTALL_DIR) clean
	rm -rf install
	rm -rf build
