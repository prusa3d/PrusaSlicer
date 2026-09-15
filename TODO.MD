# Orca profile branch follow-up

- [x] Transplant the non-build source and test changes as individual commits,
  preserving original authors and coauthor trailers.
- [x] Exclude local build tooling, compiler/linker changes, deployment records
  and machine-specific diagnostic documents.
- [x] Verify that no 3MFs are introduced or modified by the transferred commits.
  Document the provenance of the seven existing upstream regression fixtures.
- [x] Verify that normal builds, profile loading and importer tests do not depend
  on an Orca source checkout or executable. Optional maintenance tools take
  explicit caller-supplied paths.
- [x] Compile and run the standalone importer tests from this branch; run the
  three Python provenance regressions.

Current implementation, portable test commands and asset provenance are in
[doc/OrcaProfiles.md](doc/OrcaProfiles.md). Full application compilation was not
repeated on this branch; source/test equality with the tested source branch was
checked. No open transplant tasks remain.
