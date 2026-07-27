CXX      := g++
PROTOC  := protoc

# Build mode: release (default) | debug | test
#   release — -O2 -DNDEBUG, bin/vemory
#   debug   — -g, bin/vemory
#   test    — -g, bin/testcase (tests/testcase.cc)
MODE ?= release

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

# Global heap via vendored gperftools tcmalloc_minimal (STL/protobuf/new).
# Source under third_party/gperftools (make gperftools-fetch); built into prefix/ on demand.
# usearch mmap is unchanged. Disable with TCMALLOC=0.
GPERFTOOLS_TAG    := gperftools-2.15
GPERFTOOLS_URL    := https://github.com/gperftools/gperftools/releases/download/$(GPERFTOOLS_TAG)/$(GPERFTOOLS_TAG).tar.gz
GPERFTOOLS_ROOT   := third_party/gperftools
GPERFTOOLS_PREFIX := $(GPERFTOOLS_ROOT)/prefix
GPERFTOOLS_LIB    := $(GPERFTOOLS_PREFIX)/lib/libtcmalloc_minimal.a
GPERFTOOLS_CONF   := $(GPERFTOOLS_ROOT)/configure

TCMALLOC ?= 1
TCMALLOC_DEP :=
ifeq ($(TCMALLOC),1)
  # Static .a + whole-archive so malloc/new overrides are pulled in.
  TCMALLOC_LIBS := -Wl,--whole-archive $(abspath $(GPERFTOOLS_LIB)) -Wl,--no-whole-archive -pthread
  TCMALLOC_DEP := $(GPERFTOOLS_LIB)
  LDFLAGS += $(TCMALLOC_LIBS)
endif

# Optional liburing for AOF io_uring backend (fallback to thread if missing).
LIBURING_CFLAGS := $(shell pkg-config --cflags liburing 2>/dev/null)
LIBURING_LIBS   := $(shell pkg-config --libs liburing 2>/dev/null)
ifneq ($(LIBURING_LIBS),)
  CXXFLAGS += -DVEMORY_HAVE_LIBURING=1 $(LIBURING_CFLAGS)
  LDFLAGS  += $(LIBURING_LIBS)
else
  CXXFLAGS += -DVEMORY_HAVE_LIBURING=0
endif

PROTO_SRC := proto/VNode.proto proto/WalEntry.proto
PROTO_GEN_CC := generated/VNode.pb.cc generated/WalEntry.pb.cc
PROTO_GEN_H  := generated/VNode.pb.h generated/WalEntry.pb.h

SRC := $(wildcard src/net/*.cc) \
       $(wildcard src/util/*.cc) \
       $(wildcard src/protocol/*.cc) \
       $(wildcard src/protocol/*/*.cc) \
       $(wildcard src/storage/*.cc) \
       $(wildcard src/persist/*.cc) \
       $(wildcard src/mutate/*.cc) \
       $(wildcard src/index/*.cc) \
       $(wildcard src/replication/*.cc)

OBJ := $(SRC:src/%.cc=$(BUILD_ROOT)/%.o) \
       $(BUILD_ROOT)/generated/VNode.pb.o \
       $(BUILD_ROOT)/generated/WalEntry.pb.o

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

.PHONY: all clean run test debug release testcase gtest-fetch usearch-fetch \
        spdlog-fetch gperftools-fetch gperftools-build gperftools-clean \
        compile-commands proto

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

# Vendor gperftools source into third_party/gperftools (tracked in tree).
gperftools-fetch:
	@mkdir -p third_party
	rm -rf $(GPERFTOOLS_ROOT).tmp $(GPERFTOOLS_ROOT).tgz
	curl -fsSL -o $(GPERFTOOLS_ROOT).tgz $(GPERFTOOLS_URL)
	mkdir -p $(GPERFTOOLS_ROOT).tmp
	tar -xzf $(GPERFTOOLS_ROOT).tgz -C $(GPERFTOOLS_ROOT).tmp --strip-components=1
	rm -rf $(GPERFTOOLS_ROOT)
	mv $(GPERFTOOLS_ROOT).tmp $(GPERFTOOLS_ROOT)
	rm -f $(GPERFTOOLS_ROOT).tgz
	echo $(GPERFTOOLS_TAG) > $(GPERFTOOLS_ROOT)/VERSION
	@echo "Vendored $(GPERFTOOLS_TAG) into $(GPERFTOOLS_ROOT)"

# Out-of-tree build → static libtcmalloc_minimal.a under prefix/.
gperftools-build: $(GPERFTOOLS_LIB)

$(GPERFTOOLS_CONF):
	@if [ ! -f $(GPERFTOOLS_CONF) ]; then \
	  $(MAKE) gperftools-fetch; \
	fi

$(GPERFTOOLS_LIB): $(GPERFTOOLS_CONF)
	@mkdir -p $(GPERFTOOLS_ROOT)/.build $(GPERFTOOLS_PREFIX)
	cd $(GPERFTOOLS_ROOT)/.build && \
	  ../configure --prefix="$(abspath $(GPERFTOOLS_PREFIX))" \
	    --enable-minimal --enable-static --disable-shared \
	    --disable-debugalloc && \
	  $(MAKE) -j$$(nproc 2>/dev/null || echo 2) && \
	  $(MAKE) install
	@test -f $(GPERFTOOLS_LIB) || (echo "error: missing $(GPERFTOOLS_LIB)" >&2; exit 1)

gperftools-clean:
	rm -rf $(GPERFTOOLS_ROOT)/.build $(GPERFTOOLS_PREFIX)

proto: $(PROTO_GEN_CC) $(PROTO_GEN_H)

$(PROTO_GEN_CC) $(PROTO_GEN_H): $(PROTO_SRC)
	@mkdir -p generated
	$(PROTOC) -I proto --cpp_out=generated proto/VNode.proto
	$(PROTOC) -I proto --cpp_out=generated proto/WalEntry.proto

compile-commands:
	python3 scripts/gen_compile_commands.py

$(GTEST_DIR)/src/gtest-all.cc:
	@mkdir -p third_party
	git clone --depth 1 --branch $(GTEST_TAG) $(GTEST_REPO) $(GTEST_ROOT)

gtest-fetch: $(GTEST_DIR)/src/gtest-all.cc

$(MAIN_BIN): $(OBJ) $(MAIN_SRC) $(TCMALLOC_DEP) | bin
	$(CXX) $(CXXFLAGS) $(OBJ) $(MAIN_SRC) -o $@ $(LDFLAGS)

$(TEST_BIN): $(OBJ) $(TEST_SRC) $(TCMALLOC_DEP) | bin
	$(CXX) $(CXXFLAGS) $(OBJ) $(TEST_SRC) -o $@ $(LDFLAGS)

$(BUILD_ROOT)/%.o: src/%.cc $(PROTO_GEN_H) $(USEARCH_HEADER) $(SPDLOG_HEADER)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_ROOT)/generated/VNode.pb.o: generated/VNode.pb.cc generated/VNode.pb.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c generated/VNode.pb.cc -o $@

$(BUILD_ROOT)/generated/WalEntry.pb.o: generated/WalEntry.pb.cc generated/WalEntry.pb.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c generated/WalEntry.pb.cc -o $@

build/gtest/gtest-all.o: $(GTEST_DIR)/src/gtest-all.cc
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -g -I $(GTEST_DIR)/include -I $(GTEST_DIR) -pthread -c $< -o $@

build/gtest/gtest_main.o: $(GTEST_DIR)/src/gtest_main.cc | $(GTEST_DIR)/src/gtest-all.cc
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -g -I $(GTEST_DIR)/include -I $(GTEST_DIR) -pthread -c $< -o $@

build/unit/%.o: tests/unit/%.cc $(PROTO_GEN_H) $(USEARCH_HEADER) $(SPDLOG_HEADER) | $(GTEST_DIR)/src/gtest-all.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(GTEST_INC) -pthread -c $< -o $@

$(UNIT_BIN): $(UNIT_OBJS) $(OBJ) $(GTEST_OBJ) $(TCMALLOC_DEP) | bin
	$(CXX) $(CXXFLAGS) $(UNIT_OBJS) $(OBJ) $(GTEST_OBJ) -o $@ -pthread $(LDFLAGS)

test: $(UNIT_BIN)
	./$(UNIT_BIN)

bin:
	mkdir -p bin

run: $(MAIN_BIN)
	./$(MAIN_BIN)

clean:
	rm -rf build bin generated
