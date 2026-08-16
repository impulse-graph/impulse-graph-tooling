# Impulse Graph Tooling (`impulse-graph-tooling`)

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

Unified developer utilities, DSL compiler suite, layout optimizer, synthetic graph data generator, and execution runtime for the **Impulse Graph Engine** (Spec v0.9.0).

- **Canonical Binary**: `impulse-graph`
- **Symlink / CLI Alias**: `impulse`

---

## Table of Contents

- [Quick Start](#quick-start)
- [Graph Data Generator](#graph-data-generator)
  - [Supported Topology Profiles](#supported-topology-profiles)
  - [Generator CLI Examples](#generator-cli-examples)
  - [Synthetic Edge Attributes](#synthetic-edge-attributes)
- [CLI Command Reference](#cli-command-reference)
  - [Snapshot Operations](#snapshot-operations)
  - [Compiler & VM Operations](#compiler--vm-operations)
  - [Cryptographic Verification](#cryptographic-verification)
- [DSL Compiler Suite & Pipeline](#dsl-compiler-suite--pipeline)
- [Building & Installation](#building--installation)
- [Docker Container](#docker-container)
- [License](#license)

---

## Quick Start

### 1. Build the Tooling CLI

```bash
cargo build --release
export PATH="$PWD/target/release:$PATH"
```

### 2. Generate a Test Snapshot

Generate a Graph500 (R-MAT) benchmark graph with $2^{12} = 4,096$ vertices, edge factor $16$ ($65,536$ edges), and synthetic float32 weights directly into an immutable `.imps` binary snapshot:

```bash
impulse-graph generate \
  --profile graph500 \
  --scale 12 \
  --edge-factor 16 \
  --attributes "weight:f32,timestamp:i64" \
  -o graph500_s12.imps
```

### 3. Inspect Snapshot Topology & Schema

Inspect binary header metadata, domain catalogs, and CSR relation tables:

```bash
impulse-graph inspect graph500_s12.imps
```

### 4. Compute Topology, Multiplicity & CBO Statistics

Compute degree percentiles, supernodes, multiplicity classification, attribute zone maps, and CBO sketches:

```bash
impulse-graph stats graph500_s12.imps
```

### 5. Validate Spec v0.9.0 Compliance

Verify header checksums, 128-byte hardware memory alignment, and CSR array bounds:

```bash
impulse-graph snapshot validate graph500_s12.imps
```

### 6. Optimize Snapshot Layout

Apply Reverse Cuthill-McKee (RCM) bandwidth reduction and convert CSR column indices to Delta-VByte encoding for optimal L1/L2 cache locality:

```bash
impulse-graph snapshot optimize \
  --input graph500_s12.imps \
  --output graph500_s12_rcm.imps \
  --rcm \
  --encoding delta_vbyte
```

---

## Graph Data Generator

The `generate` subcommand synthesizes zero-copy `.imps` binary snapshots, TSV edge lists, or CSV files across multiple realistic and benchmark topology profiles.

```bash
impulse-graph generate --profile <PROFILE> [OPTIONS] -o <OUTPUT>
```

### Supported Topology Profiles

| Profile Name | Model / Algorithm | Primary Use Case | Key CLI Parameters |
| :--- | :--- | :--- | :--- |
| **`graph500`** (or `rmat`, `kronecker`) | Kronecker / R-MAT recursive quadrant power law | Graph500 benchmark traversals (BFS, SSSP, PageRank) | `-s, --scale <U8>` ($N = 2^S$ vertices)<br>`-e, --edge-factor <U32>` (edges/vertex, default `16`)<br>`--a`, `--b`, `--c`, `--d` (initiator probabilities) |
| **`social`** (or `barabasi`) | Barabási–Albert preferential attachment | Scale-free social follow networks ($P(k) \sim k^{-3}$) | `-n, --nodes <U64>` (total vertices)<br>`--edges-per-node <U32>` (attachment degree, default `8`) |
| **`social-sbm`** (or `communities`) | Stochastic Block Model (SBM) | Clustered social circles & community structure | `-n, --nodes <U64>`<br>`-k, --communities <U32>` (cluster count)<br>`--p-intra <F64>` (intra-cluster probability)<br>`--p-inter <F64>` (inter-cluster probability) |
| **`erdos-renyi`** (or `random`) | Erdős–Rényi uniform random $G(N, M)$ / $G(N, p)$ | Baseline random graph verification | `-n, --nodes <U64>`<br>`-m, --edges <U64>` (or `--p <F64>`) |
| **`grid`** (or `mesh`, `lattice`) | 2D / 3D regular spatial mesh | Power grids, PDE meshes, spatial networks | `--dim-x <U32>`, `--dim-y <U32>`, `--dim-z <U32>`<br>`--toroidal` (periodic boundary wrap-around) |
| **`tree`** (or `hierarchy`) | Balanced $K$-ary tree or star topology | Hierarchies, DAG reachability, ReBAC taxonomies | `--branching <U32>` (branching factor $K$)<br>`--depth <U32>` (tree depth $D$)<br>`--star` (single hub star) |
| **`bipartite`** | Multi-domain 2-mode graph | Multi-domain relations (Users $\rightarrow$ Documents) | `--src-nodes <U64>`, `--tgt-nodes <U64>`, `-m, --edges <U64>` |

### Generator CLI Examples

#### Generate a Social Network with Community Clusters
```bash
impulse-graph generate \
  --profile social-sbm \
  --nodes 50000 \
  --communities 20 \
  --p-intra 0.01 \
  --p-inter 0.0001 \
  --domain-name "User" \
  --relation-name "FRIENDS_WITH" \
  -o social_sbm.imps
```

#### Generate a Multi-Domain Bipartite Graph for Authorization Testing
```bash
impulse-graph generate \
  --profile bipartite \
  --src-nodes 10000 \
  --tgt-nodes 50000 \
  --edges 200000 \
  --domain-name "User" \
  --tgt-domain-name "Document" \
  --relation-name "CAN_ACCESS" \
  -o rebac_bipartite.imps
```

#### Export to TSV / CSV Edge Lists
```bash
impulse-graph generate \
  --profile graph500 \
  --scale 10 \
  --format tsv \
  --attributes "weight:f32" \
  -o graph500_edges.tsv
```

### Synthetic Edge Attributes

Attach synthetic Structure-of-Arrays (SoA) attribute buffers to generated relations using `--attributes`:

```bash
--attributes "weight:f32,timestamp:i64,type:i32"
```

Supported attribute types:
- `f32` / `float`: 32-bit floating point weights
- `f64` / `double`: 64-bit floating point weights
- `i32` / `int`: 32-bit signed integers (e.g. categorical relation types)
- `i64` / `timestamp`: 64-bit signed integers (e.g. UNIX timestamps)

---

## CLI Command Reference

### Snapshot Operations

* **`impulse generate`**: Synthesize `.imps` snapshots, TSV, or CSV edge lists across 7 graph topology profiles.
* **`impulse inspect <SNAPSHOT>`**: Output human-readable or JSON details of Page 0 header, section offsets, domain catalogs, and topology metadata.
* **`impulse stats <SNAPSHOT>`**: Compute structural graph statistics, in/out degree percentiles ($P_{50}, P_{90}, P_{99}$), supernode hub classification, relation multiplicity ($1:1, M:1, 1:M, M:M$), attribute zone maps, and Cost-Based Optimizer (CBO) sketches.
* **`impulse snapshot validate <SNAPSHOT>`**: Verify 128-byte hardware alignment, monotonic CSR row offsets, column index range bounds, and metadata integrity.
* **`impulse snapshot optimize <OPTIONS>`**: Offline topology optimizer:
  - `--rcm`: Apply Reverse Cuthill-McKee vertex renumbering to reduce matrix bandwidth.
  - `--degree-sort`: Sort nodes by descending degree for L1/L2 cache locality on hub nodes.
  - `--encoding <raw_uint32|delta_vbyte|simdcomp|sliced_ellpack>`: Compress CSR column indices.
  - `--csc`: Generate reverse Compressed Sparse Column auxiliary index sections.
* **`impulse snapshot diff <BASE> <TARGET>`**: Compare schema, domain catalogs, and structural edge diffs between two `.imps` snapshots.
* **`impulse snapshot export <SNAPSHOT> --out-dir <DIR> --format <tsv|csv|jsonl>`**: Export snapshot relations back to edge list files.
* **`impulse snapshot merge --base <BASE> --deltas <DELTAS...> --output <OUT>`**: Ingest WAL delta log files (`.impdelta`) to produce an updated snapshot.
* **`impulse snapshot convert-tensors --input <MODEL.safetensors> --output <OUT.imps>`**: Ingest raw weight tensors from HuggingFace `.safetensors` files directly into `.imps` property blocks.

### Compiler & VM Operations

* **`impulse compile <INPUT> [-o <OUTPUT>]`**: Compile high-level DSL sources (`.impk`, `.implog`, `.impscm`) into `.impas` assembly or `.impb` binary bytecode.
* **`impulse assemble -i <INPUT.impas> -o <OUTPUT.impb>`**: Assemble textual assembly into binary opcode bytecode.
* **`impulse disassemble -i <INPUT.impb>`**: Disassemble binary bytecode into annotated assembly text.
* **`impulse run --snapshot <SNAPSHOT.imps> --bytecode <QUERY.impb> --input-val <ROOT_ID>`**: Execute compiled query bytecode directly against a memory-mapped binary snapshot.

### Cryptographic Verification

* **`impulse crypto keygen --out <KEY_PREFIX>`**: Generate Ed25519 public/private keypair (`.pub`, `.priv`).
* **`impulse crypto sign <SNAPSHOT.imps> --key <KEY.priv>`**: Compute SHA-256 digest over snapshot data sections and produce an Ed25519 signature (`.sig`).
* **`impulse crypto verify <SNAPSHOT.imps> --key <KEY.pub>`**: Verify cryptographic authenticity of a signed snapshot file.

---

## DSL Compiler Suite & Pipeline

The compiler translates frontends into `ImpulseVM` bytecode (`impOps`):

```
+--------------------+   +--------------------+
|  ImpK (.impk)      |   |  ImpLog (.implog)  |
|  Matrix & Vector   |   |  Datalog & ReBAC   |
+---------+----------+   +---------+----------+
          |                        |
          +----------+  +----------+
                     |  |
                     v  v
        +----------------------------+
        |  ImpScheme (.impscm)       |
        |  Universal S-Expr AST IR   |
        +-------------+--------------+
                      |
                      v
        +----------------------------+
        |  ImpAsm (.impas)           |
        |  Text Assembly Format      |
        +-------------+--------------+
                      |
                      v
        +----------------------------+
        |  impOps (.impb)            |
        |  VM Bytecode Binary ISA    |
        +----------------------------+
```

* **`ImpK` (`.impk`)**: Matrix math, PageRank, connected components, SIMD vector math.
* **`ImpLog` (`.implog`)**: Declarative Datalog rules, Relationship-Based Access Control (ReBAC / Zanzibar), transitive reachability.
* **`ImpScheme` (`.impscm`)**: Homoiconic S-Expression AST compiler Intermediate Representation (IR).
* **`ImpAsm` (`.impas`)**: Register-based assembly format (`R0`..`R63`).
* **`impOps` (`.impb`)**: Bytecode instruction set architecture (opcodes `0x01`..`0x6A`).

---

## Building & Installation

### Prerequisites
- Rust 1.75+ (Cargo)
- CMake 3.20+ (for optional C-ABI tooling integration)

### Build Steps

```bash
# Debug build
cargo build

# Release build with optimizations
cargo build --release

# Run comprehensive test suite
cargo test
```

---

## Docker Container

```bash
# Build the Docker image
docker build -t ghcr.io/impulse-graph/impulse-tools:latest .

# Run the CLI container
docker run --rm -v "$PWD:/data" ghcr.io/impulse-graph/impulse-tools:latest inspect /data/snapshot.imps
```

---

## License

Apache License 2.0. See [LICENSE](LICENSE) for details.
