use clap::Parser;
use impulse_graph_tooling::commands;
use impulse_graph_tooling::{
    Cli, Commands, CompilerCommands, CryptoCommands, SnapshotCommands,
};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let cli = Cli::parse();

    match cli.command {
        // Top-Level Ergonomic & Shorthand Commands
        Commands::Run {
            snapshot,
            bytecode,
            input_val,
        } => {
            commands::run::run(&snapshot, &bytecode, input_val)?;
        }
        Commands::Compile {
            input,
            output,
            emit_ir,
        } => {
            commands::script_compile::run(&input, output.as_deref(), emit_ir)?;
        }
        Commands::Build { manifest, output } => {
            commands::compile::run(&manifest, &output)?;
        }
        Commands::Inspect {
            file,
            format,
            verbose,
        } => {
            commands::inspect::run(&file, &format, verbose)?;
        }
        Commands::Assemble { input, output } => {
            commands::assemble::run(&input, &output)?;
        }
        Commands::Disassemble { input } => {
            commands::disassemble::run(&input)?;
        }
        Commands::Generate(args) => {
            commands::generate::run(&args)?;
        }
        Commands::Stats {
            file,
            format,
            verbose,
            supernode_threshold,
        } => {
            commands::stats::run(&file, &format, verbose, supernode_threshold)?;
        }

        // Compiler Namespace (Code Only)
        Commands::Compiler { command } => match command {
            CompilerCommands::Compile {
                input,
                output,
                emit_ir,
            }
            | CompilerCommands::Build {
                input,
                output,
                emit_ir,
            } => {
                commands::script_compile::run(&input, output.as_deref(), emit_ir)?;
            }
            CompilerCommands::Assemble { input, output } => {
                commands::assemble::run(&input, &output)?;
            }
            CompilerCommands::Disassemble { input } => {
                commands::disassemble::run(&input)?;
            }
            CompilerCommands::Inspect { input } => {
                commands::disassemble::run(&input)?;
            }
        },

        // Snapshot Namespace (Snapshots / Datasets Only)
        Commands::Snapshot { command } => match command {
            SnapshotCommands::Build { manifest, output } => {
                commands::compile::run(&manifest, &output)?;
            }
            SnapshotCommands::Merge {
                base,
                deltas,
                output,
            } => {
                commands::merge::run(&base, &deltas, &output)?;
            }
            SnapshotCommands::Inspect {
                file,
                format,
                verbose,
            } => {
                commands::inspect::run(&file, &format, verbose)?;
            }
            SnapshotCommands::Validate {
                file,
                strict_alignment,
                public_key,
            } => {
                commands::validate::run(&file, strict_alignment, public_key.as_deref())?;
            }
            SnapshotCommands::Optimize {
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
            SnapshotCommands::Diff { base, target } => {
                commands::diff::run(&base, &target)?;
            }
            SnapshotCommands::Export {
                file,
                out_dir,
                format,
            } => {
                commands::export::run(&file, &out_dir, &format)?;
            }
            SnapshotCommands::ConvertTensors {
                input,
                output,
                domain,
            } => {
                commands::safetensors::run(&input, &output, &domain)?;
            }
            SnapshotCommands::Generate(args) => {
                commands::generate::run(&args)?;
            }
            SnapshotCommands::Stats {
                file,
                format,
                verbose,
                supernode_threshold,
            } => {
                commands::stats::run(&file, &format, verbose, supernode_threshold)?;
            }
        },

        // Crypto Namespace
        Commands::Crypto { command } => match command {
            CryptoCommands::Keygen { out } => {
                commands::crypto::keygen(&out)?;
            }
            CryptoCommands::Sign { file, key } => {
                commands::crypto::sign(&file, &key)?;
            }
            CryptoCommands::Verify { file, key } => {
                commands::crypto::verify(&file, &key)?;
            }
        },
    }

    Ok(())
}
