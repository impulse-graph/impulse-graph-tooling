use arrow_array::{Array, Float32Array, Float64Array, Int32Array, Int64Array, StringArray, UInt32Array, UInt64Array};
use parquet::arrow::arrow_reader::ParquetRecordBatchReaderBuilder;
use std::collections::HashMap;
use std::error::Error;
use std::fs::File;
use std::path::Path;

pub fn read_parquet_columns(
    file_path: &Path,
    columns: &[String],
) -> Result<HashMap<String, Vec<String>>, Box<dyn Error>> {
    let file = File::open(file_path)?;
    let builder = ParquetRecordBatchReaderBuilder::try_new(file)?;
    let reader = builder.build()?;

    let mut result: HashMap<String, Vec<String>> = HashMap::new();
    for col in columns {
        result.insert(col.clone(), Vec::new());
    }

    for batch_res in reader {
        let batch = batch_res?;
        let schema = batch.schema();
        for col_name in columns {
            if let Ok(col_idx) = schema.index_of(col_name) {
                let array = batch.column(col_idx);
                let col_vec = result.get_mut(col_name).unwrap();

                // Convert Arrow array values to string representation
                if let Some(string_array) = array.as_any().downcast_ref::<StringArray>() {
                    for i in 0..array.len() {
                        if string_array.is_null(i) {
                            col_vec.push(String::new());
                        } else {
                            col_vec.push(string_array.value(i).to_string());
                        }
                    }
                } else if let Some(i64_array) = array.as_any().downcast_ref::<Int64Array>() {
                    for i in 0..array.len() {
                        col_vec.push(i64_array.value(i).to_string());
                    }
                } else if let Some(i32_array) = array.as_any().downcast_ref::<Int32Array>() {
                    for i in 0..array.len() {
                        col_vec.push(i32_array.value(i).to_string());
                    }
                } else if let Some(u64_array) = array.as_any().downcast_ref::<UInt64Array>() {
                    for i in 0..array.len() {
                        col_vec.push(u64_array.value(i).to_string());
                    }
                } else if let Some(u32_array) = array.as_any().downcast_ref::<UInt32Array>() {
                    for i in 0..array.len() {
                        col_vec.push(u32_array.value(i).to_string());
                    }
                } else if let Some(f32_array) = array.as_any().downcast_ref::<Float32Array>() {
                    for i in 0..array.len() {
                        col_vec.push(f32_array.value(i).to_string());
                    }
                } else if let Some(f64_array) = array.as_any().downcast_ref::<Float64Array>() {
                    for i in 0..array.len() {
                        col_vec.push(f64_array.value(i).to_string());
                    }
                } else {
                    for _i in 0..array.len() {
                        col_vec.push(format!("{:?}", array.to_data()));
                    }

                }
            }
        }
    }

    Ok(result)
}
