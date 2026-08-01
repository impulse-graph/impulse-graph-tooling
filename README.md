# Impulse Graph Tooling (`impulse-graph-tooling`)

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

Developer utilities, snapshot compilers, and heavy offline optimizer suite for **Impulse Graph Engine**.

## Tools Included

1. **`impulse-opt`**: Heavy offline snapshot layout optimizer:
   - Reverse Cuthill-McKee (RCM) bandwidth reduction & cache-line reordering.
   - SIMDCOMP / Roaring Bitmap PFOR vector compression.
   - GPU Sliced ELLPACK matrix encoding.
   - Section stripping, metadata injection, and header validation.

2. **`impulse-inspect`**: CLI header, section directory, and domain catalog inspector for binary snapshot files (`*.imps`).

3. **`impulse-compile`**: High-performance streaming snapshot compiler for edge-list CSV/TSV format conversion.

## Build Instructions

```bash
cmake -B build && cmake --build build
```

## Docker Container

```bash
docker build -t ghcr.io/impulse-graph/impulse-tools:latest .
```

## License

Apache License 2.0. See [LICENSE](LICENSE) for details.
