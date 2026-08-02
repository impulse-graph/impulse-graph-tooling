use impulse_graph::spec::{DataType, EncodingType, KeyType};
use impulse_graph::{PropertyField, SnapshotWriter};
use memmap2::MmapOptions;
use safetensors::SafeTensors;
use std::error::Error;
use std::fs::{self, File};
use std::path::{Path, PathBuf};

pub fn run(
    safetensors_path: &Path,
    output_path: &Path,
    domain_name: &str,
) -> Result<(), Box<dyn Error>> {
    println!(
        "Ingesting .safetensors binary weight shards: {} -> {}...",
        safetensors_path.display(),
        output_path.display()
    );

    let mut shard_files: Vec<PathBuf> = Vec::new();

    if safetensors_path.is_dir() {
        for entry_res in fs::read_dir(safetensors_path)? {
            let entry = entry_res?;
            let p = entry.path();
            if p.is_file() && p.extension().map_or(false, |ext| ext == "safetensors") {
                shard_files.push(p);
            }
        }
        shard_files.sort();
    } else {
        shard_files.push(safetensors_path.to_path_buf());
    }

    if shard_files.is_empty() {
        return Err(format!("No .safetensors files found in {}", safetensors_path.display()).into());
    }

    println!("  Found {} .safetensors shard files to ingest", shard_files.len());

    let mut total_tensors = 0;
    let mut total_bytes: u64 = 0;
    let mut prop_fields = Vec::new();

    for shard in &shard_files {
        println!("  - Ingesting shard file: {}...", shard.display());
        let file = File::open(shard)?;
        let mmap = unsafe { MmapOptions::new().map(&file)? };

        let tensors = SafeTensors::deserialize(&mmap)?;
        let names = tensors.names();

        for name in &names {
            if let Ok(tensor) = tensors.tensor(name) {
                let data_bytes = tensor.data().to_vec();
                let bytes_len = data_bytes.len() as u64;
                total_bytes += bytes_len;
                total_tensors += 1;

                let dtype = match tensor.dtype() {
                    safetensors::Dtype::F32 => DataType::Float32,
                    safetensors::Dtype::F16 => DataType::Float16,
                    safetensors::Dtype::BF16 => DataType::Float16,
                    safetensors::Dtype::I32 => DataType::Int32,
                    safetensors::Dtype::I64 => DataType::Int64,
                    safetensors::Dtype::U8 => DataType::Uint8,
                    safetensors::Dtype::I8 => DataType::Int8,
                    safetensors::Dtype::BOOL => DataType::Bool8,
                    _ => DataType::Float32,
                };

                println!(
                    "      tensor '{}': shape={:?}, dtype={:?}, bytes={:.2} MB",
                    name,
                    tensor.shape(),
                    tensor.dtype(),
                    bytes_len as f64 / (1024.0 * 1024.0)
                );

                prop_fields.push(PropertyField {
                    name: name.to_string(),
                    data_type: dtype,
                    data: data_bytes,
                });
            }
        }
    }

    let mut writer = SnapshotWriter::new(output_path.to_str().unwrap());
    writer.add_domain(0, KeyType::String, domain_name, total_tensors as u64);

    // Add fixed node properties (SoA mode)
    writer.add_domain_fixed_props(0, true, prop_fields);

    // Baseline self-relation table
    let row_offsets = vec![0u32; total_tensors + 1];
    let col_indices = vec![];
    writer.add_relation(
        0,
        0,
        EncodingType::RawUint32,
        total_tensors as u64,
        0,
        row_offsets,
        col_indices,
    );

    writer.finalize()?;

    let final_mb = total_bytes as f64 / (1024.0 * 1024.0);
    let final_gb = final_mb / 1024.0;
    println!(
        "SUCCESS: Ingested {} weight tensors ({:.2} MB / {:.2} GB) into Impulse Graph snapshot: {}",
        total_tensors,
        final_mb,
        final_gb,
        output_path.display()
    );

    Ok(())
}
