use impulse_graph::spec::EncodingType;
use impulse_graph::{SnapshotReader, SnapshotWriter};
use std::collections::VecDeque;
use std::error::Error;
use std::path::Path;

pub fn run(
    input_path: &Path,
    output_path: &Path,
    rcm: bool,
    degree_sort: bool,
    csc: bool,
    encoding: Option<&str>,
    _strip_mappings: bool,
    _strip_properties: bool,
) -> Result<(), Box<dyn Error>> {
    println!(
        "Optimizing snapshot: {} -> {}",
        input_path.display(),
        output_path.display()
    );
    let reader = SnapshotReader::open(input_path)?;
    let mut writer = SnapshotWriter::new(output_path.to_str().unwrap());

    // 1. Re-add domains
    for d in reader.domains() {
        writer.add_domain(d.domain_id, d.key_type, &d.name, d.node_count);
    }

    // 2. Re-add relations with optimization transforms
    for (idx, rel) in reader.relations().iter().enumerate() {
        let enc = if let Some(e) = encoding {
            match e.to_lowercase().as_str() {
                "delta_vbyte" => EncodingType::DeltaVbyte,
                "simdcomp" | "simd_comp" => EncodingType::SimdComp,
                "sliced_ellpack" | "ellpack" => EncodingType::SlicedEllpack,
                "tpu_bcoo" | "tpu" | "tpu_coo" => EncodingType::TpuBcoo,
                "raw_uint16" => EncodingType::RawUint16,
                "hybrid1632" | "hybrid_16_32" => EncodingType::Hybrid1632,
                "raw_uint64" => EncodingType::RawUint64,
                "roaring_bitmap" | "roaring" => EncodingType::RoaringBitmap,
                _ => EncodingType::RawUint32,
            }
        } else {
            rel.encoding_type
        };

        let row_offsets = reader.get_row_offsets(idx)?;
        let col_indices = reader.get_col_indices(idx)?;

        let (final_offsets, final_cols) = if rcm && rel.node_count > 0 {
            apply_rcm(rel.node_count as usize, row_offsets, col_indices)
        } else if degree_sort && rel.node_count > 0 {
            apply_degree_sort(rel.node_count as usize, row_offsets, col_indices)
        } else {
            (row_offsets.to_vec(), col_indices.to_vec())
        };

        writer.add_relation_with_csc(
            rel.src_domain_id,
            rel.tgt_domain_id,
            enc,
            rel.node_count,
            rel.edge_count,
            final_offsets,
            final_cols,
            csc,
        );
    }

    writer.finalize()?;
    println!("SUCCESS: Optimized snapshot saved to {}", output_path.display());
    Ok(())
}

/// Apply Reverse Cuthill-McKee (RCM) bandwidth reduction reordering to CSR graph
fn apply_rcm(
    node_count: usize,
    row_offsets: &[u32],
    col_indices: &[u32],
) -> (Vec<u32>, Vec<u32>) {
    let mut visited = vec![false; node_count];
    let mut order = Vec::with_capacity(node_count);

    // Compute node degrees
    let mut degrees: Vec<(usize, u32)> = (0..node_count)
        .map(|i| (i, row_offsets[i + 1] - row_offsets[i]))
        .collect();
    degrees.sort_by_key(|&(_, deg)| deg);

    for (start_node, _) in degrees {
        if visited[start_node] {
            continue;
        }

        let mut queue = VecDeque::new();
        queue.push_back(start_node);
        visited[start_node] = true;

        while let Some(u) = queue.pop_front() {
            order.push(u);

            let start = row_offsets[u] as usize;
            let end = row_offsets[u + 1] as usize;
            let mut neighbors: Vec<usize> = col_indices[start..end]
                .iter()
                .map(|&v| v as usize)
                .filter(|&v| v < node_count && !visited[v])
                .collect();

            // Sort neighbors by degree
            neighbors.sort_by_key(|&v| row_offsets[v + 1] - row_offsets[v]);

            for v in neighbors {
                visited[v] = true;
                queue.push_back(v);
            }
        }
    }

    // Reverse order for RCM
    order.reverse();

    // Map old node index -> new node index
    let mut old_to_new = vec![0u32; node_count];
    for (new_idx, &old_idx) in order.iter().enumerate() {
        old_to_new[old_idx] = new_idx as u32;
    }

    // Reconstruct CSR with reordered nodes
    let mut new_edges: Vec<(u32, u32)> = Vec::new();
    for u in 0..node_count {
        let new_u = old_to_new[u];
        let start = row_offsets[u] as usize;
        let end = row_offsets[u + 1] as usize;
        for &v in &col_indices[start..end] {
            let new_v = if (v as usize) < node_count {
                old_to_new[v as usize]
            } else {
                v
            };
            new_edges.push((new_u, new_v));
        }
    }

    new_edges.sort_unstable();

    let mut new_offsets = vec![0u32; node_count + 1];
    let mut new_cols = Vec::with_capacity(new_edges.len());

    for &(u, v) in &new_edges {
        new_offsets[(u + 1) as usize] += 1;
        new_cols.push(v);
    }
    for i in 0..node_count {
        new_offsets[i + 1] += new_offsets[i];
    }

    (new_offsets, new_cols)
}

/// Apply degree-descending node ID reordering for L1/L2 cache locality
fn apply_degree_sort(
    node_count: usize,
    row_offsets: &[u32],
    col_indices: &[u32],
) -> (Vec<u32>, Vec<u32>) {
    let mut degrees: Vec<(usize, u32)> = (0..node_count)
        .map(|i| (i, row_offsets[i + 1] - row_offsets[i]))
        .collect();
    degrees.sort_by(|a, b| b.1.cmp(&a.1));

    let mut old_to_new = vec![0u32; node_count];
    for (new_idx, &(old_idx, _)) in degrees.iter().enumerate() {
        old_to_new[old_idx] = new_idx as u32;
    }

    let mut new_edges: Vec<(u32, u32)> = Vec::with_capacity(col_indices.len());
    for u in 0..node_count {
        let new_u = old_to_new[u];
        let start = row_offsets[u] as usize;
        let end = row_offsets[u + 1] as usize;
        for &v in &col_indices[start..end] {
            let new_v = if (v as usize) < node_count {
                old_to_new[v as usize]
            } else {
                v
            };
            new_edges.push((new_u, new_v));
        }
    }

    new_edges.sort_unstable();

    let mut new_offsets = vec![0u32; node_count + 1];
    let mut new_cols = Vec::with_capacity(new_edges.len());

    for &(u, v) in &new_edges {
        new_offsets[(u + 1) as usize] += 1;
        new_cols.push(v);
    }
    for i in 0..node_count {
        new_offsets[i + 1] += new_offsets[i];
    }

    (new_offsets, new_cols)
}
