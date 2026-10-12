TOPTARGETS := all clean install
SUBDIRS := tools/makedisk
.DEFAULT_GOAL := usage

ASM_FORMATTER ?= asmfmt
CLANG_FORMAT ?= clang-format

.PHONY: $(TOPTARGETS) all debug format test test-asm test-qemu verify verify-check

usage:
	@echo "SSOS-9 build targets (preemptive only):"
	@echo "  make all              Build the preemptive OS (.x and .xdf)"
	@echo "  make install          Build and copy preemptive .x/.xdf files to ~/tmp"
	@echo "  make test             Run native tests"
	@echo "  make test-asm         Run m68k assembly samples under QEMU"
	@echo "  make test-qemu        Run scheduler tests under QEMU"
	@echo "  make verify           Run automated verification and report hardware checks"
	@echo "  make clean            Remove build outputs"
	@echo "  make format           Format Markdown, C, and assembly sources"
	@echo "  make debug            Build .x and create ELF disassembly/symbol/map files"

.PHONY: usage

all: tools/makedisk
	$(MAKE) -C ssos all

debug: all
	$(MAKE) -C ssos debug

install: tools/makedisk
	$(MAKE) -C ssos install

clean:
	$(MAKE) -C tools/makedisk clean
	$(MAKE) -C ssos clean
	$(MAKE) -C tests clean

format:
	@echo "Formatting Markdown files..."
	pnpm dlx markdownlint-cli "**/*.md" --ignore "node_modules/**" --fix
	pnpm exec textlint --fix "**/*.md"
	@echo "Formatting C files using $(CLANG_FORMAT)..."
	@find . -type f -name '*.c' -not -path './.git/*' -not -path '*/build/*' -print0 | xargs -0 $(CLANG_FORMAT) -i
	@echo "Formatting assembly files using $(ASM_FORMATTER)..."
	@find . -name '*.s' -not -path './.git/*' -print0 | xargs -0 $(ASM_FORMATTER) -w
	@find . -name '*.s' -not -path './.git/*' -print0 | xargs -0 perl -pi -e 's/^(\s*)\/\/(.*)$$/$$1|$$2/'

# Native C tests and QEMU asm samples. See tests/README.md.
test:
	$(MAKE) -C tests test

test-asm:
	$(MAKE) -C tests test-asm

test-qemu:
	$(MAKE) -C tests test-qemu

# Report which build targets need hardware verification for the current change
# set. REF selects the diff target (default: working tree vs HEAD; use HEAD~1
# to analyze the last commit, or main..HEAD for a branch).
verify-check:
	@tools/verify-check.sh $(REF)

# Run the preemptive verification suites and report the target hardware boundary.
verify: all test test-asm test-qemu
	$(MAKE) verify-check
