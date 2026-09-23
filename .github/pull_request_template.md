## What this changes

<!-- and why -->

## How it was verified

<!--
Which tests, on which platform. For anything touching the CPU cores, the
runtime or the renderer, a lockstep run is what makes the change reviewable:

    ctest --output-on-failure
    ctest -L long          # if src/cpu/*/*_ops.h or the renderer changed
-->

- [ ] `ctest --output-on-failure` passes with a ROM configured
- [ ] Builds with no new warnings
- [ ] No golden hashes changed, or the change is explained above
- [ ] No ROM data, generated sources or memory dumps are included
