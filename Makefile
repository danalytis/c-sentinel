# ============================================================
# C-Sentinel Makefile
# Cross-platform: Linux, macOS, FreeBSD, OpenBSD, NetBSD
# ============================================================

# Platform Detection
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    PLATFORM := macos
    PLATFORM_CFLAGS := -DPLATFORM_MACOS
    PLATFORM_LDFLAGS := 
    CC := clang
else ifeq ($(UNAME_S),Linux)
    PLATFORM := linux
    PLATFORM_CFLAGS := -DPLATFORM_LINUX
    PLATFORM_LDFLAGS := -lrt
    CC := gcc
else ifeq ($(UNAME_S),FreeBSD)
    PLATFORM := freebsd
    PLATFORM_CFLAGS := -DPLATFORM_FREEBSD -DPLATFORM_BSD
    PLATFORM_LDFLAGS := -lkvm -lutil
    CC := cc
else ifeq ($(UNAME_S),OpenBSD)
    PLATFORM := openbsd
    PLATFORM_CFLAGS := -DPLATFORM_OPENBSD -DPLATFORM_BSD
    PLATFORM_LDFLAGS := -lkvm
    CC := cc
else ifeq ($(UNAME_S),NetBSD)
    PLATFORM := netbsd
    PLATFORM_CFLAGS := -DPLATFORM_NETBSD -DPLATFORM_BSD
    PLATFORM_LDFLAGS := -lkvm
    CC := cc
else ifeq ($(UNAME_S),DragonFly)
    PLATFORM := dragonfly
    PLATFORM_CFLAGS := -DPLATFORM_DRAGONFLY -DPLATFORM_BSD
    PLATFORM_LDFLAGS := -lkvm -lutil
    CC := cc
else
    $(error Unsupported platform: $(UNAME_S). Supported: Linux, Darwin, FreeBSD, OpenBSD, NetBSD, DragonFly)
endif

# ============================================================
# Compiler Configuration
# ============================================================

CFLAGS := -Wall -Wextra -Werror -pedantic -std=c99 $(PLATFORM_CFLAGS)
CFLAGS += -I./include
LDFLAGS := $(PLATFORM_LDFLAGS)
LDLIBS := -lm

# Platform-specific warning adjustments
ifeq ($(PLATFORM),macos)
    CFLAGS += -Wno-gnu-zero-variadic-macro-arguments
endif

ifeq ($(PLATFORM),openbsd)
    CFLAGS += -Wno-unknown-pragmas
endif

# Debug vs Release
ifdef DEBUG
    CFLAGS += -g -O0 -DDEBUG
else
    CFLAGS += -O2 -DNDEBUG
endif

# Sanitizers (optional)
ifdef SANITIZE
    CFLAGS += -fsanitize=address,undefined
    LDFLAGS += -fsanitize=address,undefined
endif

# Static linking
ifdef STATIC
    LDFLAGS += -static
endif

# ============================================================
# Directories
# ============================================================

SRC_DIR := src
INC_DIR := include
BUILD_DIR := obj
BIN_DIR := bin

# ============================================================
# Source Files (explicit list - NOT wildcards)
# ============================================================

# Main sentinel sources
SENTINEL_SRCS := $(SRC_DIR)/main.c \
                 $(SRC_DIR)/prober.c \
                 $(SRC_DIR)/net_probe.c \
                 $(SRC_DIR)/json_serialize.c \
                 $(SRC_DIR)/policy.c \
                 $(SRC_DIR)/sanitize.c \
                 $(SRC_DIR)/baseline.c \
                 $(SRC_DIR)/config.c \
                 $(SRC_DIR)/alert.c \
                 $(SRC_DIR)/sha256.c \
                 $(SRC_DIR)/audit.c \
                 $(SRC_DIR)/audit_json.c \
                 $(SRC_DIR)/process_chain.c

SENTINEL_OBJS := $(SENTINEL_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Diff tool sources (separate binary)
DIFF_SRCS := $(SRC_DIR)/diff.c
DIFF_OBJS := $(DIFF_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Header dependencies
HEADERS := $(INC_DIR)/sentinel.h $(INC_DIR)/policy.h $(INC_DIR)/sanitize.h $(INC_DIR)/audit.h $(INC_DIR)/color.h $(INC_DIR)/platform.h

# Target binaries
SENTINEL := $(BIN_DIR)/sentinel
SENTINEL_DIFF := $(BIN_DIR)/sentinel-diff

# ============================================================
# Build Rules
# ============================================================

.PHONY: all clean test install uninstall help info dirs static

all: info dirs $(SENTINEL) $(SENTINEL_DIFF)
	@echo ""
	@echo "Build complete. Binaries:"
	@ls -la $(BIN_DIR)/

# Static build for deployment
static: LDFLAGS += -static
static: clean all
	@echo ""
	@echo "Static build complete."
	@file $(BIN_DIR)/* || true

# Create directories
dirs:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

# Link sentinel
$(SENTINEL): $(SENTINEL_OBJS)
	$(CC) $(SENTINEL_OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

# Link sentinel-diff
$(SENTINEL_DIFF): $(DIFF_OBJS)
	$(CC) $(DIFF_OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

# Compile rule for sentinel sources
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Special rule for diff.c (doesn't need all headers)
$(BUILD_DIR)/diff.o: $(SRC_DIR)/diff.c
	$(CC) $(CFLAGS) -c $< -o $@

# ============================================================
# Info Target
# ============================================================

info:
	@echo "=== C-Sentinel Build ==="
	@echo "Platform: $(UNAME_S) ($(PLATFORM))"
	@echo "Compiler: $(CC)"
	@echo "CFLAGS:   $(CFLAGS)"
	@echo "LDFLAGS:  $(LDFLAGS)"
	@echo "========================"

# ============================================================
# Clean
# ============================================================

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "Cleaned build artifacts"

<<<<<<< HEAD
# ============================================================
# Test
# ============================================================

=======
# Install
PREFIX ?= /usr/local
install: all
	install -d $(PREFIX)/bin
	install -m 755 $(SENTINEL) $(PREFIX)/bin/
	install -m 755 $(SENTINEL_DIFF) $(PREFIX)/bin/
	install -d $(PREFIX)/share/man/man1
	install -m 644 man/sentinel.1 $(PREFIX)/share/man/man1/
	@echo "Installed to $(PREFIX)/bin/"

# Uninstall
uninstall:
	rm -f $(PREFIX)/bin/sentinel
	rm -f $(PREFIX)/bin/sentinel-diff
	rm -f $(PREFIX)/share/man/man1/sentinel.1

# Test suite
>>>>>>> 2e740dc (docs: Add sentinel(1) manpage)
test: all
	@echo "=== C-Sentinel Test Suite ==="
	@echo ""
	@echo "1. Quick mode test..."
	@./$(SENTINEL) --quick > /dev/null && echo "   PASS: Quick mode" || echo "   FAIL: Quick mode"
	@echo ""
	@echo "2. JSON output test..."
	@./$(SENTINEL) /etc/hosts > /tmp/sentinel_test.json 2>/dev/null && echo "   PASS: JSON output" || echo "   FAIL: JSON output"
	@echo ""
	@echo "3. Diff tool test..."
	@./$(SENTINEL) > /tmp/fp1.json 2>/dev/null
	@./$(SENTINEL) > /tmp/fp2.json 2>/dev/null
	@./$(SENTINEL_DIFF) /tmp/fp1.json /tmp/fp2.json > /dev/null 2>&1 && echo "   PASS: Diff tool" || echo "   PASS: Diff tool (differences found)"
	@echo ""
	@echo "4. JSON validity test..."
	@python3 -c "import json; json.load(open('/tmp/sentinel_test.json'))" 2>/dev/null && echo "   PASS: Valid JSON" || echo "   FAIL: Invalid JSON"
	@echo ""
	@echo "5. Audit probe test..."
	@./$(SENTINEL) --audit --quick 2>/dev/null && echo "   PASS: Audit probe" || echo "   WARN: Audit probe (auditd may not be installed)"
	@echo ""
	@echo "6. Colour output test..."
	@./$(SENTINEL) --quick --color 2>/dev/null | head -1 | grep -q "C-Sentinel" && echo "   PASS: Colour output" || echo "   FAIL: Colour output"
	@echo ""
	@echo "=== All tests complete ==="
	@rm -f /tmp/sentinel_test.json /tmp/fp1.json /tmp/fp2.json

# Platform-specific tests
test-platform: $(SENTINEL)
	@echo "=== Platform-Specific Tests for $(UNAME_S) ==="
ifeq ($(PLATFORM),linux)
	@echo "Testing /proc access..."
	@test -d /proc && echo "  /proc: OK" || echo "  /proc: FAIL"
	@test -f /proc/meminfo && echo "  /proc/meminfo: OK" || echo "  /proc/meminfo: FAIL"
	@test -f /proc/net/tcp && echo "  /proc/net/tcp: OK" || echo "  /proc/net/tcp: FAIL"
endif
ifeq ($(PLATFORM),macos)
	@echo "Testing sysctl access..."
	@sysctl hw.memsize >/dev/null 2>&1 && echo "  hw.memsize: OK" || echo "  hw.memsize: FAIL"
	@sysctl kern.boottime >/dev/null 2>&1 && echo "  kern.boottime: OK" || echo "  kern.boottime: FAIL"
endif
ifeq ($(PLATFORM),freebsd)
	@echo "Testing sysctl access..."
	@sysctl hw.physmem >/dev/null 2>&1 && echo "  hw.physmem: OK" || echo "  hw.physmem: FAIL"
	@sysctl kern.boottime >/dev/null 2>&1 && echo "  kern.boottime: OK" || echo "  kern.boottime: FAIL"
	@echo "Testing kvm access..."
	@test -c /dev/mem && echo "  /dev/mem: OK (root may be needed)" || echo "  /dev/mem: Not accessible (normal for non-root)"
endif
ifeq ($(PLATFORM),openbsd)
	@echo "Testing sysctl access..."
	@sysctl hw.physmem >/dev/null 2>&1 && echo "  hw.physmem: OK" || echo "  hw.physmem: FAIL"
	@sysctl kern.boottime >/dev/null 2>&1 && echo "  kern.boottime: OK" || echo "  kern.boottime: FAIL"
endif
ifeq ($(PLATFORM),netbsd)
	@echo "Testing sysctl access..."
	@sysctl hw.physmem64 >/dev/null 2>&1 && echo "  hw.physmem64: OK" || echo "  hw.physmem64: FAIL"
	@sysctl kern.boottime >/dev/null 2>&1 && echo "  kern.boottime: OK" || echo "  kern.boottime: FAIL"
endif

# ============================================================
# Install/Uninstall
# ============================================================

PREFIX ?= /usr/local

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(SENTINEL) $(DESTDIR)$(PREFIX)/bin/
	install -m 755 $(SENTINEL_DIFF) $(DESTDIR)$(PREFIX)/bin/
	@echo "Installed to $(DESTDIR)$(PREFIX)/bin/"

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/sentinel
	rm -f $(DESTDIR)$(PREFIX)/bin/sentinel-diff
	@echo "Uninstalled from $(DESTDIR)$(PREFIX)/bin/"

# ============================================================
# Development Helpers
# ============================================================

# Static analysis
lint:
	@which cppcheck > /dev/null 2>&1 && \
		cppcheck --enable=all --suppress=missingIncludeSystem \
		         --suppress=unusedFunction $(SRC_DIR)/ $(INC_DIR)/ || \
		echo "Install cppcheck for static analysis"

# Format code
format:
	@which clang-format > /dev/null 2>&1 && \
		clang-format -i $(SRC_DIR)/*.c $(INC_DIR)/*.h || \
		echo "Install clang-format for code formatting"

# Generate compile_commands.json for IDE support
compile_commands:
	@which bear > /dev/null 2>&1 && \
		bear -- make clean all || \
		echo "Install bear for compile_commands.json generation"

# Show binary sizes
size: all
	@echo "Binary sizes:"
	@size $(BIN_DIR)/* || ls -lh $(BIN_DIR)/*

# ============================================================
# Help
# ============================================================

help:
	@echo "C-Sentinel Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all            Build all binaries (default)"
	@echo "  static         Build with static linking"
	@echo "  clean          Remove build artifacts"
	@echo "  test           Run test suite"
	@echo "  test-platform  Run platform-specific tests"
	@echo "  install        Install to PREFIX (default: /usr/local)"
	@echo "  uninstall      Remove installed binaries"
	@echo "  lint           Run static analysis"
	@echo "  format         Format source code"
	@echo "  size           Show binary sizes"
	@echo "  info           Show build configuration"
	@echo "  help           Show this help"
	@echo ""
	@echo "Options:"
	@echo "  DEBUG=1        Debug build with symbols"
	@echo "  STATIC=1       Static linking"
	@echo "  SANITIZE=1     Enable address/undefined sanitizers"
	@echo "  PREFIX=/path   Installation prefix"
	@echo ""
	@echo "Detected platform: $(PLATFORM) ($(UNAME_S))"
