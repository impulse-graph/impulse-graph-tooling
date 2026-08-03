use impulse_graph::spec::{EncodingType, KeyType};
use impulse_graph::SnapshotWriter;
use serde::Deserialize;
use std::collections::HashMap;
use std::error::Error;
use std::fs::{self, File};
use std::io::{BufRead, BufReader};
use std::path::Path;

#[derive(Debug, Deserialize)]
struct Manifest {
    #[allow(dead_code)]
    version: Option<String>,
    domains: Vec<DomainDef>,
    relations: Vec<RelationDef>,
}

#[derive(Debug, Deserialize)]
struct DomainDef {
    id: u16,
    name: String,
    key_type: String,
}

#[derive(Debug, Deserialize)]
struct RelationDef {
    src_domain: u16,
    tgt_domain: u16,
    encoding: String,
    file: String,
    include_csc: Option<bool>,
}

fn parse_key_type(s: &str) -> KeyType {
    match s.to_lowercase().as_str() {
        "string" | "str" => KeyType::String,
        "uint64" | "u64" | "int64" | "i64" => KeyType::Int64,
        "uuid" => KeyType::Uuid,
        _ => KeyType::Int32,
    }
}

fn parse_encoding(s: &str) -> EncodingType {
    match s.to_lowercase().as_str() {
        "delta_vbyte" => EncodingType::DeltaVbyte,
        "simdcomp" => EncodingType::SimdComp,
        "sliced_ellpack" => EncodingType::SlicedEllpack,
        _ => EncodingType::RawUint32,
    }
}

pub fn run(manifest_path: &Path, output_path: &Path) -> Result<(), Box<dyn Error>> {
    println!("Compiling snapshot using manifest: {}...", manifest_path.display());
    let manifest_str = fs::read_to_string(manifest_path)?;
    let manifest: Manifest = serde_json::from_str(&manifest_str)?;

    let base_dir = manifest_path.parent().unwrap_or_else(|| Path::new("."));
    let mut writer = SnapshotWriter::new(output_path.to_str().unwrap());

    // Maps domain_id -> (key -> NodeIndex)
    let mut domain_node_maps: HashMap<u16, HashMap<String, u32>> = HashMap::new();
    for d in &manifest.domains {
        domain_node_maps.insert(d.id, HashMap::new());
    }

    // Validate relation uniqueness
    let mut seen_relations = std::collections::HashSet::new();
    for r in &manifest.relations {
        if !seen_relations.insert((r.src_domain, r.tgt_domain)) {
            return Err(format!(
                "Duplicate relation definition in manifest for src_domain {} -> tgt_domain {}",
                r.src_domain, r.tgt_domain
            )
            .into());
        }
    }

    struct CompiledRelation {
        src_domain: u16,
        tgt_domain: u16,
        encoding: String,
        include_csc: bool,
        src_domain_count: u32,
        edges_count: u64,
        row_offsets: Vec<u32>,
        col_indices: Vec<u32>,
    }

    let mut compiled_relations = Vec::new();

    // Process Relations and build Node Maps
    for r in &manifest.relations {
        let edge_file_path = base_dir.join(&r.file);
        println!("  Reading edge file: {}...", edge_file_path.display());
        let file = File::open(&edge_file_path)?;
        let reader = BufReader::new(file);

        let mut edges: Vec<(u32, u32)> = Vec::new();

        for line_res in reader.lines() {
            let line = line_res?;
            let trimmed = line.trim();
            if trimmed.is_empty() || trimmed.starts_with('#') {
                continue;
            }
            let parts: Vec<&str> = trimmed.split_whitespace().collect();
            if parts.len() < 2 {
                continue;
            }

            let src_key = parts[0].to_string();
            let tgt_key = parts[1].to_string();

            let src_idx = {
                let src_map = domain_node_maps.get_mut(&r.src_domain).unwrap();
                let next_idx = src_map.len() as u32;
                *src_map.entry(src_key).or_insert(next_idx)
            };

            let tgt_idx = {
                let tgt_map = domain_node_maps.get_mut(&r.tgt_domain).unwrap();
                let next_idx = tgt_map.len() as u32;
                *tgt_map.entry(tgt_key).or_insert(next_idx)
            };

            edges.push((src_idx, tgt_idx));
        }

        // Sort edges by src_idx then tgt_idx for CSR construction
        edges.sort_unstable();

        let src_domain_count = domain_node_maps.get(&r.src_domain).unwrap().len() as u32;
        let mut row_offsets = vec![0u32; (src_domain_count + 1) as usize];
        let mut col_indices = Vec::with_capacity(edges.len());

        for &(src, tgt) in &edges {
            row_offsets[(src + 1) as usize] += 1;
            col_indices.push(tgt);
        }

        // Cumulative sum for row offsets
        for i in 0..src_domain_count as usize {
            row_offsets[i + 1] += row_offsets[i];
        }

        compiled_relations.push(CompiledRelation {
            src_domain: r.src_domain,
            tgt_domain: r.tgt_domain,
            encoding: r.encoding.clone(),
            include_csc: r.include_csc.unwrap_or(false),
            src_domain_count,
            edges_count: edges.len() as u64,
            row_offsets,
            col_indices,
        });
    }

    // Add domain metadata ONCE to SnapshotWriter
    for d in &manifest.domains {
        let count = domain_node_maps.get(&d.id).map(|m| m.len() as u64).unwrap_or(0);
        writer.add_domain(d.id, parse_key_type(&d.key_type), &d.name, count);
    }

    // Add relations to SnapshotWriter
    for cr in compiled_relations {
        writer.add_relation_with_csc(
            cr.src_domain,
            cr.tgt_domain,
            parse_encoding(&cr.encoding),
            cr.src_domain_count as u64,
            cr.edges_count,
            cr.row_offsets,
            cr.col_indices,
            cr.include_csc,
        );
    }

    writer.finalize()?;
    println!("SUCCESS: Compiled snapshot to {}", output_path.display());
    Ok(())
}
