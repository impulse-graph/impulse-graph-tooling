pub mod backend;
pub mod frontends;
pub mod ir;
pub mod passes;
pub mod regalloc;

use frontends::LanguageTarget;
use std::error::Error;

pub fn compile_to_impas(source: &str) -> Result<String, Box<dyn Error>> {
    compile_script_to_impas(source, LanguageTarget::ImpScm)
}

pub fn compile_script_to_impas(
    source: &str,
    target: LanguageTarget,
) -> Result<String, Box<dyn Error>> {
    // 1. Parse Source Language into ImpScm IR AST
    let exprs = frontends::parse_to_ir(source, target)?;

    // 2. Run AST Optimization, Vector Fusion & Partition Elimination Passes
    let opt_exprs = passes::vector_fusion::run(exprs);
    let opt_exprs = passes::partition_elimination::run(opt_exprs);

    // 3. Perform Linear Scan Register Allocation
    let prog = regalloc::linear_scan::assign_registers(opt_exprs)?;

    // 4. Emit Canonical .impas Text
    let asm = backend::impas_emitter::emit(prog);

    Ok(asm)
}
