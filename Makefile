NAME      := BetterKeyboard
VERSION   ?= $(shell cat VERSION)
BUILD     ?= release
OUT       ?= build

SRC := src/module.c src/boot.c src/itemkeys.c src/ikcore.c src/hooks.c src/aimkeep.c src/driver.c src/keys.c src/movement.c src/config.c src/host.c src/platform.c
OBJ_FLAGS_COMMON := -std=c11 -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden -Iinclude -Isrc \
    -DKBM_VERSION='"$(VERSION)"'

ifeq ($(BUILD),debug)
OPT := -O0 -g
else
OPT := -O2
endif

LINUX_CC   ?= gcc
WINDOWS_CC ?= x86_64-w64-mingw32-gcc

.PHONY: all linux windows manifest package-windows package-linux clean

all: linux

linux: $(OUT)/linux/$(NAME).so

windows: $(OUT)/windows/$(NAME).dll

$(OUT)/linux/$(NAME).so: $(SRC) src/*.h include/*.h
	@mkdir -p $(dir $@)
	$(LINUX_CC) $(OPT) $(OBJ_FLAGS_COMMON) -D_GNU_SOURCE -fPIC -shared -o $@ $(SRC) -ldl -lpthread

$(OUT)/windows/$(NAME).dll: $(SRC) src/*.h include/*.h
	@mkdir -p $(dir $@)
	$(WINDOWS_CC) $(OPT) $(OBJ_FLAGS_COMMON) -shared -static-libgcc -o $@ $(SRC)

package-windows: windows
	rm -rf $(OUT)/pkg-windows
	mkdir -p $(OUT)/pkg-windows/engine_modules/$(NAME)
	cp $(OUT)/windows/$(NAME).dll $(OUT)/pkg-windows/engine_modules/$(NAME)/
	cp config/BetterKeyboard.ini $(OUT)/pkg-windows/engine_modules/$(NAME)/

package-linux: linux
	rm -rf $(OUT)/pkg-linux
	mkdir -p $(OUT)/pkg-linux/engine_modules/$(NAME)
	cp $(OUT)/linux/$(NAME).so $(OUT)/pkg-linux/engine_modules/$(NAME)/
	cp config/BetterKeyboard.ini $(OUT)/pkg-linux/engine_modules/$(NAME)/

manifest:
	bash tools/make_manifest.sh

clean:
	rm -rf $(OUT)
