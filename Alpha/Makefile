# ═══════════════════════════════════════════════════════════════════════════════
# Alpha Trading System - Makefile
# ═══════════════════════════════════════════════════════════════════════════════
# Usage:
#   make              # Build release
#   make debug        # Build debug
#   make clean        # Clean build artifacts
#   make run          # Run with shadow mode
#   make run-live     # Run in live mode
# ═══════════════════════════════════════════════════════════════════════════════

CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -I./include
LDFLAGS := -pthread -lssl -lcrypto

# Release flags
RELEASE_FLAGS := -O3 -DNDEBUG -march=native

# Debug flags
DEBUG_FLAGS := -g -O0 -DDEBUG

# Detect OS
ifeq ($(OS),Windows_NT)
    LDFLAGS += -lws2_32 -lcrypt32
    TARGET := alpha.exe
else
    TARGET := alpha
endif

# Source files
SRCS := src/main.cpp
OBJS := $(SRCS:.cpp=.o)

# Default target
all: release

# Release build
release: CXXFLAGS += $(RELEASE_FLAGS)
release: $(TARGET)
	@echo "[BUILD] Release build complete: $(TARGET)"

# Debug build
debug: CXXFLAGS += $(DEBUG_FLAGS)
debug: $(TARGET)
	@echo "[BUILD] Debug build complete: $(TARGET)"

# Link
$(TARGET): $(SRCS)
	@echo "[BUILD] Compiling Alpha..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Clean
clean:
	rm -f $(TARGET) $(OBJS) *.o
	rm -rf build/
	@echo "[BUILD] Cleaned"

# Run shadow mode
run: $(TARGET)
	./$(TARGET) --shadow

# Run live mode
run-live: $(TARGET)
	./$(TARGET) --live

# Help
help:
	@echo ""
	@echo "Alpha Trading System - Build Targets"
	@echo ""
	@echo "  make          - Build release version"
	@echo "  make debug    - Build debug version"
	@echo "  make clean    - Clean build artifacts"
	@echo "  make run      - Run in shadow mode"
	@echo "  make run-live - Run in live mode"
	@echo ""

.PHONY: all release debug clean run run-live help
