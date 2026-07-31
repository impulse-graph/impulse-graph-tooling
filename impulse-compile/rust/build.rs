// build.rs — links against libimpulse_graph (C-ABI engine) at build time
fn main() {
    let impulse_cpp = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../../impulse-graph/impulse-cpp");

    // Try pre-built static archive first, fall back to shared lib
    let static_lib = impulse_cpp.join("libimpulse_graph_static.a");
    if static_lib.exists() {
        println!("cargo:rustc-link-search=native={}", impulse_cpp.display());
        println!("cargo:rustc-link-lib=static=impulse_graph_static");
    } else {
        println!("cargo:rustc-link-search=native={}", impulse_cpp.display());
        println!("cargo:rustc-link-lib=dylib=impulse_graph");
    }

    // Bindgen: generate Rust FFI bindings from impulse_graph.h
    let bindings = bindgen::Builder::default()
        .header(impulse_cpp.join("include/impulse_graph.h").to_str().unwrap())
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .generate()
        .expect("Unable to generate bindings from impulse_graph.h");

    let out_dir = std::path::PathBuf::from(std::env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_dir.join("impulse_bindings.rs"))
        .expect("Could not write bindings");
}
