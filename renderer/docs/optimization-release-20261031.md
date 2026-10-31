# Validated optimization test installer

Version: 0.3.216-test.20261031.1. The batch contains 31 new validated changes; the original target of 60 new changes is not reached. This release publishes the completed set rather than padding the count with tests or earlier features.

- ZIP bytes: 19618804
- ZIP SHA256: bdbbba4ca9c80b97dec147aa65e0dfa14d47c687b5965bc488ec4e5f2a5ad05f
- DLL SHA256: 71906f51e039cc19ad3b90331f3e1d201042dbe426e94faa7a126e928e0e2041
- Embedded production identity: 5e2e721cd61fbe06dc9d8885617122d55efd3a376e291ded37904ed0b756a7d0

The installer payload passes verify_packages.py. It includes the existing settings and opt-in HDR implementation together with the validated optimization batch; see settings-hdr-integration-20261031.md for review ownership, test matrices and the five controls retained only in the combined PR. The new in-progress HDR pipeline is separate.

No game cache is packaged or used for validation. Existing configuration files and the generating cache must remain preserved. CPU functional acceptance is 233 PASS, zero failures/timeouts, 169 explicit omissions. Windows GPU acceptance is reported separately in the compatibility note. The one discovered legacy implicit CPU benchmark is excluded from performance evidence and now requires an explicit flag.
