use std::collections::HashMap;
use std::fs;
use std::path::Path;

// Include the auto-generated C-ABI FFI bindings from impulse_graph.h
#[allow(non_upper_case_globals, non_camel_case_types, non_snake_case, dead_code)]
mod ffi {
    include!(concat!(env!("OUT_DIR"), "/impulse_bindings.rs"));
}

use ffi::*;

/// JSON manifest schema — mirrors `impulse-compile/cpp` testdata format
#[derive(Debug, serde::Deserialize)]
struct Manifest {
    domains: Vec<DomainDef>,
    relations: Vec<RelationDef>,
}

#[derive(Debug, serde::Deserialize)]
struct DomainDef {
    id: u16,
    key_type: u8,
    name: String,
}

#[derive(Debug, serde::Deserialize)]
struct RelationDef {
    src_domain: u16,
    tgt_domain: u16,
    encoding: String,
    edge_file: String, // relative path to TSV edge list (src_id\ttgt_id per line)
}

fn encoding_type(s: &str) -> u8 {
    match s {
        "raw_uint32"    => IMPULSE_ENC_RAW_UINT32 as u8,
        "delta_vbyte"   => IMPULSE_ENC_DELTA_VBYTE as u8,
        "simdcomp"      => IMPULSE_ENC_SIMDCOMP as u8,
        "sliced_ellpack" => IMPULSE_ENC_SLICED_ELLPACK as u8,
        other => panic!("Unknown encoding type: {}", other),
    }
}

fn build_csr(edges: &[(u32, u32)]) -> (Vec<u32>, Vec<u32>) {
    if edges.is_empty() {
        return (vec![0, 0], vec![]);
    }
    let max_src = edges.iter().map(|(s, _)| *s).max().unwrap_or(0);
    let n = (max_src + 1) as usize;
    let mut degree = vec![0u32; n];
    for (src, _) in edges {
        degree[*src as usize] += 1;
    }
    let mut offsets = vec![0u32; n + 1];
    for i in 0..n {
        offsets[i + 1] = offsets[i] + degree[i];
    }
    let mut col_indices = vec![0u32; edges.len()];
    let mut cursor = offsets[..n].to_vec();
    for (src, tgt) in edges {
        let idx = cursor[*src as usize] as usize;
        col_indices[idx] = *tgt;
        cursor[*src as usize] += 1;
    }
    (offsets, col_indices)
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 {
        eprintln!("Usage: impulse-compile <manifest.json> <output.imps>");
        std::process::exit(1);
    }
    let manifest_path = Path::new(&args[1]);
    let output_path = &args[2];

    let manifest_dir = manifest_path.parent().unwrap_or(Path::new("."));
    let manifest_str = fs::read_to_string(manifest_path)
        .unwrap_or_else(|e| panic!("Failed to read manifest: {e}"));
    let manifest: Manifest = serde_json::from_str(&manifest_str)
        .unwrap_or_else(|e| panic!("Failed to parse manifest JSON: {e}"));

    let t_start = std::time::Instant::now();

    // Create writer
    let output_cstr = std::ffi::CString::new(output_path.as_str()).unwrap();
    let writer = unsafe {
        impulse_writer_create(output_cstr.as_ptr(), IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED as u64)
    };
    if writer.is_null() {
        eprintln!("ERROR: Failed to create snapshot writer");
        std::process::exit(1);
    }

    // Add domains
    for domain in &manifest.domains {
        let name_cstr = std::ffi::CString::new(domain.name.as_str()).unwrap();
        let status = unsafe {
            impulse_writer_add_domain(writer, domain.id, domain.key_type, name_cstr.as_ptr())
        };
        if status != IMPULSE_OK as i32 {
            eprintln!("ERROR: Failed to add domain '{}'", domain.name);
            std::process::exit(1);
        }
    }

    let mut total_edges = 0u64;

    // Add relations
    for rel in &manifest.relations {
        let edge_file = manifest_dir.join(&rel.edge_file);
        let content = fs::read_to_string(&edge_file)
            .unwrap_or_else(|e| panic!("Failed to read edge file {:?}: {e}", edge_file));

        let edges: Vec<(u32, u32)> = content
            .lines()
            .filter(|l| !l.trim().is_empty() && !l.starts_with('#'))
            .map(|l| {
                let mut parts = l.split('\t');
                let src: u32 = parts.next().unwrap().trim().parse().expect("bad src id");
                let tgt: u32 = parts.next().unwrap().trim().parse().expect("bad tgt id");
                (src, tgt)
            })
            .collect();

        let edge_count = edges.len() as u64;
        total_edges += edge_count;

        let (row_offsets, col_indices) = build_csr(&edges);
        let node_count = (row_offsets.len() - 1) as u64;

        let enc = encoding_type(&rel.encoding);
        let section_features = match enc as u32 {
            x if x == IMPULSE_ENC_RAW_UINT32  => 1u64 << 0,
            x if x == IMPULSE_ENC_DELTA_VBYTE => 1u64 << 1,
            x if x == IMPULSE_ENC_SIMDCOMP    => 1u64 << 4,
            x if x == IMPULSE_ENC_SLICED_ELLPACK => 1u64 << 5,
            _ => 0,
        };

        let row_bytes = (row_offsets.len() * 4) as u64;
        let col_bytes = (col_indices.len() * 4) as u64;

        let status = unsafe {
            impulse_writer_add_relation(
                writer,
                rel.src_domain, rel.tgt_domain,
                enc,
                node_count, edge_count,
                section_features,
                row_offsets.as_ptr() as *const std::ffi::c_void, row_bytes,
                col_indices.as_ptr() as *const std::ffi::c_void, col_bytes,
            )
        };
        if status != IMPULSE_OK as i32 {
            eprintln!("ERROR: Failed to add relation {}→{}", rel.src_domain, rel.tgt_domain);
            std::process::exit(1);
        }

        println!(
            "  [✓] Relation {}→{}: {} nodes, {} edges ({})",
            rel.src_domain, rel.tgt_domain, node_count, edge_count, rel.encoding
        );
    }

    // Finalize
    let status = unsafe { impulse_writer_finalize(writer) };
    unsafe { impulse_writer_destroy(writer) };

    if status != IMPULSE_OK as i32 {
        eprintln!("ERROR: Failed to finalize snapshot");
        std::process::exit(1);
    }

    let elapsed = t_start.elapsed().as_millis();
    let meta = fs::metadata(output_path).ok();
    let file_size_mb = meta.map(|m| m.len() as f64 / 1024.0 / 1024.0).unwrap_or(0.0);

    println!("=================================================================");
    println!(" IMPULSE-COMPILE (Rust) — Snapshot Build Complete");
    println!("=================================================================");
    println!(" Output:        {}", output_path);
    println!(" Total Edges:   {}", total_edges);
    println!(" File Size:     {:.2} MB", file_size_mb);
    println!(" Elapsed:       {} ms", elapsed);
    println!("=================================================================");
}
