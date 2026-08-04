use clap::{Parser, Subcommand};
use std::path::PathBuf;

pub mod commands;
pub mod parquet_reader;


#[derive(Parser)]
#[command(
    name = "impulse-graph",
    author = "Impulse Graph Engine Contributors",
    version = "2.4.0",
    about = "Impulse Graph Engine Developer Utilities & Layout Optimizer Suite (Spec v2.4)",
    long_about = "Official CLI utility for inspecting, validating, compiling, optimizing, signing, comparing, exporting, and ingesting .safetensors into zero-copy Impulse Graph binary snapshot files (.imps)."
)]
pub struct Cli {
    #[command(subcommand)]
    pub command: Commands,
}

#[derive(Subcommand)]
pub enum Commands {
    /// Inspect binary snapshot header, section directory, domain catalogs, and topology metadata
    Inspect {
        /// Path to binary snapshot file (.imps)
        #[arg(value_name = "SNAPSHOT")]
        file: PathBuf,

        /// Output format (text, json)
        #[arg(short, long, default_value = "text")]
        format: String,

        /// Verbose detail dumping (including offset tables and encoding feature bitmaps)
        #[arg(short, long)]
        verbose: bool,
    },

    /// Validate binary snapshot file against Spec v2.4 normative requirements
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

    /// Compile Parquet/CSV/TSV/JSON-L node and edge data into zero-copy binary snapshot (.imps) or stdout stream
    Compile {
        /// Path to JSON compiler manifest file
        #[arg(short, long, value_name = "MANIFEST")]
        manifest: PathBuf,

        /// Output binary snapshot destination path (.imps or '-' for stdout)
        #[arg(short, long, value_name = "OUTPUT")]
        output: PathBuf,
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

    /// Ingest raw binary weight tensors from HuggingFace .safetensors files into .imps snapshot property blocks
    ConvertSafetensors {
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
}
