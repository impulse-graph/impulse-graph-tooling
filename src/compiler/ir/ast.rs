use std::fmt;

#[derive(Debug, Clone, PartialEq)]
pub enum SExpr {
    Symbol(String),
    Int(i64),
    Float(f64),
    Str(String),
    Bool(bool),
    List(Vec<SExpr>),
}

impl fmt::Display for SExpr {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SExpr::Symbol(s) => write!(f, "{}", s),
            SExpr::Int(n) => write!(f, "{}", n),
            SExpr::Float(val) => write!(f, "{}", val),
            SExpr::Str(s) => write!(f, "{:?}", s),
            SExpr::Bool(b) => write!(f, "{}", if *b { "#t" } else { "#f" }),
            SExpr::List(list) => {
                write!(f, "(")?;
                for (i, elem) in list.iter().enumerate() {
                    if i > 0 {
                        write!(f, " ")?;
                    }
                    write!(f, "{}", elem)?;
                }
                write!(f, ")")
            }
        }
    }
}
