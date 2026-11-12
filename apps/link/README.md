# ld — SSOS-9 linker

Phase 4 linker: links S9O1 object files (produced by `apps/asm`) into an
S9M1 module image loadable by the existing validator and spawn path.
The contract is `docs/linker-contract.md`.

## Usage (on target)

```text
run ld OUTPUT IN1 [IN2 ...]
```

`OUTPUT` is the S9M1 file to write (module name comes from its base);
`IN1..` are S9O1 objects, 1 to 14, linked in input order. The first
input's text[0] is the entry. On success:

```text
Wrote OUTPUT: N bytes, text T, data D, bss B, fixups F
```

Errors stop at the first problem and print `ld: message` or
`FILE: message` (exit 1; usage errors exit 2). The output file is only
written once the whole image is complete (`ss9_file_store`), so no
partial output can exist.

## Files

- `link.h` — limits, structures, statuses, API.
- `readobj.c` — sequential S9O1 input with format validation (no seek;
  file length checked by exact reads plus an EOF probe).
- `layout.c` — pass 1: symbol table, name pool, section bases,
  duplicate/undefined globals, capacity limits.
- `fold.c` — pass 2: section copy, relocation application (PC16 fold,
  A5 displacements, constants), fixup generation, PIC diagnostics.
- `image.c` — output image geometry, S9M1 header, CRC32, module name
  derivation, diagnostic text.
- `main.c` — target CLI (ss9_* syscalls). Host driver: `host_main.c`.
- `support.c` — memcpy and 32-bit mul/div (libgcc replacement, same as
  `apps/asm/support.c`; target build only).

All state lives on the stack (the S9M1 code-only profile forbids static
data); the module is built with a 65532-byte stack, within the contract
budget (~62 KB).

## Build

```sh
make -C apps/link        # build/ld.s9m + build/ld.xdf (needs cross cc)
make -C apps/link host   # build/link-host, host CLI for verification
```

`.text` must stay at or below 32768 bytes; `mkmodule.py` checks it.
