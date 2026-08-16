use clap::{Parser, Subcommand};
use std::path::PathBuf;

pub mod commands;
pub use impulse_compiler as compiler;
pub mod parquet_reader;

#[derive(Parser)]
#[command(
    name = "impulse-graph",
    author = "Impulse Graph Engine Contributors",
    version = "0.9.0",
    about = "Impulse Graph Engine Developer Utilities & Layout Optimizer Suite (Spec v0.9.0)",
    long_about = "Official CLI utility for inspecting, validating, compiling, optimizing, signing, comparing, exporting, and executing impulse scripts (.impscm) and binary snapshot files (.imps)."
)]
pub struct Cli {
    #[command(subcommand)]
    pub command: Commands,
}

#[derive(Subcommand)]
pub enum Commands {
    /// Execute compiled bytecode query (.impb) or script against a binary snapshot (.imps)
    Run {
        /// Path to binary snapshot (.imps)
        #[arg(short, long, value_name = "SNAPSHOT")]
        snapshot: PathBuf,

        /// Path to compiled bytecode program (.impb) or assembly (.impas)
        #[arg(short, long, value_name = "BYTECODE")]
        bytecode: PathBuf,

        /// Integer input parameter (e.g. source root node ID)
        #[arg(short, long, default_value_t = 0)]
        input_val: u64,
    },

    /// Compile DSL source script (.impscm, .impk, .implog) into .impas assembly text or .impb bytecode (Code Only)
    Compile {
        /// Input script file (.impscm, .impk, .implog)
        #[arg(value_name = "INPUT")]
        input: PathBuf,

        /// Output assembly file (.impas) or bytecode file (.impb)
        #[arg(short, long, value_name = "OUTPUT")]
        output: Option<PathBuf>,

        /// Emit Intermediate Representation (S-Expressions) to stdout
        #[arg(long)]
        emit_ir: bool,
    },

    /// Build zero-copy binary snapshot (.imps) from dataset manifest (Snapshots Only)
    Build {
        /// Path to JSON compiler manifest file
        #[arg(short, long, value_name = "MANIFEST")]
        manifest: PathBuf,

        /// Output binary snapshot destination path (.imps)
        #[arg(short, long, value_name = "OUTPUT")]
        output: PathBuf,
    },

    /// Inspect binary snapshot header, section directory, domain catalogs, and topology metadata
    Inspect {
        /// Path to binary snapshot file (.imps)
        #[arg(value_name = "SNAPSHOT")]
        file: PathBuf,

        /// Output format (text, json)
        #[arg(short, long, default_value = "text")]
        format: String,

        /// Verbose detail dumping
        #[arg(short, long)]
        verbose: bool,
    },

    /// Assemble textual assembly (.impas) into binary bytecode program (.impb)
    Assemble {
        /// Path to input assembly file (.impas)
        #[arg(short, long, value_name = "INPUT")]
        input: PathBuf,

        /// Path to output binary file (.impb)
        #[arg(short, long, value_name = "OUTPUT")]
        output: PathBuf,
    },

    /// Disassemble binary bytecode program (.impb) into annotated text assembly
    Disassemble {
        /// Path to input binary file (.impb)
        #[arg(short, long, value_name = "INPUT")]
        input: PathBuf,
    },

    /// Synthesize synthetic graph datasets and .imps binary snapshots across multiple topology profiles
    Generate(GenerateArgs),

    /// Subcommand namespace for Compiler & Bytecode Toolchain (Code Only)
    Compiler {
        #[command(subcommand)]
        command: CompilerCommands,
    },

    /// Subcommand namespace for Binary Snapshot (.imps) Data Tooling (Snapshots Only)
    Snapshot {
        #[command(subcommand)]
        command: SnapshotCommands,
    },

    /// Subcommand namespace for Cryptographic Signatures & Verification
    Crypto {
        #[command(subcommand)]
        command: CryptoCommands,
    },
}

#[derive(Subcommand)]
pub enum CompilerCommands {
    /// Compile DSL source script (.impscm, .impk, .implog) into .impas assembly text or .impb bytecode (Code Only)
    Compile {
        /// Input script file (.impscm, .impk, .implog)
        #[arg(value_name = "INPUT")]
        input: PathBuf,

        /// Output assembly file (.impas) or bytecode file (.impb)
        #[arg(short, long, value_name = "OUTPUT")]
        output: Option<PathBuf>,

        /// Emit Intermediate Representation (S-Expressions) to stdout
        #[arg(long)]
        emit_ir: bool,
    },

    /// Alias for Compile: Compile DSL script (.impscm) into .impas assembly text (Code Only)
    Build {
        /// Input script file (.impscm, .impk, .implog)
        #[arg(value_name = "INPUT")]
        input: PathBuf,

        /// Output assembly file (.impas) or bytecode file (.impb)
        #[arg(short, long, value_name = "OUTPUT")]
        output: Option<PathBuf>,

        /// Emit Intermediate Representation (S-Expressions) to stdout
        #[arg(long)]
        emit_ir: bool,
    },

    /// Assemble textual assembly (.impas) into binary bytecode program (.impb)
    Assemble {
        /// Path to input assembly file (.impas)
        #[arg(short, long, value_name = "INPUT")]
        input: PathBuf,

        /// Path to output binary file (.impb)
        #[arg(short, long, value_name = "OUTPUT")]
        output: PathBuf,
    },

    /// Disassemble binary bytecode program (.impb) into annotated text assembly
    Disassemble {
        /// Path to input binary file (.impb)
        #[arg(short, long, value_name = "INPUT")]
        input: PathBuf,
    },

    /// Inspect binary bytecode program (.impb) headers and opcode distribution
    Inspect {
        /// Path to input binary bytecode file (.impb)
        #[arg(value_name = "BYTECODE")]
        input: PathBuf,
    },
}

#[derive(Subcommand)]
pub enum SnapshotCommands {
    /// Build zero-copy binary snapshot (.imps) from dataset manifest (Snapshots Only)
    Build {
        /// Path to JSON compiler manifest file
        #[arg(short, long, value_name = "MANIFEST")]
        manifest: PathBuf,

        /// Output binary snapshot destination path (.imps)
        #[arg(short, long, value_name = "OUTPUT")]
        output: PathBuf,
    },

    /// Build a new binary snapshot by integrating WAL delta log files
    Merge {
        /// Base binary snapshot file (.imps)
        #[arg(short, long, value_name = "BASE")]
        base: PathBuf,

        /// WAL delta log files to integrate (.impdelta)
        #[arg(short, long, value_name = "DELTAS", num_args = 1..)]
        deltas: Vec<PathBuf>,

        /// Output merged binary snapshot destination path (.imps)
        #[arg(short, long, value_name = "OUTPUT")]
        output: PathBuf,
    },

    /// Inspect binary snapshot header, section directory, domain catalogs, and topology metadata
    Inspect {
        /// Path to binary snapshot file (.imps)
        #[arg(value_name = "SNAPSHOT")]
        file: PathBuf,

        /// Output format (text, json)
        #[arg(short, long, default_value = "text")]
        format: String,

        /// Verbose detail dumping
        #[arg(short, long)]
        verbose: bool,
    },

    /// Validate binary snapshot file against Spec v0.9.0 normative requirements
    Validate {
        /// Path to binary snapshot file (.imps)
        #[arg(value_name = "SNAPSHOT")]
        file: PathBuf,

        /// Verify 128-byte hardware alignment rules strictly
        #[arg(long, default_value_t = true)]
        strict_alignment: bool,

        /// Optional public key file to verify Ed25519 signature
        #[arg(short, long)]
        public_key: Option<PathBuf>,
    },

    /// Heavy offline snapshot layout optimizer (RCM graph reordering, vector encodings, section stripping)
    Optimize {
        /// Input binary snapshot (.imps)
        #[arg(short, long, value_name = "INPUT")]
        input: PathBuf,

        /// Output optimized snapshot destination (.imps)
        #[arg(short, long, value_name = "OUTPUT")]
        output: PathBuf,

        /// Apply Reverse Cuthill-McKee (RCM) bandwidth reduction and cache-line reordering
        #[arg(long)]
        rcm: bool,

        /// Apply degree-descending node ID reordering for L1/L2 cache locality
        #[arg(long)]
        degree_sort: bool,

        /// Generate CSC (Compressed Sparse Column) auxiliary reverse topology sections
        #[arg(long)]
        csc: bool,

        /// Target CSR column encoding (raw_uint32, delta_vbyte, simdcomp, sliced_ellpack)
        #[arg(long)]
        encoding: Option<String>,

        /// Strip string ID mapping catalog for minimal runtime binary footprint
        #[arg(long)]
        strip_mappings: bool,

        /// Strip DTO property payload blocks
        #[arg(long)]
        strip_properties: bool,
    },

    /// Compare structural topology and schema differences between two binary snapshots
    Diff {
        /// Base binary snapshot file (.imps)
        #[arg(value_name = "BASE")]
        base: PathBuf,

        /// Target binary snapshot file (.imps)
        #[arg(value_name = "TARGET")]
        target: PathBuf,
    },

    /// Export binary snapshot file (.imps) back to CSV/TSV edge lists
    Export {
        /// Input binary snapshot file (.imps)
        #[arg(value_name = "SNAPSHOT")]
        file: PathBuf,

        /// Output directory for exported edge files
        #[arg(short, long, value_name = "OUTDIR")]
        out_dir: PathBuf,

        /// Export format (tsv, csv, jsonl)
        #[arg(short, long, default_value = "tsv")]
        format: String,
    },

    /// Ingest raw binary weight tensors from HuggingFace .safetensors files into .imps snapshot property blocks
    ConvertTensors {
        /// Path to input .safetensors file
        #[arg(short, long, value_name = "SAFETENSORS")]
        input: PathBuf,

        /// Destination binary snapshot path (.imps)
        #[arg(short, long, value_name = "OUTPUT")]
        output: PathBuf,

        /// Domain name for model weight nodes
        #[arg(short, long, default_value = "ModelWeightNode")]
        domain: String,
    },

    /// Synthesize synthetic graph datasets and .imps binary snapshots across multiple topology profiles
    Generate(GenerateArgs),
}

#[derive(Subcommand)]
pub enum CryptoCommands {
    /// Generate Ed25519 keypairs for snapshot signature verification
    Keygen {
        /// Destination directory or file path prefix for generated keypair
        #[arg(short, long, default_value = "impulse_key")]
        out: String,
    },

    /// Sign a binary snapshot using Ed25519 private key
    Sign {
        /// Target binary snapshot file (.imps)
        #[arg(value_name = "SNAPSHOT")]
        file: PathBuf,

        /// Path to Ed25519 private key file (hex/pem)
        #[arg(short, long)]
        key: PathBuf,
    },

    /// Verify Ed25519 signature on a binary snapshot
    Verify {
        /// Target binary snapshot file (.imps)
        #[arg(value_name = "SNAPSHOT")]
        file: PathBuf,

        /// Path to Ed25519 public key file (hex/pem)
        #[arg(short, long)]
        key: PathBuf,
    },
}

#[derive(clap::Args, Debug, Clone)]
pub struct GenerateArgs {
    /// Topology profile shape: graph500, social (Barabási-Albert), social-sbm (Communities), erdos-renyi (Random), grid (2D/3D Mesh), tree (Hierarchical), bipartite
    #[arg(short = 'p', long, default_value = "graph500", value_name = "PROFILE")]
    pub profile: String,

    /// Destination output path (.imps, .tsv, .csv)
    #[arg(short = 'o', long, default_value = "generated_graph.imps", value_name = "OUTPUT")]
    pub output: PathBuf,

    /// Output serialization format: imps, tsv, csv
    #[arg(long, default_value = "imps", value_name = "FORMAT")]
    pub format: String,

    // --- Profile: Graph500 (R-MAT) Parameters ---
    /// Graph500 / R-MAT scale factor S (number of vertices N = 2^S, e.g. 10..36)
    #[arg(short = 's', long, default_value_t = 10, value_name = "SCALE")]
    pub scale: u8,

    /// Graph500 edge factor E (edges = 2^scale * edge_factor)
    #[arg(short = 'e', long, default_value_t = 16, value_name = "FACTOR")]
    pub edge_factor: u32,

    /// Graph500 R-MAT quadrant A probability
    #[arg(long, default_value_t = 0.57, value_name = "PROB")]
    pub a: f64,

    /// Graph500 R-MAT quadrant B probability
    #[arg(long, default_value_t = 0.19, value_name = "PROB")]
    pub b: f64,

    /// Graph500 R-MAT quadrant C probability
    #[arg(long, default_value_t = 0.19, value_name = "PROB")]
    pub c: f64,

    /// Graph500 R-MAT quadrant D probability
    #[arg(long, default_value_t = 0.05, value_name = "PROB")]
    pub d: f64,

    // --- Profile: General Node & Edge Counts (Social, Erdos-Renyi, Bipartite) ---
    /// Total number of vertices/nodes (for social, erdos-renyi, star, etc.)
    #[arg(short = 'n', long, value_name = "NODES")]
    pub nodes: Option<u64>,

    /// Target number of edges (for erdos-renyi, bipartite, graph500 override)
    #[arg(short = 'm', long, value_name = "EDGES")]
    pub edges: Option<u64>,

    /// Edges attached per new node (for Barabási–Albert social model)
    #[arg(long, default_value_t = 8, value_name = "COUNT")]
    pub edges_per_node: u32,

    /// Edge probability p (for Erdos-Renyi G(N, p))
    #[arg(long, value_name = "PROB")]
    pub p: Option<f64>,

    // --- Profile: Social-SBM / Communities Parameters ---
    /// Number of communities / clusters (for social-sbm)
    #[arg(short = 'k', long, default_value_t = 10, value_name = "CLUSTERS")]
    pub communities: u32,

    /// Intra-community edge probability (for social-sbm)
    #[arg(long, default_value_t = 0.02, value_name = "PROB")]
    pub p_intra: f64,

    /// Inter-community edge probability (for social-sbm)
    #[arg(long, default_value_t = 0.0005, value_name = "PROB")]
    pub p_inter: f64,

    // --- Profile: Grid / Lattice Parameters ---
    /// Grid X dimension
    #[arg(long, default_value_t = 32, value_name = "DIM")]
    pub dim_x: u32,

    /// Grid Y dimension
    #[arg(long, default_value_t = 32, value_name = "DIM")]
    pub dim_y: u32,

    /// Grid Z dimension (optional for 3D grid/mesh)
    #[arg(long, value_name = "DIM")]
    pub dim_z: Option<u32>,

    /// Periodic / toroidal grid boundary wrap-around
    #[arg(long)]
    pub toroidal: bool,

    // --- Profile: Tree / Hierarchical Parameters ---
    /// Tree branching factor K (for balanced K-ary tree)
    #[arg(long, default_value_t = 3, value_name = "BRANCHING")]
    pub branching: u32,

    /// Tree depth D (for balanced K-ary tree)
    #[arg(long, default_value_t = 6, value_name = "DEPTH")]
    pub depth: u32,

    /// Generate a star topology (1 central hub connected to all nodes)
    #[arg(long)]
    pub star: bool,

    // --- Profile: Bipartite Parameters ---
    /// Source domain node count (for bipartite graphs)
    #[arg(long, value_name = "COUNT")]
    pub src_nodes: Option<u64>,

    /// Target domain node count (for bipartite graphs)
    #[arg(long, value_name = "COUNT")]
    pub tgt_nodes: Option<u64>,

    // --- General Graph Generation & Output Controls ---
    /// Deterministic pseudo-random seed (default: random system seed)
    #[arg(long, value_name = "SEED")]
    pub seed: Option<u64>,

    /// Generate undirected (symmetric bidirectional) edges
    #[arg(long)]
    pub undirected: bool,

    /// Allow self-loops (u -> u edges)
    #[arg(long)]
    pub allow_self_loops: bool,

    /// Include CSC (Compressed Sparse Column) reverse topology auxiliary index in .imps
    #[arg(long)]
    pub include_csc: bool,

    /// Source domain catalog entity name
    #[arg(long, default_value = "Node", value_name = "NAME")]
    pub domain_name: String,

    /// Target domain catalog entity name (for bipartite graphs)
    #[arg(long, default_value = "Resource", value_name = "NAME")]
    pub tgt_domain_name: String,

    /// Relation catalog name
    #[arg(long, default_value = "CONNECTS", value_name = "NAME")]
    pub relation_name: String,

    /// Comma-separated synthetic edge attributes to generate (e.g. "weight:f32,timestamp:i64,type:i32")
    #[arg(long, value_name = "ATTRS")]
    pub attributes: Option<String>,

    /// Chunk / batch buffer size in edges for streaming generation
    #[arg(long, default_value_t = 5_000_000, value_name = "BATCH_SIZE")]
    pub chunk_size: usize,
}

