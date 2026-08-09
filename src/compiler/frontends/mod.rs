pub mod impk;
pub mod implog;

use super::ir::ast::SExpr;
use std::error::Error;

pub enum LanguageTarget {
    ImpScm,
    ImpK,
    ImpLog,
}

pub fn parse_to_ir(source: &str, target: LanguageTarget) -> Result<Vec<SExpr>, Box<dyn Error>> {
    match target {
        LanguageTarget::ImpScm => {
            let exprs = super::ir::reader::parse(source)?;
            Ok(exprs)
        }
        LanguageTarget::ImpK => {
            let exprs = impk::parse(source)?;
            Ok(exprs)
        }
        LanguageTarget::ImpLog => {
            let exprs = implog::parse(source)?;
            Ok(exprs)
        }
    }
}
