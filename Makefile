TOPTARGETS := all clean install
SUBDIRS := tools/makedisk
.DEFAULT_GOAL := usage

ASM_FORMATTER ?= asmfmt
CLANG_FORMAT ?= clang-format

.PHONY: $(TOPTARGETS) all debug format loc test test-asm test-link test-qemu test-emulator test-emulator-list test-mpx68k-h2 test-emulator-benchmark test-emulator-benchmark-short test-emulator-benchmark-full test-emulator-benchmark-compare-short test-emulator-benchmark-compare-full verify verify-check

EMULATOR_SHORT_REPEATS ?= 30
EMULATOR_FULL_REPEATS ?= 10
EMULATOR_BENCHMARK_DIR ?= build/xeij/benchmarks

usage:
	@echo "SSOS-9 build targets (preemptive only):"
	@echo "  make all              Build the preemptive OS (.x and .xdf)"
	@echo "  make install          Build and copy preemptive .x/.xdf files to ~/tmp"
	@echo "  make test             Run native tests and XEiJ harness unit tests"
	@echo "  make test-asm         Run m68k assembly samples under QEMU"
	@echo "  make test-link        Run linker host and golden tests"
	@echo "  make test-qemu        Run scheduler tests under QEMU"
	@echo "  make test-emulator    Run XEiJ integration tests against a running emulator"
	@echo "                        (select with EMULATOR_TESTS=\"a2 b1\", list with make test-emulator-list)"
	@echo "  make test-emulator-automated  Run all cases using a prepared disposable profile"
	@echo "  make test-emulator-h1         Compare MFP snapshots using the staged probe"
	@echo "  make test-emulator-soak       Run the 25-cycle process recovery case"
	@echo "  make test-mpx68k-h2   Run the H2 FD1 real-floppy case on a running MPX68K"
	@echo "                        (tools/mpx68k-h2.sh; needs monitor socket + OCR)"
	@echo "  make test-emulator-benchmark  Run 30 short / 10 full repeats (long-running)"
	@echo "  make test-emulator-benchmark-compare-short  Compare short timing reports"
	@echo "  make test-emulator-benchmark-compare-full   Compare full timing reports"
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
test: test-xeij-transport test-xeij-benchmark test-mpx68k-h2-host
	$(MAKE) -C tests test

.PHONY: test-xeij-transport test-xeij-benchmark test-mpx68k-h2-host
test-xeij-transport:
	python3 tests/xeij_transport_test.py
test-xeij-benchmark:
	python3 tests/xeij_benchmark_test.py

test-mpx68k-h2-host:
	python3 tests/mpx68k_h2_test.py

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
#   make test-emulator                      # run the default (always-green) set
#   make test-emulator EMULATOR_TESTS=b1    # one test
#   EMULATOR_TESTS="a2 b1" make test-emulator
#   make test-emulator-list                 # list selectable IDs
# EMULATOR_ALL_TESTS is the full selection whitelist. The default set is the
# always-green core that never writes the real HDD and never needs staged
# fixtures (a3's XDF boot restarts XEiJ and transiently edits ~/XEiJ.ini,
# restoring both afterwards). Opt in for the rest: c5..e2/f2/h4 stage or
# write A:\TEST and h5 restarts XEiJ and touches the mounted HDD, and c6
# is newly added. The h2 real-floppy regression runs on MPX68K via
# make test-mpx68k-h2 (tools/mpx68k-h2.sh).
EMULATOR_ALL_TESTS = a2 a3 b1 b2 b3 b4 c1 c2 c3 c4 c5 c6 \
	d1 d2 d3 d4 e1 e2 e3 f1 f2 g1 h3 h4 h5 h6
EMULATOR_CORE_TESTS = a2 a3 b1 b2 b3 b4 c1 c2 c3 c4 e3 f1 g1 h3 h6
EMULATOR_AUTOMATED_TESTS = $(EMULATOR_ALL_TESTS)
EMULATOR_TESTS ?= $(EMULATOR_CORE_TESTS)

.PHONY: test-emulator-core test-emulator-automated test-emulator-soak test-emulator-h1
test-emulator-core: EMULATOR_TESTS = $(EMULATOR_CORE_TESTS)
test-emulator-soak: EMULATOR_TESTS = h6
test-emulator-core test-emulator-soak: test-emulator
test-mpx68k-h2:
	tools/mpx68k-h2.sh
test-emulator-automated:
	python3 tools/xeij-suite.py --require-profile --mfp-check $(EMULATOR_AUTOMATED_TESTS)

test-emulator-h1:
	python3 tools/xeij-suite.py --mfp-check a2

test-emulator-benchmark:
	$(MAKE) test-emulator-benchmark-short
	$(MAKE) test-emulator-benchmark-full

test-emulator-benchmark-short:
	python3 tools/xeij-benchmark.py run --suite short --iterations $(EMULATOR_SHORT_REPEATS) \
		--results $(EMULATOR_BENCHMARK_DIR)/short/candidate --require-profile $(EMULATOR_CORE_TESTS)

test-emulator-benchmark-full:
	python3 tools/xeij-benchmark.py run --suite full --iterations $(EMULATOR_FULL_REPEATS) \
		--results $(EMULATOR_BENCHMARK_DIR)/full/candidate --require-profile $(EMULATOR_AUTOMATED_TESTS)

test-emulator-benchmark-compare-short:
	python3 tools/xeij-benchmark.py compare --iterations $(EMULATOR_SHORT_REPEATS) \
		--baseline $(BASELINE_SHORT) --candidate $(CANDIDATE_SHORT) \
		--output $(EMULATOR_BENCHMARK_DIR)/short/comparison.json

test-emulator-benchmark-compare-full:
	python3 tools/xeij-benchmark.py compare --iterations $(EMULATOR_FULL_REPEATS) \
		--baseline $(BASELINE_FULL) --candidate $(CANDIDATE_FULL) \
		--output $(EMULATOR_BENCHMARK_DIR)/full/comparison.json

.PHONY: test-emulator-list $(addprefix test-emulator-,$(EMULATOR_ALL_TESTS))
test-emulator-list:
	@echo "selectable XEiJ integration tests (EMULATOR_TESTS):"
	@echo "  a2  repeated XEiJ startup/exit cycles with MFP mask check"
	@echo "  a3  XDF IPL boot and PID 1 exit rejection (restarts XEiJ twice)"
	@echo "  b1  Timer D tick rate measurement (200 Hz nominal)"
	@echo "  b2  trace shows scheduler events during run counter"
	@echo "  b3  real-interrupt preemption keeps input responsive"
	@echo "  b4  real-key Ctrl-C kill and arena baseline restore"
	@echo "  c1  help output matches host expectation"
	@echo "  c2  redraw of output beyond 25 rows"
	@echo "  c3  mux F1-F3 switching with per-screen retention"
	@echo "  c4  F1/F2/F3 mux window switching and output retention"
	@echo "  c5  console module key reporting (stages CONSOLE.S9M in A:\\TEST)"
	@echo "  c6  BS/cursor/DEL line editing via real IOCS keys"
	@echo "  d1  create/read/replace on Human68k-formatted SCSI HDD"
	@echo "  d2  editor save and reopen on target HDD"
	@echo "  d3  on-target asm output byte-compared with host build"
	@echo "  d4  ld + run disk fallback with golden comparison"
	@echo "  e1  mux three-screen edit -> asm -> ld -> run flow"
	@echo "  e2  linker negative case (undefined symbol rejection)"
	@echo "  e3  builtin hello launch with trace evidence"
	@echo "  f1  background builtins via kernel-entry children + reclaim"
	@echo "  f2  background output survives screen switching"
	@echo "  g1  spawn/kill/wait cycles restore arena baseline"
	@echo "  h3  Ctrl-C delivery only to the active mux window"
	@echo "  h4  sread empty-file OK and >256-byte rejection"
	@echo "  h5  XDF IPL boot + smount/sread + PID 1 rejection + ini restore"
	@echo "  h6  repeated spawn cycles fully restore arena"
	@echo 'usage: make test-emulator [EMULATOR_TESTS="<ids>"] (see list above)'

# One convenience target per test. The test-emulator loop calls the script
# directly; these exist for single-test runs and carry the install dep.
define EMULATOR_TEST_RULE
test-emulator-$(1): install
	tools/emulator-itest.sh $(1)
endef
$(foreach t,$(EMULATOR_ALL_TESTS),$(eval $(call EMULATOR_TEST_RULE,$(t))))

build/xeij/mfpstate.x: tests/xeij/mfp_snapshot.c
	mkdir -p $(@D)
	m68k-xelf-gcc -m68000 -O2 -Wall -Wextra -Werror $< -lx68kiocs -o $@

test-emulator: check-emulator-tests install
	python3 tools/xeij-suite.py $(EMULATOR_TESTS)

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
