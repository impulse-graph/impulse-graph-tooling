use comfy_table::Table;
use impulse_graph::spec::{
    IMPULSE_FEAT_4KB_PAGE_ALIGNED, IMPULSE_FEAT_CRYPTO_SIGNED,
};
use impulse_graph::SnapshotReader;
use serde_json::json;
use std::error::Error;
use std::fs;
use std::path::Path;

pub fn run(file: &Path, format: &str, verbose: bool) -> Result<(), Box<dyn Error>> {
    let reader = SnapshotReader::open(file)?;
    let metadata = fs::metadata(file)?;
    let header = reader.header();

    if format == "json" {
        let mut domains = Vec::new();
        for d in reader.domains() {
            domains.push(json!({
                "domain_id": d.domain_id,
                "name": d.name,
                "key_type": format!("{:?}", d.key_type),
                "node_count": d.node_count,
            }));
        }

        let mut relations = Vec::new();
        for r in reader.relations() {
            let mut attrs = Vec::new();
            for a in &r.attributes {
                attrs.push(json!({
                    "name": a.name,
                    "type_code": a.type_code,
                    "dimension": a.dimension,
                    "data_offset": a.data_offset,
                    "data_bytes": a.data_bytes,
                }));
            }

            relations.push(json!({
                "relation_id": r.relation_id,
                "src_domain_id": r.src_domain_id,
                "tgt_domain_id": r.tgt_domain_id,
                "encoding_id": r.encoding_id,
                "name": r.name,
                "node_count": r.node_count,
                "edge_count": r.edge_count,
                "csr_row_off_offset": r.csr_row_off_offset,
                "csr_row_off_bytes": r.csr_row_off_bytes,
                "csr_col_idx_offset": r.csr_col_idx_offset,
                "csr_col_idx_bytes": r.csr_col_idx_bytes,
                "attributes": attrs,
            }));
        }

        let custom_metadata = reader.get_metadata().unwrap_or_default();

        let output = json!({
            "file": file.to_string_lossy(),
            "size_bytes": metadata.len(),
            "header": {
                "magic": format!("0x{:08X}", header.magic()),
                "version": format!("0.9.0 (0x{:04X})", header.version()),
                "required_features": format!("0x{:016X}", header.required_features()),
                "domain_count": header.domain_count(),
                "relation_count": header.relation_count(),
            },
            "domains": domains,
            "relations": relations,
            "metadata": custom_metadata,
        });

        println!("{}", serde_json::to_string_pretty(&output)?);
        return Ok(());
    }

    println!("=========================================================================");
    println!("               IMPULSE GRAPH ENGINE SNAPSHOT INSPECTOR                   ");
    println!("=========================================================================");
    println!("File:              {}", file.display());
    println!("File Size:         {} bytes", metadata.len());
    println!("Magic:             0x{:08X} (IMPS)", header.magic());
    let version_str = if header.version() == 9 {
        "0.9.0 (0x0009)".to_string()
    } else {
        format!("0.9.0 (0x{:04X})", header.version())
    };
    println!("Version:           {}", version_str);
    println!("Required Features: {}", format_required_features(header.required_features()));
    println!("Domains:           {}", header.domain_count());
    println!("Relations:         {}", header.relation_count());
    println!();

    // Domain Table
    let mut dom_table = Table::new();
    dom_table.set_header(vec!["ID", "Domain Name", "Key Type", "Node Count"]);
    for d in reader.domains() {
        dom_table.add_row(vec![
            d.domain_id.to_string(),
            d.name.clone(),
            format!("{:?}", d.key_type),
            d.node_count.to_string(),
        ]);
    }
    println!("--- DOMAIN CATALOG ---");
    println!("{}", dom_table);
    println!();

    // Relation Table
    let mut rel_table = Table::new();
    rel_table.set_header(vec![
        "ID",
        "Relation Name",
        "Src Domain",
        "Tgt Domain",
        "Encoding ID",
        "Nodes",
        "Edges",
        "Attributes",
        "Avg Degree",
    ]);
    for r in reader.relations() {
        let avg_deg = if r.node_count > 0 {
            r.edge_count as f64 / r.node_count as f64
        } else {
            0.0
        };
        rel_table.add_row(vec![
            r.relation_id.to_string(),
            if r.name.is_empty() { format!("rel_{}_{}", r.src_domain_id, r.tgt_domain_id) } else { r.name.clone() },
            r.src_domain_id.to_string(),
            r.tgt_domain_id.to_string(),
            format!("0x{:02X}", r.encoding_id),
            r.node_count.to_string(),
            r.edge_count.to_string(),
            r.attributes.len().to_string(),
            format!("{:.2}", avg_deg),
        ]);
    }
    println!("--- RELATION CATALOG & TOPOLOGY ---");
    println!("{}", rel_table);

    if verbose {
        println!();
        println!("--- VERBOSE SECTION OFFSETS & ATTRIBUTE DESCRIPTORS ---");
        for r in reader.relations() {
            println!(
                "  Relation #{}: Name='{}' | CSR Row Offsets Offset=0x{:X} ({} B), Column Indices Offset=0x{:X} ({} B)",
                r.relation_id, r.name, r.csr_row_off_offset, r.csr_row_off_bytes, r.csr_col_idx_offset, r.csr_col_idx_bytes
            );
            for a in &r.attributes {
                println!(
                    "    Attribute '{}': type_code=0x{:02X}, dim={}, data_offset=0x{:X} ({} B)",
                    a.name, a.type_code, a.dimension, a.data_offset, a.data_bytes
                );
            }
        }

        let custom_meta = reader.get_metadata().unwrap_or_default();
        if !custom_meta.is_empty() {
            println!();
            println!("--- CUSTOM METADATA ---");
            for (k, v) in custom_meta {
                println!("  {} = {}", k, v);
            }
        }
    }

    Ok(())
}

fn format_required_features(flags: u64) -> String {
    let mut names = Vec::new();
    if flags & IMPULSE_FEAT_4KB_PAGE_ALIGNED != 0 {
        names.push("4KB_PAGE_ALIGNED");
    }
    if flags & IMPULSE_FEAT_CRYPTO_SIGNED != 0 {
        names.push("CRYPTO_SIGNED");
    }
    format!("0x{:016X} [{}]", flags, names.join(", "))
}
