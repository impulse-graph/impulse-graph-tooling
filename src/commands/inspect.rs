use comfy_table::Table;
use impulse_graph::spec::{
    IMPULSE_FEAT_SECTION_DIRECTORY, IMPULSE_FEAT_SIGNED_ENFORCED, IMPULSE_FEAT_WIDE_NODE_IDS,
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
            relations.push(json!({
                "src_domain_id": r.src_domain_id,
                "tgt_domain_id": r.tgt_domain_id,
                "encoding_type": format!("{:?}", r.encoding_type),
                "node_count": r.node_count,
                "edge_count": r.edge_count,
            }));
        }

        let output = json!({
            "file": file.to_string_lossy(),
            "size_bytes": metadata.len(),
            "header": {
                "magic": format!("0x{:08X}", header.magic()),
                "version": format!("2.4.0 (0x{:04X})", header.version()),
                "required_features": format!("0x{:016X}", header.required_features()),
                "domain_count": header.domain_count(),
                "relation_count": header.relation_count(),
            },
            "domains": domains,
            "relations": relations,
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
    println!("Version:           2.4.0 (0x{:04X})", header.version());
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
        "Src Domain",
        "Tgt Domain",
        "Encoding",
        "Nodes",
        "Edges",
        "Avg Degree",
    ]);
    for (idx, r) in reader.relations().iter().enumerate() {
        let avg_deg = if r.node_count > 0 {
            r.edge_count as f64 / r.node_count as f64
        } else {
            0.0
        };
        rel_table.add_row(vec![
            idx.to_string(),
            r.src_domain_id.to_string(),
            r.tgt_domain_id.to_string(),
            format!("{:?}", r.encoding_type),
            r.node_count.to_string(),
            r.edge_count.to_string(),
            format!("{:.2}", avg_deg),
        ]);
    }
    println!("--- RELATION CATALOG & TOPOLOGY ---");
    println!("{}", rel_table);

    if verbose {
        println!();
        println!("--- VERBOSE SECTION OFFSETS & METADATA ---");
        for (idx, r) in reader.relations().iter().enumerate() {
            println!(
                "  Relation #{}: CSR Offsets Pos=0x{:X} ({} B), Targets Pos=0x{:X} ({} B)",
                idx, r.csr_offsets_pos, r.csr_offsets_size, r.csr_targets_pos, r.csr_targets_size
            );
        }
    }

    Ok(())
}

fn format_required_features(flags: u64) -> String {
    let mut names = Vec::new();
    if flags & IMPULSE_FEAT_WIDE_NODE_IDS != 0 {
        names.push("WIDE_NODE_IDS");
    }
    if flags & IMPULSE_FEAT_SECTION_DIRECTORY != 0 {
        names.push("SECTION_DIRECTORY");
    }
    if flags & IMPULSE_FEAT_SIGNED_ENFORCED != 0 {
        names.push("SIGNED_ENFORCED");
    }
    format!("0x{:016X} [{}]", flags, names.join(", "))
}
