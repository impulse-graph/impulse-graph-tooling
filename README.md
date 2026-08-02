# Impulse Graph Tooling (`impulse-graph-tooling`)

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

Unified Rust developer utility and layout optimizer CLI suite for **Impulse Graph Engine**.

- **Canonical Executable**: `impulse-graph`
- **Symlink / Alias**: `impulse`

## Subcommands Included

1. **`impulse inspect`**: Inspect snapshot header, version, global/relation feature bitmaps, domain catalogs, and topology metadata.
2. **`impulse validate`**: Verify Spec v2.4 normative requirements, 128-byte hardware alignment, CSR offset monotonicity, and target index bounds.
3. **`impulse compile`**: High-performance streaming compiler from CSV/TSV/JSON-L edge lists to zero-copy `.imps` binary snapshots.
4. **`impulse optimize`**: Heavy offline snapshot optimizer: Reverse Cuthill-McKee (RCM) bandwidth reduction, vector encoding conversions (`RAW_UINT32`, `DELTA_VBYTE`, `SIMDCOMP`, `SLICED_ELLPACK`), and section stripping.
5. **`impulse keygen` / `sign` / `verify`**: Ed25519 cryptographic key generation, snapshot signing, and signature verification.
6. **`impulse diff`**: Structural snapshot topology and domain schema comparison between base and target `.imps` files.
7. **`impulse export`**: Dump binary `.imps` snapshot files back to CSV, TSV, or JSON-L edge format.

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
