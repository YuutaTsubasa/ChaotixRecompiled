# generated/

Output of `chaotix_recomp`. Everything in this directory (except this file)
is derived from your ROM and is ignored by git. Do not commit or redistribute it.

Contents after a build with `-DCHAOTIX_ROM=...`:

- `recomp_m68k_000.cpp` … `recomp_m68k_015.cpp` — 68000 functions
- `recomp_sh2_000.cpp` … `recomp_sh2_007.cpp` — SH-2 functions (both CPUs)
- `recomp_tables.cpp` — dispatch tables and RAM-code validation ranges
- `recomp_decls.h` — declarations
- `manifest.txt` — ROM hash and statistics
