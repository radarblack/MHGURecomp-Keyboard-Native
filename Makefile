NAME      := keyboard_movement
VERSION   ?= 0.1.0
LOG_LEVEL ?= KBM_LOG_INFO
BUILD     ?= release
OUT       ?= build

SRC := src/module.c src/hooks.c src/driver.c src/keys.c src/movement.c src/config.c src/host.c src/log.c src/platform.c
OBJ_FLAGS_COMMON := -std=c11 -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden -Iinclude -Isrc \
    -DKBM_VERSION='"$(VERSION)"' -DKBM_DEFAULT_LOG_LEVEL=$(LOG_LEVEL)

ifeq ($(BUILD),debug)
OPT := -O0 -g
else
OPT := -O2
endif

LINUX_CC   ?= gcc
WINDOWS_CC ?= x86_64-w64-mingw32-gcc

.PHONY: all linux windows test package-windows package-linux clean

all: linux

linux: $(OUT)/linux/$(NAME).so

windows: $(OUT)/windows/$(NAME).dll

$(OUT)/linux/$(NAME).so: $(SRC) src/*.h include/*.h
	@mkdir -p $(dir $@)
	$(LINUX_CC) $(OPT) $(OBJ_FLAGS_COMMON) -D_GNU_SOURCE -fPIC -shared -o $@ $(SRC) -ldl -lpthread

$(OUT)/windows/$(NAME).dll: $(SRC) src/*.h include/*.h
	@mkdir -p $(dir $@)
	$(WINDOWS_CC) $(OPT) $(OBJ_FLAGS_COMMON) -shared -static-libgcc -o $@ $(SRC)

TEST_SRC := src/hooks.c src/driver.c src/keys.c src/movement.c src/config.c src/host.c src/log.c src/platform.c

test: $(OUT)/test_core $(OUT)/test_module
	$(OUT)/test_core
	$(OUT)/test_module

$(OUT)/test_core: tests/test_core.c src/movement.c src/config.c src/keys.c src/log.c src/platform.c src/host.c src/*.h
	@mkdir -p $(OUT)
	$(LINUX_CC) -O1 -g $(OBJ_FLAGS_COMMON) -D_GNU_SOURCE -rdynamic -o $@ tests/test_core.c src/movement.c src/config.c src/keys.c src/log.c src/platform.c src/host.c -ldl -lpthread

$(OUT)/test_module: tests/test_module.c src/module.c $(TEST_SRC) src/*.h
	@mkdir -p $(OUT)
	$(LINUX_CC) -O1 -g $(OBJ_FLAGS_COMMON) -D_GNU_SOURCE -rdynamic -o $@ tests/test_module.c src/module.c $(TEST_SRC) -ldl -lpthread

package-windows: windows
	rm -rf $(OUT)/pkg-windows
	mkdir -p $(OUT)/pkg-windows/engine_modules/$(NAME)
	cp $(OUT)/windows/$(NAME).dll $(OUT)/pkg-windows/engine_modules/$(NAME)/
	cp config/$(NAME).ini $(OUT)/pkg-windows/engine_modules/$(NAME)/
	cp README.md LICENSE NOTICE $(OUT)/pkg-windows/engine_modules/$(NAME)/

package-linux: linux
	rm -rf $(OUT)/pkg-linux
	mkdir -p $(OUT)/pkg-linux/engine_modules/$(NAME)
	cp $(OUT)/linux/$(NAME).so $(OUT)/pkg-linux/engine_modules/$(NAME)/
	cp config/$(NAME).ini $(OUT)/pkg-linux/engine_modules/$(NAME)/
	cp README.md LICENSE NOTICE $(OUT)/pkg-linux/engine_modules/$(NAME)/

clean:
	rm -rf $(OUT)
