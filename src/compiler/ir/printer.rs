use super::ast::SExpr;

pub fn print_ir(exprs: &[SExpr]) -> String {
    let mut out = String::new();
    for (i, expr) in exprs.iter().enumerate() {
        if i > 0 {
            out.push('\n');
        }
        out.push_str(&format!("{}", expr));
    }
    out
}
