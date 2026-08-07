use clap::Parser;
use impulse_graph_tooling::commands;
use impulse_graph_tooling::{Cli, Commands};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let cli = Cli::parse();

    match cli.command {
        Commands::Inspect {
            file,
            format,
            verbose,
        } => {
            commands::inspect::run(&file, &format, verbose)?;
        }
        Commands::Validate {
            file,
            strict_alignment,
            public_key,
        } => {
            commands::validate::run(&file, strict_alignment, public_key.as_deref())?;
        }
        Commands::Compile { manifest, output } => {
            commands::compile::run(&manifest, &output)?;
        }
        Commands::Optimize {
            input,
            output,
            rcm,
            degree_sort,
            csc,
            encoding,
            strip_mappings,
            strip_properties,
        } => {
            commands::optimize::run(
                &input,
                &output,
                rcm,
                degree_sort,
                csc,
                encoding.as_deref(),
                strip_mappings,
                strip_properties,
            )?;
        }
        Commands::ConvertSafetensors {
            input,
            output,
            domain,
        } => {
            commands::safetensors::run(&input, &output, &domain)?;
        }
        Commands::Keygen { out } => {
            commands::crypto::keygen(&out)?;
        }
        Commands::Sign { file, key } => {
            commands::crypto::sign(&file, &key)?;
        }
        Commands::Verify { file, key } => {
            commands::crypto::verify(&file, &key)?;
        }
        Commands::Diff { base, target } => {
            commands::diff::run(&base, &target)?;
        }
        Commands::Export {
            file,
            out_dir,
            format,
        } => {
            commands::export::run(&file, &out_dir, &format)?;
        }
        Commands::Assemble { input, output } => {
            commands::assemble::run(&input, &output)?;
        }
        Commands::Disassemble { input } => {
            commands::disassemble::run(&input)?;
        }
        Commands::Run {
            snapshot,
            bytecode,
            input_val,
        } => {
            commands::run::run(&snapshot, &bytecode, input_val)?;
        }
    }

    Ok(())
}
