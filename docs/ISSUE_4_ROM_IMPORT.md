# Issue #4: verified ROM installation and startup

The Windows 0.3.4 frontend can install an unknown 32X image and keep using it.
An Explorer drop onto the executable overrides that copy for one launch only.

## Accepted repair

- Require the known Japan/USA SHA-1 before replacing the installed copy.
- Display the selected path, actual SHA-1 and expected SHA-1 on rejection.
- Return legacy invalid installs to setup, with Browse available.
- Install a verified ROM passed to the executable and remember the installed copy.
- Keep reference-interpreter behavior in the headless research tool unchanged.
- Keep the user's saved game data unchanged.

## Implementation and validation plan

- [x] Add and run failing install and startup regression tests (2 unit cases and all 5 startup cases failed on the original code).
- [x] Enforce verification in setup; update nonseekable stream tests for rejection.
- [x] Unify frontend startup persistence and expose recoverable setup diagnostics.
- [x] Build Windows; run unit, ROM startup, and existing non-long CTest checks.
- [x] Independent code review completed; corrected missing hashes for invalid headers. Windows UI recovery was subsequently verified through the native Browse dialog and restart.

The unit tests use synthetic data; successful import and startup tests use only
an explicitly provided local ROM in temporary user-data directories.

## Verification

- Windows Release build completed without compiler warnings.
- Unit and nonseekable SDL stream tests pass, including an optional local-ROM successful import.
- All five Python startup regressions failed before the repair and pass after it.
- Readable invalid-header diagnostics regression also failed before its correction and passes after it.
- All 14 non-long CTest entries pass.
- Local package at out/issue-4-windows passes all five startup tests with a system-only PATH.
- Tests use temporary stores; no personal ROM installation, settings or saved progress was changed.
- Release 0.3.5 builds on Windows x64 and Android arm64-v8a/x86_64; Android hardware remains unverified.

## Windows UI verification (2026-09-28)

Using the local out/issue-4-windows package and an isolated temporary store:

1. Seeded a legacy unknown ROM with matching saved hash; startup displayed its path and actual/expected SHA-1.
2. Dismissed the error; setup remained available and displayed the verified candidate.
3. Clicked Browse and selected a verified local test ROM in the native Windows file picker.
4. Confirmed the main menu appeared and the installed file/configuration contained the expected SHA-1.
5. Closed the app normally, removed only the temporary source ROM, and relaunched with no ROM argument.
6. Confirmed the main menu appeared directly without the error or setup screen; the log reported static recompilation.

Both UI test sessions were closed. Personal settings and saves were not used.

## Release 0.3.5

The Windows ZIP passes all five startup tests with a system-only PATH. The Android
APK has version code 8 and the same signing certificate as 0.3.4, preserving
in-place updates. It follows the existing debug-signed APK / Release native-code
packaging. Android build warnings are the existing unused kNotATap constant and
SDK XML tool-version mismatch; both architectures build successfully.
