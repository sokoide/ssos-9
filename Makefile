TOPTARGETS := all clean install
SUBDIRS := tools/makedisk
.DEFAULT_GOAL := usage

ASM_FORMATTER ?= asmfmt
CLANG_FORMAT ?= clang-format

.PHONY: $(TOPTARGETS) all debug format loc test test-asm test-link test-qemu test-emulator test-emulator-a2 test-emulator-b1 test-emulator-list verify verify-check

usage:
	@echo "SSOS-9 build targets (preemptive only):"
	@echo "  make all              Build the preemptive OS (.x and .xdf)"
	@echo "  make install          Build and copy preemptive .x/.xdf files to ~/tmp"
	@echo "  make test             Run native tests"
	@echo "  make test-asm         Run m68k assembly samples under QEMU"
	@echo "  make test-link        Run linker host and golden tests"
	@echo "  make test-qemu        Run scheduler tests under QEMU"
	@echo "  make test-emulator    Run XEiJ integration tests against a running emulator"
	@echo "                        (select with EMULATOR_TESTS=\"a2 b1\", list with make test-emulator-list)"
	@echo "  make verify           Run automated verification and report hardware checks"
	@echo "  make clean            Remove build outputs"
	@echo "  make format           Format Markdown, C, and assembly sources"
	@echo "  make debug            Build .x and create ELF disassembly/symbol/map files"
	@echo "  make loc              Count source lines, excluding node_modules and build outputs"

.PHONY: usage

all: tools/makedisk
	$(MAKE) -C ssos all
	$(MAKE) -C apps/edit all
	$(MAKE) -C apps/asm all
	$(MAKE) -C apps/link all

debug: all
	$(MAKE) -C ssos debug

install: tools/makedisk
	$(MAKE) -C ssos install

clean:
	$(MAKE) -C tools/makedisk clean
	$(MAKE) -C ssos clean
	$(MAKE) -C apps/edit clean
	$(MAKE) -C apps/asm clean
	$(MAKE) -C apps/link clean
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

loc:
	gocloc --not-match-d='node_modules|build|\.git' .

# Native C tests and QEMU asm samples. See tests/README.md.
test:
	$(MAKE) -C tests test

test-asm:
	$(MAKE) -C tests test-asm

test-link:
	$(MAKE) -C tests test-link

test-qemu:
	$(MAKE) -C tests test-qemu

# XEiJ integration tests against a running emulator instance. They rebuild and
# install ssos9.x to ~/tmp first, then drive the guest through the control
# pipe. Needs the accessibility permission for the calling terminal, XEiJ with
# the paste and control pipe enabled, and the guest idle at the Human68k
# prompt. See docs/emulator-integration-test-plan.md.
#
# Select tests with EMULATOR_TESTS (space-separated IDs):
#   make test-emulator                      # run all
#   make test-emulator EMULATOR_TESTS=b1    # one test
#   EMULATOR_TESTS="a2 b1" make test-emulator
#   make test-emulator-list                 # list selectable IDs
EMULATOR_ALL_TESTS = a2 a3 b1 e3
EMULATOR_TESTS ?= $(EMULATOR_ALL_TESTS)

.PHONY: test-emulator-list
test-emulator-list:
	@echo "selectable XEiJ integration tests (EMULATOR_TESTS):"
	@echo "  a2  repeated XEiJ startup and return exercise"
	@echo "  a3  XDF IPL boot and PID 1 exit rejection (restarts XEiJ twice)"
	@echo "  b1  Timer D tick rate measurement (200 Hz nominal)"
	@echo "  e3  builtin hello launch with trace evidence"
	@echo 'usage: make test-emulator [EMULATOR_TESTS="a2 a3 b1 e3"]'

test-emulator-a2: install
	tools/emulator-itest.sh a2

test-emulator-a3: install
	tools/emulator-itest.sh a3

test-emulator-b1: install
	tools/emulator-itest.sh b1

test-emulator-e3: install
	tools/emulator-itest.sh e3

test-emulator: check-emulator-tests install
	@for t in $(EMULATOR_TESTS); do \
		tools/emulator-itest.sh $$t || exit 1 ; \
	done

check-emulator-tests:
	@for t in $(EMULATOR_TESTS); do \
		case " $(EMULATOR_ALL_TESTS) " in \
			*" $$t "*) ;; \
			*) echo "unknown emulator test: $$t (see: make test-emulator-list)"; exit 2 ;; \
		esac ; \
	done

# Report which build targets need hardware verification for the current change
# set. REF selects the diff target (default: working tree vs HEAD; use HEAD~1
# to analyze the last commit, or main..HEAD for a branch).
verify-check:
	@tools/verify-check.sh $(REF)

# Run the preemptive verification suites and report the target hardware boundary.
verify: all test test-asm test-link test-qemu
	$(MAKE) verify-check
