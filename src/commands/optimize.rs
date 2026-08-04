use impulse_graph::{SnapshotReader, SnapshotWriter};
use std::collections::VecDeque;
use std::error::Error;
use std::path::Path;

pub fn run(
    input_path: &Path,
    output_path: &Path,
    rcm: bool,
    degree_sort: bool,
    _csc: bool,
    _encoding: Option<&str>,
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
        writer.add_domain(d.domain_id, d.key_type, &d.name);
    }

    // 2. Re-add relations with optimization transforms
    for (idx, rel) in reader.relations().iter().enumerate() {
        let row_offsets = reader.get_row_offsets(idx)?;
        let col_indices = reader.get_col_indices(idx)?;

        let (final_offsets, final_cols) = if rcm && rel.node_count > 0 {
            apply_rcm(rel.node_count as usize, row_offsets, col_indices)
        } else if degree_sort && rel.node_count > 0 {
            apply_degree_sort(rel.node_count as usize, row_offsets, col_indices)
        } else {
            (row_offsets.to_vec(), col_indices.to_vec())
        };

        writer.add_relation(
            rel.src_domain_id,
            rel.tgt_domain_id,
            rel.node_count,
            rel.edge_count,
            final_offsets,
            final_cols,
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
            let mut nbrs: Vec<usize> = (start..end)
                .map(|k| col_indices[k] as usize)
                .filter(|&v| v < node_count && !visited[v])
                .collect();

            // Sort neighbors by degree ascending
            nbrs.sort_by_key(|&v| row_offsets[v + 1] - row_offsets[v]);

            for v in nbrs {
                visited[v] = true;
                queue.push_back(v);
            }
        }
    }

    // Reverse for RCM
    order.reverse();

    // Map old node ID -> new node ID
    let mut old_to_new = vec![0u32; node_count];
    for (new_id, &old_id) in order.iter().enumerate() {
        old_to_new[old_id] = new_id as u32;
    }

    // Build new CSR with reordered nodes
    let mut new_edges: Vec<(u32, u32)> = Vec::new();
    for u in 0..node_count {
        let start = row_offsets[u] as usize;
        let end = row_offsets[u + 1] as usize;
        let new_u = old_to_new[u];
        for k in start..end {
            let v = col_indices[k] as usize;
            if v < node_count {
                let new_v = old_to_new[v];
                new_edges.push((new_u, new_v));
            }
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

/// Apply Degree Sort node ordering to CSR graph
fn apply_degree_sort(
    node_count: usize,
    row_offsets: &[u32],
    col_indices: &[u32],
) -> (Vec<u32>, Vec<u32>) {
    let mut degrees: Vec<(usize, u32)> = (0..node_count)
        .map(|i| (i, row_offsets[i + 1] - row_offsets[i]))
        .collect();

    // Sort by degree descending
    degrees.sort_by(|a, b| b.1.cmp(&a.1));

    let mut old_to_new = vec![0u32; node_count];
    for (new_id, &(old_id, _)) in degrees.iter().enumerate() {
        old_to_new[old_id] = new_id as u32;
    }

    let mut new_edges: Vec<(u32, u32)> = Vec::new();
    for u in 0..node_count {
        let start = row_offsets[u] as usize;
        let end = row_offsets[u + 1] as usize;
        let new_u = old_to_new[u];
        for k in start..end {
            let v = col_indices[k] as usize;
            if v < node_count {
                let new_v = old_to_new[v];
                new_edges.push((new_u, new_v));
            }
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
