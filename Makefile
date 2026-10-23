# Fallback build for machines without CMake:  make && make test
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Iinclude

CORE_SRC := $(filter-out src/main.cpp,$(wildcard src/*.cpp))
CORE_OBJ := $(patsubst src/%.cpp,build/obj/%.o,$(CORE_SRC))

all: build/CloudResourceSimulator build/crs_tests build/CloudResourceSimulatorWeb

build/obj/%.o: src/%.cpp $(wildcard include/*.h)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/CloudResourceSimulator: src/main.cpp $(CORE_OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

build/crs_tests: tests/tests.cpp $(CORE_OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

build/CloudResourceSimulatorWeb: web/server.cpp $(CORE_OBJ)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@ $(if $(filter Windows_NT,$(OS)),-lws2_32,)

web: build/CloudResourceSimulatorWeb
	./build/CloudResourceSimulatorWeb

test: build/crs_tests
	./build/crs_tests

clean:
	rm -rf build

.PHONY: all test clean web
