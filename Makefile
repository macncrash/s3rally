# S3-16 + S3 RALLY
CXX      ?= clang++
# Prefer the native (arm64) Homebrew SDL2 on Apple Silicon.
SDL2_CONFIG ?= $(firstword $(wildcard /opt/homebrew/bin/sdl2-config) sdl2-config)
SDL_CFLAGS := $(shell $(SDL2_CONFIG) --cflags)
SDL_LIBS   := $(shell $(SDL2_CONFIG) --libs)
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -MMD -MP $(SDL_CFLAGS) -Isrc
SRC := $(wildcard src/console/*.cpp src/game/*.cpp src/*.cpp)
OBJ := $(SRC:src/%.cpp=build/%.o)

s3: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(SDL_LIBS)

build/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

run: s3
	./s3

sim: s3
	./s3 --sim

clean:
	rm -rf build s3

.PHONY: run sim clean
-include $(OBJ:.o=.d)
