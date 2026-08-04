use arrow_array::{ArrayRef, Int32Array, RecordBatch, StringArray};
use arrow_schema::{DataType, Field, Schema};
use impulse_graph_tooling::commands;
use parquet::arrow::ArrowWriter;
use std::fs::{self, File};
use std::path::PathBuf;
use std::sync::Arc;

fn setup_test_workspace() -> PathBuf {
    let dir = std::env::temp_dir().join(format!("impulse_parquet_test_{}", rand::random::<u32>()));
    let _ = fs::create_dir_all(&dir);
    dir
}

fn write_parquet_file(file_path: &PathBuf, schema: Arc<Schema>, columns: Vec<ArrayRef>) {
    let file = File::create(file_path).unwrap();
    let batch = RecordBatch::try_new(schema.clone(), columns).unwrap();
    let mut writer = ArrowWriter::try_new(file, schema, None).unwrap();
    writer.write(&batch).unwrap();
    writer.close().unwrap();
}

#[test]
fn test_parquet_compile_nodes_edges_attributes() {
    let test_dir = setup_test_workspace();

    let node_file = test_dir.join("users.parquet");
    let edge_file = test_dir.join("follows.parquet");
    let manifest_path = test_dir.join("manifest.json");
    let output_snapshot = test_dir.join("snapshot.imps");

    // 1. Write users.parquet (nodes & attributes)
    let node_schema = Arc::new(Schema::new(vec![
        Field::new("user_id", DataType::Utf8, false),
        Field::new("age", DataType::Int32, false),
    ]));
    let user_ids: ArrayRef = Arc::new(StringArray::from(vec!["u1", "u2", "u3"]));
    let ages: ArrayRef = Arc::new(Int32Array::from(vec![25, 30, 35]));
    write_parquet_file(&node_file, node_schema, vec![user_ids, ages]);

    // 2. Write follows.parquet (edges & attributes)
    let edge_schema = Arc::new(Schema::new(vec![
        Field::new("src_user", DataType::Utf8, false),
        Field::new("tgt_user", DataType::Utf8, false),
        Field::new("weight", DataType::Int32, false),
    ]));
    let src_users: ArrayRef = Arc::new(StringArray::from(vec!["u1", "u1", "u2"]));
    let tgt_users: ArrayRef = Arc::new(StringArray::from(vec!["u2", "u3", "u3"]));
    let weights: ArrayRef = Arc::new(Int32Array::from(vec![10, 20, 30]));
    write_parquet_file(&edge_file, edge_schema, vec![src_users, tgt_users, weights]);

    // 3. Write manifest.json
    let manifest_json = r#"{
        "version": "2.4.0",
        "domains": [
            {
                "id": 0,
                "name": "User",
                "key_type": "string",
                "file": "users.parquet",
                "id_column": "user_id",
                "attributes": [
                    { "name": "age", "column": "age", "type": "int32" }
                ]
            }
        ],
        "relations": [
            {
                "src_domain": 0,
                "tgt_domain": 0,
                "file": "follows.parquet",
                "src_column": "src_user",
                "tgt_column": "tgt_user",
                "attributes": [
                    { "name": "weight", "column": "weight", "type": "int32" }
                ]
            }
        ]
    }"#;
    fs::write(&manifest_path, manifest_json).unwrap();

    // 4. Compile to snapshot file
    let res = commands::compile::run(&manifest_path, &output_snapshot);
    assert!(res.is_ok(), "compile from Parquet failed: {:?}", res);

    // 5. Inspect and validate compiled snapshot
    let reader = impulse_graph::SnapshotReader::open(&output_snapshot).unwrap();
    assert_eq!(reader.header().domain_count(), 1);
    assert_eq!(reader.relation_count(), 1);

    let rel_meta = &reader.relations()[0];
    assert_eq!(rel_meta.edge_count, 3);
    assert_eq!(rel_meta.attributes.len(), 1);



    let _ = fs::remove_dir_all(&test_dir);
}

#[test]
fn test_compile_to_stdout() {
    let test_dir = setup_test_workspace();

    let edge_file = test_dir.join("edges.parquet");
    let manifest_path = test_dir.join("manifest.json");

    let edge_schema = Arc::new(Schema::new(vec![
        Field::new("src", DataType::Utf8, false),
        Field::new("tgt", DataType::Utf8, false),
    ]));
    let src: ArrayRef = Arc::new(StringArray::from(vec!["nodeA", "nodeB"]));
    let tgt: ArrayRef = Arc::new(StringArray::from(vec!["nodeB", "nodeC"]));
    write_parquet_file(&edge_file, edge_schema, vec![src, tgt]);

    let manifest_json = r#"{
        "domains": [
            { "id": 0, "name": "Node", "key_type": "string" }
        ],
        "relations": [
            { "src_domain": 0, "tgt_domain": 0, "file": "edges.parquet" }
        ]
    }"#;
    fs::write(&manifest_path, manifest_json).unwrap();

    // Test output path "-" (stdout)
    let stdout_path = PathBuf::from("-");
    let res = commands::compile::run(&manifest_path, &stdout_path);
    assert!(res.is_ok(), "compile to stdout failed: {:?}", res);

    let _ = fs::remove_dir_all(&test_dir);
}
