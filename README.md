# Impulse Graph Tooling (`impulse-graph-tooling`)

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

Unified Rust developer utility and layout optimizer CLI suite for **Impulse Graph Engine**.

- **Canonical Executable**: `impulse-graph`
- **Symlink / Alias**: `impulse`

## Subcommands Included

1. **`impulse inspect`**: Inspect snapshot header, version, global/relation feature bitmaps, domain catalogs, and topology metadata.
2. **`impulse validate`**: Verify Spec v0.9.0 normative requirements, 128-byte hardware alignment, CSR offset monotonicity, and target index bounds.
3. **`impulse compile`**: Multi-frontend DSL compiler (compiling `.impk`, `.implog`, and `.impscm` sources into `.impas` assembly or `.impb` bytecode) and snapshot builder from edge lists into `.imps` files.
4. **`impulse optimize`**: Heavy offline snapshot optimizer: Reverse Cuthill-McKee (RCM) bandwidth reduction, vector encoding conversions (`RAW_UINT32`, `DELTA_VBYTE`, `SIMDCOMP`, `SLICED_ELLPACK`), and section stripping.
5. **`impulse assemble` / `disassemble`**: Assemble `ImpAsm` (`.impas`) text into binary opcode bytecode (`.impb`) or disassemble binary bytecode back to text assembly.
6. **`impulse keygen` / `sign` / `verify`**: Ed25519 cryptographic key generation, snapshot signing, and signature verification.
7. **`impulse diff`**: Structural snapshot topology and domain schema comparison between base and target `.imps` files.
8. **`impulse export`**: Dump binary `.imps` snapshot files back to CSV, TSV, or JSON-L edge format.

## DSL Compiler Suite & Multi-Frontend Positioning

The compiler pipeline translates high-level domain languages down to **`ImpulseVM` bytecode (`impOps`)**:

* **`ImpK` (`.impk`)**: Primary user frontend for GraphBLAS matrix mathematics, PageRank, connected components, and SIMD vector operations.
* **`ImpLog` (`.implog`)**: Primary user frontend for declarative Datalog logic rules, ReBAC authorizations (Zanzibar model), and transitive reachability.
* **`ImpScheme` (`.impscm`)**: Universal Compiler Intermediate Representation (IR) & Low-Level Macro Extension Target. Homoiconic S-Expression AST compiler bus between frontends (`ImpK`, `ImpLog`) and the `impOps` bytecode emitter. Can be authored directly for advanced macro metaprogramming or AST pass debugging.
* **`ImpAsm` (`.impas`)**: Human-readable text assembly representation of `ImpulseVM` registers (`R0`..`R63`).
* **`impOps` (`.impb`)**: Virtual Machine opcode instruction set architecture (opcodes `0x01`..`0x6A`). Part of core specification.


## Build Instructions

```bash
cargo build --release
```

The output binaries will be created at `target/release/impulse-graph` and symlinked to `target/release/impulse`.

## Docker Container

```bash
docker build -t ghcr.io/impulse-graph/impulse-tools:latest .
```

## License

Apache License 2.0. See [LICENSE](LICENSE) for details.
