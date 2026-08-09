use impulse_graph_tooling::compiler;
use impulse_graph_tooling::compiler::frontends::LanguageTarget;
use impulse_graph_tooling::compiler::ir::ast::SExpr;

#[test]
fn test_impscm_reader_parsing() {
    let source = r#"
        (define-query (bfs-reachability [g : graph] [start : node])
            (let ([frontier (bitset:from start)]
                  [visited (bitset:from start)])
                (g:walk-csr g frontier "FOLLOWS")))
    "#;

    let parsed = compiler::ir::reader::parse(source);
    assert!(parsed.is_ok(), "Failed to parse S-expression source: {:?}", parsed);

    let exprs = parsed.unwrap();
    assert_eq!(exprs.len(), 1);

    if let SExpr::List(ref list) = exprs[0] {
        assert_eq!(list.len(), 3);
        if let SExpr::Symbol(ref name) = list[0] {
            assert_eq!(name, "define-query");
        }
    } else {
        panic!("Expected SExpr::List for define-query");
    }
}

#[test]
fn test_vector_fusion_pass() {
    let source = r#"
        (define-query (test-fusion [g : graph] [f : bitset] [v : bitset])
            (bitset:and (g:walk-csr g f "FOLLOWS") (bitset:not v)))
    "#;

    let exprs = compiler::ir::reader::parse(source).unwrap();
    let opt_exprs = compiler::passes::vector_fusion::run(exprs);

    let ir_str = compiler::ir::printer::print_ir(&opt_exprs);
    assert!(ir_str.contains("g:walk-csr-filtered") || ir_str.contains("bitset:and-not"),
            "Vector fusion failed to rewrite AST into fused form: {}", ir_str);
}

#[test]
fn test_partition_elimination_pass() {
    let source = r#"
        (define-query (test-prune [g : graph])
            (g:walk-csr g (bitset:empty) "FOLLOWS"))
    "#;

    let exprs = compiler::ir::reader::parse(source).unwrap();
    let opt_exprs = compiler::passes::partition_elimination::run(exprs);

    let ir_str = compiler::ir::printer::print_ir(&opt_exprs);
    assert!(ir_str.contains("bitset:empty") && !ir_str.contains("g:walk-csr"),
            "Partition elimination failed to prune dead CSR walk: {}", ir_str);
}

#[test]
fn test_compile_to_impas_assembly() {
    let source = r#"
        (define-query (bfs-step [g : graph] [start : node] [target : node])
            (let ([frontier (bitset:from start)]
                  [visited (bitset:from start)])
                (set! frontier (bitset:and-not (g:walk-csr g frontier "FOLLOWS") visited))
                (return #t)))
    "#;

    let asm_res = compiler::compile_to_impas(source);
    assert!(asm_res.is_ok(), "compile_to_impas failed: {:?}", asm_res);

    let asm = asm_res.unwrap();
    assert!(asm.contains(".text"), "Assembly missing .text header: {}", asm);
    assert!(asm.contains("OP_ENTER_FRAME"), "Assembly missing OP_ENTER_FRAME: {}", asm);
    assert!(
        asm.contains("OP_CSR_WALK") || asm.contains("OP_ROARING_BITMAP_AND_NOT"),
        "Assembly missing traversal or bitset opcode: {}",
        asm
    );
    assert!(asm.contains("OP_RET") || asm.contains("OP_HALT"), "Assembly missing OP_RET or OP_HALT: {}", asm);
}

#[test]
fn test_impk_compiler_parsing() {
    let impk_source = r#"
        fn reachability(g, start, target) {
            frontier: bitset start
            visited: bitset start
            frontier: (g @[frontier; "FOLLOWS"]) &~ visited
            return true
        }
    "#;

    let asm_res = compiler::compile_script_to_impas(impk_source, LanguageTarget::ImpK);
    assert!(asm_res.is_ok(), "ImpK compilation failed: {:?}", asm_res);

    let asm = asm_res.unwrap();
    assert!(asm.contains("reachability"), "Missing reachability function label");
    assert!(asm.contains("OP_CSR_WALK"), "Missing OP_CSR_WALK in ImpK output: {}", asm);
    assert!(asm.contains("OP_ROARING_BITMAP_AND_NOT"), "Missing OP_ROARING_BITMAP_AND_NOT in ImpK output: {}", asm);
}

#[test]
fn test_implog_compiler_parsing() {
    let implog_source = r#"
        .decl member(User, Group)
        .decl can_view(User, Document)

        can_view(U, D) :- member(U, G), group_parent(G, D), !visited(G).
    "#;

    let asm_res = compiler::compile_script_to_impas(implog_source, LanguageTarget::ImpLog);
    assert!(asm_res.is_ok(), "ImpLog compilation failed: {:?}", asm_res);

    let asm = asm_res.unwrap();
    assert!(asm.contains("implog_rule_query"), "Missing implog function label");
    assert!(asm.contains("OP_CSR_WALK"), "Missing OP_CSR_WALK in ImpLog output: {}", asm);
    assert!(asm.contains("OP_ROARING_BITMAP_AND_NOT"), "Missing OP_ROARING_BITMAP_AND_NOT in ImpLog output: {}", asm);
}
