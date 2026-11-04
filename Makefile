# Command-line front end for the CMake presets (devcontainer, CI), with the same target names as
# the KM-Sweetch project. CMake does the build; this file only names the steps.
#
#   make             build the Linux program (build/linux/dso2100scope)
#   make help        list the targets
#
# A "## text" comment directly above a target puts it in "make help".

.DEFAULT_GOAL := all

SHOT ?= build/screenshot.png

.PHONY: all debug windows test run screenshot format format-check tidy check \
        check-thirdparties upgrade-thirdparties clean help

## Build the Linux program: build/linux/dso2100scope (warnings are errors)
all:
	cmake --preset linux
	cmake --build --preset linux

## Debug build for gdb / VS Code F5: build/linux-debug/dso2100scope
debug:
	cmake --preset linux-debug
	cmake --build --preset linux-debug

## Cross-build the Windows program and its zip: build/windows/dist/dso2100scope-windows.zip
windows:
	cmake --preset windows
	cmake --build --preset windows

## Unit tests of core/ (GoogleTest): make test [TEST_FILTER='Measure*']
test: all
	GTEST_FILTER='$(or $(TEST_FILTER),*)' ctest --preset linux

## Start the Linux program (demo signals without a scope; DSO_USB_TRACE=1 logs USB)
run: all
	./build/linux/dso2100scope

## Headless smoke test: run in demo mode and save a screenshot to $(SHOT)
screenshot: all
	QT_QPA_PLATFORM=offscreen DSO_SCREENSHOT=$(abspath $(SHOT)) ./build/linux/dso2100scope

## Format our code (src/, tests/) with clang-format
format:
	cmake --preset linux >/dev/null
	cmake --build --preset linux --target format

## Check that our code is formatted (CI), changes nothing
format-check:
	cmake --preset linux >/dev/null
	cmake --build --preset linux --target format-check

## Static checks of our code with clang-tidy (.clang-tidy)
tidy: all
	cmake --build --preset linux --target tidy

## Everything CI runs: format check, Linux build + tests + smoke test, Windows build
check: format-check test screenshot windows

## Pinned third-party versions (Qt, libusb, clang tools) against the newest upstream
check-thirdparties:
	sh tools/thirdparty.sh check

## Upgrade pinned versions in .devcontainer/Dockerfile: make upgrade-thirdparties [LIB=qt [VER=6.8.4]]
## Then rebuild the container, make check, try on the scope, commit
upgrade-thirdparties:
	sh tools/thirdparty.sh upgrade $(LIB) $(VER)

## Delete all build trees
clean:
	rm -rf build

help:
	@echo "Usage: make [target] [TEST_FILTER=...] [SHOT=...] [LIB=... VER=...]"
	@echo
	@awk '\
	  /^## / { desc = (desc ? desc " " : "") substr($$0, 4); next } \
	  /^[a-zA-Z0-9_.-]+:([^=]|$$)/ { \
	    if (desc != "") { split($$0, a, ":"); printf "  \033[36m%-20s\033[0m %s\n", a[1], desc } \
	  } \
	  { desc = "" }' $(MAKEFILE_LIST)
