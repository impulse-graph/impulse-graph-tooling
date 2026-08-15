use crate::parquet_reader::read_parquet_columns;
use impulse_graph::spec::KeyType;
use impulse_graph::SnapshotWriter;
use serde::Deserialize;
use std::collections::HashMap;
use std::error::Error;
use std::fs::{self, File};
use std::io::{self, BufRead, BufReader, BufWriter, Write};
use std::path::Path;

#[derive(Debug, Deserialize)]
struct Manifest {
    #[allow(dead_code)]
    version: Option<String>,
    domains: Vec<DomainDef>,
    relations: Vec<RelationDef>,
}

#[derive(Debug, Deserialize)]
struct AttributeDef {
    name: String,
    column: String,
    #[serde(rename = "type")]
    type_name: Option<String>,
    dimension: Option<u32>,
}

#[derive(Debug, Deserialize)]
struct DomainDef {
    id: u16,
    name: String,
    key_type: String,
    file: Option<String>,
    format: Option<String>,
    id_column: Option<String>,
    attributes: Option<Vec<AttributeDef>>,
}

#[derive(Debug, Deserialize)]
struct RelationDef {
    src_domain: u16,
    tgt_domain: u16,
    #[allow(dead_code)]
    encoding: Option<String>,
    file: String,
    format: Option<String>,
    src_column: Option<String>,
    tgt_column: Option<String>,
    #[allow(dead_code)]
    include_csc: Option<bool>,
    attributes: Option<Vec<AttributeDef>>,
}


fn parse_key_type(s: &str) -> KeyType {
    match s.to_lowercase().as_str() {
        "string" | "str" => KeyType::String,
        "uint64" | "u64" | "int64" | "i64" => KeyType::Int64,
        "uuid" => KeyType::Uuid,
        _ => KeyType::Int32,
    }
}

fn parse_type_code(s: &str) -> u8 {
    match s.to_lowercase().as_str() {
        "int32" | "i32" | "uint32" | "u32" => 1,
        "int64" | "i64" | "uint64" | "u64" => 2,
        "float32" | "f32" | "float" => 3,
        "float64" | "f64" | "double" => 4,
        "string" | "str" | "utf8" => 5,
        _ => 1,
    }
}

fn is_parquet_file(file_path: &Path, format_opt: Option<&str>) -> bool {
    if let Some(fmt) = format_opt {
        if fmt.eq_ignore_ascii_case("parquet") || fmt.eq_ignore_ascii_case("pq") {
            return true;
        }
    }
    if let Some(ext) = file_path.extension() {
        let ext_str = ext.to_string_lossy().to_lowercase();
        return ext_str == "parquet" || ext_str == "pq";
    }
    false
}

pub fn run(manifest_path: &Path, output_path: &Path) -> Result<(), Box<dyn Error>> {
    let is_stdout = output_path.to_str() == Some("-") || output_path.to_str() == Some("stdout");

    if !is_stdout {
        println!("Compiling snapshot using manifest: {}...", manifest_path.display());
    }

    let manifest_str = fs::read_to_string(manifest_path)?;
    let manifest: Manifest = serde_json::from_str(&manifest_str)?;
    let base_dir = manifest_path.parent().unwrap_or_else(|| Path::new("."));

    let mut writer = SnapshotWriter::new(output_path.to_str().unwrap_or("snapshot.imps"));

    // Maps domain_id -> (key -> NodeIndex)
    let mut domain_node_maps: HashMap<u16, HashMap<String, u32>> = HashMap::new();
    for d in &manifest.domains {
        domain_node_maps.insert(d.id, HashMap::new());
    }

    // Process optional Domain files (for predefined nodes and node attributes)
    for d in &manifest.domains {
        if let Some(ref file_name) = d.file {
            let domain_file_path = base_dir.join(file_name);
            if !is_stdout {
                println!("  Reading domain file: {}...", domain_file_path.display());
            }

            let id_col = d.id_column.as_deref().unwrap_or("id");

            if is_parquet_file(&domain_file_path, d.format.as_deref()) {
                let mut cols_to_read = vec![id_col.to_string()];
                if let Some(ref attrs) = d.attributes {
                    for attr in attrs {
                        cols_to_read.push(attr.column.clone());
                    }
                }
                let data = read_parquet_columns(&domain_file_path, &cols_to_read)?;
                if let Some(ids) = data.get(id_col) {
                    let map = domain_node_maps.get_mut(&d.id).unwrap();
                    for key in ids {
                        let next_idx = map.len() as u32;
                        map.entry(key.clone()).or_insert(next_idx);
                    }
                }
            } else {
                let file = File::open(&domain_file_path)?;
                let reader = BufReader::new(file);
                let map = domain_node_maps.get_mut(&d.id).unwrap();
                for line_res in reader.lines() {
                    let line = line_res?;
                    let trimmed = line.trim();
                    if trimmed.is_empty() || trimmed.starts_with('#') {
                        continue;
                    }
                    let parts: Vec<&str> = trimmed.split_whitespace().collect();
                    if !parts.is_empty() {
                        let next_idx = map.len() as u32;
                        map.entry(parts[0].to_string()).or_insert(next_idx);
                    }
                }
            }
        }
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
        src_domain_count: u32,
        edges_count: u64,
        row_offsets: Vec<u32>,
        col_indices: Vec<u32>,
        attributes: Vec<(String, u8, u32, Vec<u8>, Option<Vec<u32>>)>,
    }

    let mut compiled_relations = Vec::new();

    // Process Relations and build Node Maps & Edge Attributes
    for r in &manifest.relations {
        let edge_file_path = base_dir.join(&r.file);
        if !is_stdout {
            println!("  Reading edge file: {}...", edge_file_path.display());
        }

        let mut edges: Vec<(u32, u32, usize)> = Vec::new(); // (src_idx, tgt_idx, original_row_index)
        let mut attr_raw_values: HashMap<String, Vec<String>> = HashMap::new();

        if is_parquet_file(&edge_file_path, r.format.as_deref()) {
            let src_col = r.src_column.as_deref().unwrap_or("src");
            let tgt_col = r.tgt_column.as_deref().unwrap_or("tgt");

            let mut cols_to_read = vec![src_col.to_string(), tgt_col.to_string()];
            if let Some(ref attrs) = r.attributes {
                for attr in attrs {
                    cols_to_read.push(attr.column.clone());
                }
            }

            let pq_data = read_parquet_columns(&edge_file_path, &cols_to_read)?;
            let src_keys = pq_data.get(src_col).ok_or("Missing src_column in Parquet file")?;
            let tgt_keys = pq_data.get(tgt_col).ok_or("Missing tgt_column in Parquet file")?;

            for i in 0..src_keys.len() {
                let src_key = &src_keys[i];
                let tgt_key = &tgt_keys[i];

                let src_idx = {
                    let src_map = domain_node_maps.get_mut(&r.src_domain).unwrap();
                    let next_idx = src_map.len() as u32;
                    *src_map.entry(src_key.clone()).or_insert(next_idx)
                };

                let tgt_idx = {
                    let tgt_map = domain_node_maps.get_mut(&r.tgt_domain).unwrap();
                    let next_idx = tgt_map.len() as u32;
                    *tgt_map.entry(tgt_key.clone()).or_insert(next_idx)
                };

                edges.push((src_idx, tgt_idx, i));
            }

            if let Some(ref attrs) = r.attributes {
                for attr in attrs {
                    if let Some(vals) = pq_data.get(&attr.column) {
                        attr_raw_values.insert(attr.name.clone(), vals.clone());
                    }
                }
            }
        } else {
            let file = File::open(&edge_file_path)?;
            let reader = BufReader::new(file);

            let mut row_idx = 0;
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

                edges.push((src_idx, tgt_idx, row_idx));
                row_idx += 1;
            }
        }

        // Sort edges by src_idx then tgt_idx for CSR construction
        edges.sort_by(|a, b| a.0.cmp(&b.0).then_with(|| a.1.cmp(&b.1)));

        let src_domain_count = domain_node_maps.get(&r.src_domain).unwrap().len() as u32;
        let mut row_offsets = vec![0u32; (src_domain_count + 1) as usize];
        let mut col_indices = Vec::with_capacity(edges.len());

        for &(src, tgt, _) in &edges {
            row_offsets[(src + 1) as usize] += 1;
            col_indices.push(tgt);
        }

        // Cumulative sum for row offsets
        for i in 0..src_domain_count as usize {
            row_offsets[i + 1] += row_offsets[i];
        }

        // Process relation attributes aligned with sorted CSR edge indices
        let mut compiled_attrs = Vec::new();
        if let Some(ref attrs) = r.attributes {
            for attr in attrs {
                let type_code = parse_type_code(attr.type_name.as_deref().unwrap_or("int32"));
                let dim = attr.dimension.unwrap_or(1);
                let mut data_bytes = Vec::new();
                let mut string_offsets: Option<Vec<u32>> = if type_code == 5 { Some(vec![0]) } else { None };

                if let Some(raw_vals) = attr_raw_values.get(&attr.name) {
                    for &(_, _, original_idx) in &edges {
                        let val_str = raw_vals.get(original_idx).cloned().unwrap_or_default();
                        match type_code {
                            1 => { // Int32
                                let v: i32 = val_str.parse().unwrap_or(0);
                                data_bytes.extend_from_slice(&v.to_le_bytes());
                            }
                            2 => { // Int64
                                let v: i64 = val_str.parse().unwrap_or(0);
                                data_bytes.extend_from_slice(&v.to_le_bytes());
                            }
                            3 => { // Float32
                                let v: f32 = val_str.parse().unwrap_or(0.0);
                                data_bytes.extend_from_slice(&v.to_le_bytes());
                            }
                            4 => { // Float64
                                let v: f64 = val_str.parse().unwrap_or(0.0);
                                data_bytes.extend_from_slice(&v.to_le_bytes());
                            }
                            5 => { // String
                                data_bytes.extend_from_slice(val_str.as_bytes());
                                data_bytes.push(0); // null-terminated
                                if let Some(ref mut offs) = string_offsets {
                                    offs.push(data_bytes.len() as u32);
                                }
                            }
                            _ => {}
                        }
                    }
                }

                compiled_attrs.push((attr.name.clone(), type_code, dim, data_bytes, string_offsets));
            }
        }

        compiled_relations.push(CompiledRelation {
            src_domain: r.src_domain,
            tgt_domain: r.tgt_domain,
            src_domain_count,
            edges_count: edges.len() as u64,
            row_offsets,
            col_indices,
            attributes: compiled_attrs,
        });
    }

    // Add domain metadata ONCE to SnapshotWriter
    for d in &manifest.domains {
        writer.add_domain(d.id, parse_key_type(&d.key_type), &d.name);
    }

    // Add relations to SnapshotWriter
    for (rel_idx, cr) in compiled_relations.into_iter().enumerate() {
        writer.add_relation(
            cr.src_domain,
            cr.tgt_domain,
            cr.src_domain_count as u64,
            cr.edges_count,
            cr.row_offsets,
            cr.col_indices,
        );

        for attr in cr.attributes {
            writer.add_attribute_to_relation(rel_idx, &attr.0, attr.1, attr.2, attr.3, attr.4);
        }
    }

    // Add Domain Indexes
    for d in &manifest.domains {
        let map = domain_node_maps.get(&d.id).unwrap();
        let key_count = std::cmp::max(16, (map.len() as f64 * 1.5) as u64);
        let seed = 0x1234567890ABCDEF_u64;

        let mut string_pool = vec![0u8];
        let mut offsets = Vec::new();
        let mut keys = Vec::new();
        let mut node_ids = Vec::new();

        for (k, v) in map {
            keys.push(k.clone());
            node_ids.push(*v);
            offsets.push(string_pool.len() as u32);
            string_pool.extend_from_slice(k.as_bytes());
            string_pool.push(0);
        }

        let string_table_bytes = string_pool.len() as u32;
        let mut table = vec![0u8; (key_count as usize) * 8];

        for i in 0..keys.len() {
            let k = &keys[i];
            let off = offsets[i];
            let nid = node_ids[i];

            let mut h = seed;
            for &b in k.as_bytes() {
                h ^= b as u64;
                h = h.wrapping_mul(1099511628211);
            }

            let mut slot = (h % key_count) as usize;
            let start_slot = slot;
            loop {
                let mut k_off = [0u8; 4];
                k_off.copy_from_slice(&table[slot * 8 .. slot * 8 + 4]);
                if u32::from_le_bytes(k_off) == 0 {
                    table[slot * 8 .. slot * 8 + 4].copy_from_slice(&off.to_le_bytes());
                    table[slot * 8 + 4 .. slot * 8 + 8].copy_from_slice(&nid.to_le_bytes());
                    break;
                }
                slot = (slot + 1) % (key_count as usize);
                if slot == start_slot {
                    panic!("Hash table full!");
                }
            }
        }

        let mut index_data = Vec::new();
        index_data.extend_from_slice(&key_count.to_le_bytes());
        index_data.extend_from_slice(&seed.to_le_bytes());
        index_data.extend_from_slice(&string_table_bytes.to_le_bytes());
        index_data.extend_from_slice(&[0u8; 12]);
        index_data.extend_from_slice(&string_pool);
        index_data.extend_from_slice(&table);

        writer.add_index(
            d.id,
            0xFFFF,
            0,
            4, // IMP_INDEX_MINIMAL_PERFECT_HASH
            "_domain_index",
            index_data,
        );
    }

    if is_stdout {
        let stdout = io::stdout();
        let mut handle = BufWriter::new(stdout.lock());
        writer.finalize_to_writer(&mut handle)?;
        handle.flush()?;
    } else {
        writer.finalize()?;
        println!("SUCCESS: Compiled snapshot to {}", output_path.display());
    }

    Ok(())
}
