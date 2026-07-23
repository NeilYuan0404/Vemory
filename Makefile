CXX      := g++
PROTOC  := protoc

# Build mode: debug (default) | release | test
#   debug   — -g, bin/vemory
#   release — -O2 -DNDEBUG, bin/vemory
#   test    — -g, bin/testcase (tests/testcase.cc)
MODE ?= debug

ifeq ($(MODE),release)
  MODE_FLAGS := -O2 -DNDEBUG
  BUILD_ROOT := build/release
else ifeq ($(MODE),test)
  MODE_FLAGS := -g
  BUILD_ROOT := build/debug
else ifeq ($(MODE),debug)
  MODE_FLAGS := -g
  BUILD_ROOT := build/debug
else
  $(error Unknown MODE=$(MODE). Use debug, release, or test)
endif

# --- USearch (header-only ANN) ---
USEARCH_REPO   := https://github.com/unum-cloud/usearch.git
USEARCH_TAG    := v2.17.6
USEARCH_ROOT   := third_party/usearch
USEARCH_HEADER := $(USEARCH_ROOT)/include/usearch/index_dense.hpp
USEARCH_INC    := -I $(USEARCH_ROOT)/include -I $(USEARCH_ROOT)/fp16/include

# --- spdlog (header-only logging) ---
SPDLOG_REPO   := https://github.com/gabime/spdlog.git
SPDLOG_TAG    := v1.15.1
SPDLOG_ROOT   := third_party/spdlog
SPDLOG_HEADER := $(SPDLOG_ROOT)/include/spdlog/spdlog.h
SPDLOG_INC    := -I $(SPDLOG_ROOT)/include

CXXFLAGS := -std=c++17 -Wall -Wextra $(MODE_FLAGS) -I include -I generated $(USEARCH_INC) $(SPDLOG_INC)
PROTOBUF_LIBS := $(shell pkg-config --libs protobuf 2>/dev/null)
ifeq ($(PROTOBUF_LIBS),)
PROTOBUF_LIBS := -lprotobuf
endif
LDFLAGS  := $(PROTOBUF_LIBS)

PROTO_SRC := proto/VNode.proto
PROTO_GEN_CC := generated/VNode.pb.cc
PROTO_GEN_H  := generated/VNode.pb.h

SRC := $(wildcard src/net/*.cc) \
       $(wildcard src/util/*.cc) \
       $(wildcard src/protocol/*.cc) \
       $(wildcard src/protocol/*/*.cc) \
       $(wildcard src/storage/*.cc) \
       $(wildcard src/persist/*.cc) \
       $(wildcard src/index/*.cc)

OBJ := $(SRC:src/%.cc=$(BUILD_ROOT)/%.o) $(BUILD_ROOT)/generated/VNode.pb.o

MAIN_SRC := src/Vemory.cc
MAIN_BIN := bin/vemory

TEST_SRC := tests/testcase.cc
TEST_BIN := bin/testcase

# --- GoogleTest ---
GTEST_REPO     := https://github.com/google/googletest.git
GTEST_TAG      := v1.14.0
GTEST_ROOT     := third_party/googletest
GTEST_DIR      := $(GTEST_ROOT)/googletest
GTEST_INC      := -I $(GTEST_DIR)/include -I $(GTEST_DIR)
GTEST_OBJ      := build/gtest/gtest-all.o build/gtest/gtest_main.o

UNIT_SRCS := $(wildcard tests/unit/*.cc)
UNIT_OBJS := $(UNIT_SRCS:tests/unit/%.cc=build/unit/%.o)
UNIT_BIN  := bin/unit_tests

.PHONY: all clean run test debug release testcase gtest-fetch usearch-fetch spdlog-fetch compile-commands proto

ifeq ($(MODE),test)
all: $(TEST_BIN)
else
all: $(MAIN_BIN)
endif

debug:
	$(MAKE) MODE=debug

release:
	$(MAKE) MODE=release

testcase:
	$(MAKE) MODE=test

# Re-vendor usearch headers into third_party/usearch (runtime dep, tracked in tree).
usearch-fetch:
	@mkdir -p third_party
	rm -rf $(USEARCH_ROOT).tmp
	git clone --depth 1 --branch $(USEARCH_TAG) $(USEARCH_REPO) $(USEARCH_ROOT).tmp
	cd $(USEARCH_ROOT).tmp && git submodule update --init --depth 1 fp16
	rm -rf $(USEARCH_ROOT)
	mkdir -p $(USEARCH_ROOT)/include $(USEARCH_ROOT)/fp16
	cp -a $(USEARCH_ROOT).tmp/include/usearch $(USEARCH_ROOT)/include/
	cp -a $(USEARCH_ROOT).tmp/fp16/include $(USEARCH_ROOT)/fp16/
	cp $(USEARCH_ROOT).tmp/VERSION $(USEARCH_ROOT)/VERSION 2>/dev/null || \
	  echo $(USEARCH_TAG) > $(USEARCH_ROOT)/VERSION
	rm -rf $(USEARCH_ROOT).tmp
	@echo "Vendored usearch $(USEARCH_TAG) into $(USEARCH_ROOT)"

# Re-vendor spdlog headers into third_party/spdlog (runtime dep, tracked in tree).
spdlog-fetch:
	@mkdir -p third_party
	rm -rf $(SPDLOG_ROOT).tmp
	git clone --depth 1 --branch $(SPDLOG_TAG) $(SPDLOG_REPO) $(SPDLOG_ROOT).tmp
	rm -rf $(SPDLOG_ROOT)
	mkdir -p $(SPDLOG_ROOT)/include
	cp -a $(SPDLOG_ROOT).tmp/include/spdlog $(SPDLOG_ROOT)/include/
	echo $(SPDLOG_TAG) > $(SPDLOG_ROOT)/VERSION
	rm -rf $(SPDLOG_ROOT).tmp
	@echo "Vendored spdlog $(SPDLOG_TAG) into $(SPDLOG_ROOT)"

proto: $(PROTO_GEN_CC) $(PROTO_GEN_H)

$(PROTO_GEN_CC) $(PROTO_GEN_H): $(PROTO_SRC)
	@mkdir -p generated
	$(PROTOC) -I proto --cpp_out=generated $(PROTO_SRC)

compile-commands:
	python3 scripts/gen_compile_commands.py

$(GTEST_DIR)/src/gtest-all.cc:
	@mkdir -p third_party
	git clone --depth 1 --branch $(GTEST_TAG) $(GTEST_REPO) $(GTEST_ROOT)

gtest-fetch: $(GTEST_DIR)/src/gtest-all.cc

$(MAIN_BIN): $(OBJ) $(MAIN_SRC) | bin
	$(CXX) $(CXXFLAGS) $(OBJ) $(MAIN_SRC) -o $@ $(LDFLAGS)

$(TEST_BIN): $(OBJ) $(TEST_SRC) | bin
	$(CXX) $(CXXFLAGS) $(OBJ) $(TEST_SRC) -o $@ $(LDFLAGS)

$(BUILD_ROOT)/%.o: src/%.cc $(PROTO_GEN_H) $(USEARCH_HEADER) $(SPDLOG_HEADER)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_ROOT)/generated/VNode.pb.o: $(PROTO_GEN_CC) $(PROTO_GEN_H)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $(PROTO_GEN_CC) -o $@

build/gtest/gtest-all.o: $(GTEST_DIR)/src/gtest-all.cc
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -g -I $(GTEST_DIR)/include -I $(GTEST_DIR) -pthread -c $< -o $@

build/gtest/gtest_main.o: $(GTEST_DIR)/src/gtest_main.cc | $(GTEST_DIR)/src/gtest-all.cc
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -g -I $(GTEST_DIR)/include -I $(GTEST_DIR) -pthread -c $< -o $@

build/unit/%.o: tests/unit/%.cc $(PROTO_GEN_H) $(USEARCH_HEADER) $(SPDLOG_HEADER) | $(GTEST_DIR)/src/gtest-all.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(GTEST_INC) -pthread -c $< -o $@

$(UNIT_BIN): $(UNIT_OBJS) $(OBJ) $(GTEST_OBJ) | bin
	$(CXX) $(CXXFLAGS) $(UNIT_OBJS) $(OBJ) $(GTEST_OBJ) -o $@ -pthread $(LDFLAGS)

test: $(UNIT_BIN)
	./$(UNIT_BIN)

bin:
	mkdir -p bin

run: $(MAIN_BIN)
	./$(MAIN_BIN)

clean:
	rm -rf build bin generated
