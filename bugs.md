# Impulse Graph Tooling (`impulse-graph-tooling`) Bug Tracker

This document records known bugs, architectural defects, root cause analyses, proposed fixes, and mandatory test cases required to prevent regressions across `impulse-graph-tooling`.

---

## BUG-TOOL-001: Duplicate Domain Catalog Writing in `impulse compile`

* **Severity**: High
* **Component**: `src/commands/compile.rs`
* **Affected Versions**: v2.4.0

### Description & Symptoms
When compiling a binary snapshot (`.imps`) using `impulse compile --manifest <manifest.json> --output <output.imps>`, the binary header domain catalog contains duplicate domain entries proportional to the number of relations. For example, compiling a graph with 3 domains and 2 relations produces 6 domain catalog entries in `impulse inspect` output instead of 3.

### Root Cause Analysis
In `src/commands/compile.rs`, the domain writing block is located inside the relation processing loop:

```rust
// BUG: Inside relation loop
for r in &manifest.relations {
    // ...
    for d in &manifest.domains {
        let count = domain_node_maps.get(&d.id).map(|m| m.len() as u64).unwrap_or(0);
        writer.add_domain(d.id, parse_key_type(&d.key_type), &d.name, count);
    }
    // ...
}
```

Because `writer.add_domain()` is called inside `for r in &manifest.relations`, all domains are added to `SnapshotWriter` $R$ times (where $R$ is the number of relations).

### Specification Mandate (Impulse-Graph C-ABI Spec v2.5 / v2.4 Section 2 Part A)
Per the formal specification (`impulse-graph-spec/docs/FORMAT_SPECIFICATION.md`):
1. **`DomainCount` (Header bytes `0x0A..0x0B`)**: Formally defined as the total count of distinct, unique node type domains in the graph schema (e.g. `USER`, `GROUP`, `ROLE`).
2. **Catalog Uniqueness Invariant**: Section 2 Part A (Domain Catalog) contains exactly `DomainCount` sequential records. Duplicate `DomainID`s or duplicate domain `Name`s violate the 1-to-1 mapping invariant of the catalog.
3. **Compiler Mandate**: Compilers (`impulse compile`) and writer implementations (`SnapshotWriter`) **MUST NOT** write duplicate domain catalog entries. `SnapshotWriter::add_domain()` should deduplicate or reject duplicate domain registrations.

### Proposed Fix
Move domain metadata initialization out of the relation loop in `compile.rs`. First perform a pass over all edge files to populate `domain_node_maps`, write the domain metadata section once, and then write relation CSR matrices:

```rust
// FIX: Process domain metadata ONCE prior to writing relations
for d in &manifest.domains {
    let count = domain_node_maps.get(&d.id).map(|m| m.len() as u64).unwrap_or(0);
    writer.add_domain(d.id, parse_key_type(&d.key_type), &d.name, count);
}
```

In addition, update `SnapshotWriter::add_domain` in `impulse-graph-core/impulse-rust/src/writer.rs` to disallow or ignore duplicate domain IDs/names if `add_domain` is called multiple times for an existing domain ID.

### Mandatory Test Case (Regression Prevention)
Add an integration test `test_compile_multi_relation_domain_count()` in `tests/compile_test.rs`:
1. Construct a manifest with 3 domains (`USER`, `GROUP`, `ROLE`) and 2 relations (`userToGroup`, `groupToRole`).
2. Run `impulse_graph_tooling::commands::compile::run(&manifest_path, &output_path)`.
3. Inspect the header of the compiled `.imps` snapshot and assert `loaded.domainCount == 3`.
4. Verify that calling `writer.add_domain()` with an already registered domain ID/name is either deduplicated or returns an explicit error.

---

## BUG-TOOL-002: Missing `--snapshot` / `-s` Flag Alias in `impulse inspect` CLI

* **Severity**: Medium
* **Component**: CLI Argument Parser (`src/main.rs`, `src/commands/inspect.rs`)
* **Affected Versions**: v2.4.0

### Description & Symptoms
Running `impulse inspect --snapshot path/to/snapshot.imps` fails with:
`error: unexpected argument '--snapshot' found`.

### Root Cause Analysis
The Clap command definition for `inspect` defines `<SNAPSHOT>` as a positional parameter, but does not define `--snapshot` or `-s` as an option flag alias.

### Proposed Fix
Annotate the `snapshot` field in `src/main.rs` with `#[arg(short, long)]` so both positional arguments (`impulse inspect snapshot.imps`) and option flags (`impulse inspect --snapshot snapshot.imps`) are accepted.

### Mandatory Test Case (Regression Prevention)
Add a CLI integration test in `tests/cli_test.rs` asserting that `impulse inspect --snapshot <path>` returns exit code `0`.

---

## BUG-TOOL-003: Duplicate Relation Definitions Validation in Compiler & Validator

* **Severity**: High
* **Component**: `src/commands/compile.rs`, `src/commands/validate.rs`
* **Affected Versions**: v2.4.0

### Description & Symptoms
Manifests containing duplicate relation declarations between the same `(src_domain, tgt_domain)` pair were compiled without error, resulting in duplicate relation descriptors in Section 2 Part B of the snapshot.

### Root Cause Analysis
Neither `impulse compile` nor `impulse validate` checked `(src_domain, tgt_domain)` tuple uniqueness when iterating over relations.

### Proposed Fix
1. In `compile.rs`: Validate that `(src_domain, tgt_domain)` pairs are unique across `manifest.relations` prior to processing edge files. Reject with `Err("Duplicate relation definition in manifest...")` if a duplicate is found.
2. In `validate.rs`: Check `reader.relations()` and reject snapshots containing duplicate relation descriptors for the same `(src_domain_id, tgt_domain_id)` pair.

### Mandatory Test Case (Regression Prevention)
Add integration test `test_compile_duplicate_relation_rejection()` in `tests/cli_tests.rs` asserting that compiling a manifest with duplicate `(src_domain, tgt_domain)` entries fails with an explicit diagnostic error.
