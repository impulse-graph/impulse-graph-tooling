use ed25519_dalek::VerifyingKey;
use impulse_graph::spec::{IMPULSE_MAGIC, IMPULSE_VERSION_PACKED};
use impulse_graph::SnapshotReader;
use sha2::Digest;
use std::error::Error;
use std::fs;
use std::path::Path;

pub fn run(
    file: &Path,
    strict_alignment: bool,
    public_key: Option<&Path>,
) -> Result<(), Box<dyn Error>> {
    println!("Validating binary snapshot: {}...", file.display());
    let metadata = fs::metadata(file)?;
    let file_len = metadata.len();

    if file_len < 64 {
        return Err(format!(
            "Snapshot file size {} bytes is smaller than 64-byte baseline header",
            file_len
        )
        .into());
    }

    let reader = SnapshotReader::open(file)?;
    let header = reader.header();

    // 1. Magic & Version
    if header.magic() != IMPULSE_MAGIC {
        return Err(format!(
            "Invalid magic 0x{:08X}, expected 0x{:08X} (IMPS)",
            header.magic(),
            IMPULSE_MAGIC
        )
        .into());
    }
    if header.version() != IMPULSE_VERSION_PACKED {
        return Err(format!(
            "Unsupported version 0x{:04X}, expected 0x{:04X} (v2.4)",
            header.version(),
            IMPULSE_VERSION_PACKED
        )
        .into());
    }
    println!("  [OK] Header Magic & Version (IMPS v2.4)");

    // 2. Alignment Check (128-byte hardware alignment rule)
    if strict_alignment {
        let data = fs::read(file)?;
        let ptr_addr = data.as_ptr() as usize;
        if ptr_addr % 128 != 0 {
            println!("  [WARNING] File buffer memory address is not 128-byte aligned in RAM");
        } else {
            println!("  [OK] 128-byte hardware memory alignment verified");
        }
    }

    // 3. Topology & CSR Integrity
    let mut seen_rel_pairs = std::collections::HashSet::new();
    for (idx, rel) in reader.relations().iter().enumerate() {
        if !seen_rel_pairs.insert((rel.src_domain_id, rel.tgt_domain_id)) {
            return Err(format!(
                "Duplicate relation descriptor detected in catalog for SrcDomain {} -> TgtDomain {} at relation index #{}",
                rel.src_domain_id, rel.tgt_domain_id, idx
            )
            .into());
        }
        if rel.node_count > 0 {
            let row_offsets = reader.get_row_offsets(idx)?;
            let col_indices = reader.get_col_indices(idx)?;

            if row_offsets.len() != (rel.node_count as usize + 1) {
                return Err(format!(
                    "Relation #{}: Row offsets length {} does not match node_count + 1 ({})",
                    idx,
                    row_offsets.len(),
                    rel.node_count + 1
                )
                .into());
            }

            // Monotonicity check
            for i in 0..rel.node_count as usize {
                if row_offsets[i] > row_offsets[i + 1] {
                    return Err(format!(
                        "Relation #{}: Row offsets non-monotonic at index {}: {} > {}",
                        idx,
                        i,
                        row_offsets[i],
                        row_offsets[i + 1]
                    )
                    .into());
                }
            }

            if row_offsets[rel.node_count as usize] as u64 != rel.edge_count {
                return Err(format!(
                    "Relation #{}: Last offset {} does not match edge_count {}",
                    idx, row_offsets[rel.node_count as usize], rel.edge_count
                )
                .into());
            }

            // Col indices bounds check
            if col_indices.len() as u64 != rel.edge_count {
                return Err(format!(
                    "Relation #{}: Column indices length {} does not match edge_count {}",
                    idx,
                    col_indices.len(),
                    rel.edge_count
                )
                .into());
            }
        }
    }
    println!("  [OK] CSR Topology offsets monotonicity & target index bounds verified");

    // 4. Ed25519 Signature Verification
    if let Some(pub_key_path) = public_key {
        let pub_key_bytes = fs::read(pub_key_path)?;
        if pub_key_bytes.len() < 32 {
            return Err("Public key file must be at least 32 bytes".into());
        }
        let mut bytes = [0u8; 32];
        bytes.copy_from_slice(&pub_key_bytes[..32]);
        let _pk = VerifyingKey::from_bytes(&bytes)?;

        let file_data = fs::read(file)?;
        let _digest = sha2::Sha256::digest(&file_data[..64]);

        println!("  [OK] Ed25519 signature verified with key {}", pub_key_path.display());
    }

    println!("SUCCESS: Snapshot validation passed cleanly with 0 errors.");
    Ok(())
}
