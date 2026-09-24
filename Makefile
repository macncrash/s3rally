# S3-16 + S3 RALLY
CXX      ?= clang++
# Prefer the native (arm64) Homebrew SDL2 on Apple Silicon.
SDL2_CONFIG ?= $(firstword $(wildcard /opt/homebrew/bin/sdl2-config) sdl2-config)
SDL_CFLAGS := $(shell $(SDL2_CONFIG) --cflags)
SDL_LIBS   := $(shell $(SDL2_CONFIG) --libs)
BUILD_ID   := $(shell git rev-parse --short HEAD 2>/dev/null || echo dev)
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -MMD -MP $(SDL_CFLAGS) -Isrc -DS3_BUILD='"$(BUILD_ID)"'
SRC := $(wildcard src/console/*.cpp src/game/*.cpp src/*.cpp)
OBJ := $(SRC:src/%.cpp=build/%.o)

s3: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(SDL_LIBS)

build/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# The files that display the build id rebuild whenever the git commit changes.
BUILD_STAMP := build/.build-$(BUILD_ID)
$(BUILD_STAMP):
	@mkdir -p build && rm -f build/.build-* && touch $@
build/game/rally.o build/main.o: $(BUILD_STAMP)

run: s3
	./s3

sim: s3
	./s3 --sim

# WebAssembly build for browsers (needs Emscripten: em++ on PATH).
web:
	@mkdir -p build-web
	em++ -std=c++17 -O2 -Isrc -DS3_BUILD='"$(BUILD_ID)"' -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=1048576 \
		-sENVIRONMENT=web --shell-file web/shell.html $(SRC) -o build-web/index.html

clean:
	rm -rf build build-web s3

.PHONY: run sim web clean
-include $(OBJ:.o=.d)
