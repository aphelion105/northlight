# Windows installer 0.3.217-test.20261104.3

This replaces [20261104.2](complete-installer-20261104.2.md). It retains the complete
renderer, extraction/art-layer pipeline, calibrated equipment lighting and all
earlier optimizations, including the bundled StormLib ownership correction.

The release adds [city-lamp shadow fixes](lamp-shadow-fix-20261104.md): native
Dalaran fixture qualification and exact live luminous-layer ownership, bounded
orc flame origins, and Stormwind daylight shadow composition. Source energy,
distance/shadow budgets and filtering quality are preserved. Actual-client
offline tests cover weighted character movement, articulation, independent
obstacles, metal retention and exact actor-off/removal behavior.

[Explicit cache revisions](cache-generator-versions.md) replace source hashes as
the compatibility contract. Unversioned caches upgrade once. Later DLL builds
do not trigger cache regeneration. Compatible installed prebuilt variants remain
usable even if a later installer contains no matching prebuilt archive.

The package contains all 48 current application files and the production x86
DLL. Client models, captures and generated client caches are not distributed.
The installer suite has 257 tests, with six existing skips and no failures.
The real four-tile cache integration creates 451 files, including the NLF1-v2
native fixture catalog. A second run reuses the cache and creates a byte-identical
37,970,233-byte cache ZIP.

This is a complete installer, not an update applied to the local game during
validation. `Wow.exe` and the installed game archives are read only.
