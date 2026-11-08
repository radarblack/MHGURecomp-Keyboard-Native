NAME      := keyboard_movement
VERSION   ?= $(shell cat VERSION)
LOG_LEVEL ?= KBM_LOG_INFO
BUILD     ?= release
OUT       ?= build

SRC := src/module.c src/boot.c src/hooks.c src/driver.c src/keys.c src/movement.c src/config.c src/host.c src/log.c src/platform.c
OBJ_FLAGS_COMMON := -std=c11 -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden -Iinclude -Isrc \
    -DKBM_VERSION='"$(VERSION)"' -DKBM_DEFAULT_LOG_LEVEL=$(LOG_LEVEL)

ifeq ($(BUILD),debug)
OPT := -O0 -g
else
OPT := -O2
endif

TEST_FLAGS := -DKBM_POLL_MS=5 -DKBM_NOTE_MS=50
LINUX_CC   ?= gcc
WINDOWS_CC ?= x86_64-w64-mingw32-gcc
WINE       ?= $(shell command -v wine64 2>/dev/null || command -v wine 2>/dev/null || echo /usr/lib/wine/wine64)

.PHONY: all linux windows test test-wine package-windows package-linux clean

all: linux

linux: $(OUT)/linux/$(NAME).so

windows: $(OUT)/windows/$(NAME).dll

$(OUT)/linux/$(NAME).so: $(SRC) src/*.h include/*.h
	@mkdir -p $(dir $@)
	$(LINUX_CC) $(OPT) $(OBJ_FLAGS_COMMON) -D_GNU_SOURCE -fPIC -shared -o $@ $(SRC) -ldl -lpthread

$(OUT)/windows/$(NAME).dll: $(SRC) src/*.h include/*.h
	@mkdir -p $(dir $@)
	$(WINDOWS_CC) $(OPT) $(OBJ_FLAGS_COMMON) -shared -static-libgcc -o $@ $(SRC)

TEST_SRC := src/boot.c src/hooks.c src/driver.c src/keys.c src/movement.c src/config.c src/host.c src/log.c src/platform.c

test: $(OUT)/test_core $(OUT)/test_module $(OUT)/test_module_exported $(OUT)/test_module_nogate $(OUT)/test_module_nodispatch
	$(OUT)/test_core
	$(OUT)/test_module
	$(OUT)/test_module_exported
	$(OUT)/test_module_nogate
	$(OUT)/test_module_nodispatch

$(OUT)/test_core: tests/test_core.c src/movement.c src/config.c src/keys.c src/log.c src/platform.c src/host.c src/*.h
	@mkdir -p $(OUT)
	$(LINUX_CC) -O1 -g $(OBJ_FLAGS_COMMON) -D_GNU_SOURCE -rdynamic -o $@ tests/test_core.c src/movement.c src/config.c src/keys.c src/log.c src/platform.c src/host.c -ldl -lpthread

$(OUT)/test_module: tests/test_module.c src/module.c $(TEST_SRC) src/*.h
	@mkdir -p $(OUT)
	$(LINUX_CC) -O1 -g $(OBJ_FLAGS_COMMON) $(TEST_FLAGS) -D_GNU_SOURCE -rdynamic -o $@ tests/test_module.c src/module.c $(TEST_SRC) -ldl -lpthread

$(OUT)/test_module_exported: tests/test_module.c src/module.c $(TEST_SRC) src/*.h
	@mkdir -p $(OUT)
	$(LINUX_CC) -O1 -g $(OBJ_FLAGS_COMMON) -DMOCK_OVERLAY_EXPORT $(TEST_FLAGS) -D_GNU_SOURCE -rdynamic -o $@ tests/test_module.c src/module.c $(TEST_SRC) -ldl -lpthread

$(OUT)/test_module_nogate: tests/test_module.c src/module.c $(TEST_SRC) src/*.h
	@mkdir -p $(OUT)
	$(LINUX_CC) -O1 -g $(OBJ_FLAGS_COMMON) -DMOCK_NO_MENU_GATE $(TEST_FLAGS) -D_GNU_SOURCE -rdynamic -o $@ tests/test_module.c src/module.c $(TEST_SRC) -ldl -lpthread

$(OUT)/test_module_nodispatch: tests/test_module.c src/module.c $(TEST_SRC) src/*.h
	@mkdir -p $(OUT)
	$(LINUX_CC) -O1 -g $(OBJ_FLAGS_COMMON) -DMOCK_NO_DISPATCH $(TEST_FLAGS) -D_GNU_SOURCE -rdynamic -o $@ tests/test_module.c src/module.c $(TEST_SRC) -ldl -lpthread

test-wine: windows
	mkdir -p $(OUT)/win/engine_modules/$(NAME)
	$(WINDOWS_CC) -shared -o $(OUT)/win/SDL3.dll tests/win/mock_sdl.c
	$(WINDOWS_CC) -O1 -Iinclude -Isrc -o $(OUT)/win/winhost.exe tests/win/winhost.c -Wl,--export-all-symbols
	cp $(OUT)/windows/$(NAME).dll $(OUT)/win/engine_modules/$(NAME)/
	cd $(OUT)/win && WINEDEBUG=-all WINEPREFIX=$${WINEPREFIX:-$$HOME/.wine-kbm} $(WINE) winhost.exe

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
