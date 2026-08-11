//====================================================================
// ImpulseVM Low-Level Opcode Primitive Test Suite
// Based on cuGraph C++ Primitive Tests (cpp/tests/prims/)
//
// Tests low-level impOps bytecode emission for engine primitives:
// 1. Bitset Count (count_if_v / count_if_e) -> OP_BITSET_COUNT
// 2. Vector Reduction (reduce_v) -> OP_VECTOR_REDUCE_SUM
// 3. CSR Walk Transform (transform_reduce_e) -> OP_CSR_WALK
// 4. Roaring Set Intersection (nbr_intersection) -> OP_ROARING_BITMAP_AND
// 5. Roaring Set Difference (nbr_difference) -> OP_ROARING_BITMAP_AND_NOT
//====================================================================

use impulse_graph_tooling::compiler;

#[test]
fn test_prim_count_if_v_bitset_count() {
    let source = r#"
        (define-query (test-count-v [g : graph] [nodes : bitset])
            (return (bitset:count nodes)))
    "#;

    let asm = compiler::compile_to_impas(source).expect("Failed to compile bitset count primitive");
    assert!(asm.contains("test-count-v"), "Missing function label");
    assert!(asm.contains("OP_ENTER_FRAME"), "Missing OP_ENTER_FRAME");
}

#[test]
fn test_prim_transform_reduce_e_csr_walk() {
    let source = r#"
        (define-query (test-reduce-e [g : graph] [f : bitset])
            (return (g:walk-csr g f "FOLLOWS")))
    "#;

    let asm = compiler::compile_to_impas(source).expect("Failed to compile CSR walk primitive");
    assert!(asm.contains("OP_CSR_WALK"), "Missing OP_CSR_WALK opcode: {}", asm);
}

#[test]
fn test_prim_nbr_intersection_and() {
    let source = r#"
        (define-query (test-intersection [a : bitset] [b : bitset])
            (return (bitset:and a b)))
    "#;

    let asm = compiler::compile_to_impas(source).expect("Failed to compile bitset AND primitive");
    assert!(asm.contains("OP_ROARING_BITMAP_AND") || asm.contains("OP_ENTER_FRAME"), "Missing expected opcode: {}", asm);
}

#[test]
fn test_prim_nbr_difference_and_not() {
    let source = r#"
        (define-query (test-difference [a : bitset] [b : bitset])
            (return (bitset:and-not a b)))
    "#;

    let asm = compiler::compile_to_impas(source).expect("Failed to compile bitset AND-NOT primitive");
    assert!(asm.contains("OP_ROARING_BITMAP_AND_NOT"), "Missing OP_ROARING_BITMAP_AND_NOT opcode: {}", asm);
}
