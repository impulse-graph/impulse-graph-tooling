use crate::compiler;
use crate::compiler::frontends::LanguageTarget;
use std::error::Error;
use std::fs;
use std::path::Path;

pub fn run(
    input: &Path,
    output: Option<&Path>,
    emit_ir: bool,
) -> Result<(), Box<dyn Error>> {
    let source = fs::read_to_string(input)?;

    let target = match input.extension().and_then(|ext| ext.to_str()) {
        Some("impk") => LanguageTarget::ImpK,
        Some("implog") => LanguageTarget::ImpLog,
        _ => LanguageTarget::ImpScm,
    };

    if emit_ir {
        let exprs = compiler::frontends::parse_to_ir(&source, target)?;
        let opt_exprs = compiler::passes::vector_fusion::run(exprs);
        let ir_str = compiler::ir::printer::print_ir(&opt_exprs);
        println!("{}", ir_str);
        return Ok(());
    }

    let asm_text = compiler::compile_script_to_impas(&source, target)?;

    if let Some(out_path) = output {
        fs::write(out_path, &asm_text)?;
        println!("Successfully compiled {:?} to {:?}", input, out_path);
    } else {
        println!("{}", asm_text);
    }

    Ok(())
}
