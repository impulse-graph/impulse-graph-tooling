use std::fs::File;
use std::io::Read;
use std::path::Path;
use impulse_graph::ffi::impulse_instruction_t;

fn get_opcode_name(opcode: u8) -> &'static str {
    match opcode {
        0x00 => "OP_NOP",
        0x01 => "OP_INIT_INPUT_NODE",
        0x02 => "OP_INIT_INPUT_SET",
        0x03 => "OP_LOAD_CONST_INT",
        0x04 => "OP_MAP_KEYS_TO_DENSE",
        0x05 => "OP_LOAD_CONST_FLOAT",
        0x06 => "OP_LOAD_CONST_STR_PREFIX",
        0x10 => "OP_CSR_WALK",
        0x11 => "OP_CSR_WALK_FILTERED",
        0x12 => "OP_CSR_DEGREE",
        0x13 => "OP_CSR_WALK_PREDICATE",
        0x14 => "OP_NODE_FILTER",
        0x15 => "OP_NODE_FILTER_STR_PREFIX",
        0x16 => "OP_CSR_WALK_REDUCE_SUM",
        0x17 => "OP_CSR_WALK_REDUCE",
        0x30 => "OP_SET_UNION",
        0x31 => "OP_SET_INTERSECT",
        0x32 => "OP_SET_DIFFERENCE",
        0x33 => "OP_SET_CARDINALITY",
        0x34 => "OP_VECTOR_MUL_ATTR",
        0x35 => "OP_VECTOR_REDUCE_SUM",
        0x36 => "OP_VECTOR_DIV",
        0x37 => "OP_VECTOR_STR_CONCAT",
        0x38 => "OP_FLOAT_VECTOR_SCALE",
        0x39 => "OP_L1_NORM_DIFF",
        0x40 => "OP_CC_AFFOREST",
        0x41 => "OP_MXV",
        0x42 => "OP_VXM",
        0x43 => "OP_EWISE_ADD",
        0x44 => "OP_EWISE_MULT",
        0x45 => "OP_REDUCE",
        0x46 => "OP_CC_HOOK_COMPRESS",
        0x47 => "OP_TC_SWEEP_BATCH",
        0x48 => "OP_BRANDES_FORWARD",
        0x49 => "OP_BRANDES_BACKWARD",
        0x4A => "OP_DELTA_STEP_RELAX",
        0x4B => "OP_READ_EDGE_WEIGHT",
        0x50 => "OP_JMP",
        0x51 => "OP_JZ",
        0x52 => "OP_JNZ",
        0x53 => "OP_LOOP_DECR",
        0x54 => "OP_STABLE_CHECK",
        0x55 => "OP_CALL",
        0x56 => "OP_RET",
        0x65 => "OP_ISLAND_DETECT",
        0x70 => "OP_MOV",
        0x71 => "OP_CLEAR_REG",
        0x90 => "OP_COLLECT_BITSET",
        0x91 => "OP_COLLECT_ARRAY",
        0x92 => "OP_MAP_DENSE_TO_KEYS",
        0x93 => "OP_COLLECT_VALUE_MAP",
        0xFF => "OP_HALT",
        _ => "OP_UNKNOWN",
    }
}

fn get_semiring_name(id: u32) -> &'static str {
    match id {
        0 => "SEMIRING_PLUS_TIMES",
        1 => "SEMIRING_MIN_PLUS",
        2 => "SEMIRING_MAX_MIN",
        3 => "SEMIRING_BOOL",
        _ => "SEMIRING_UNKNOWN",
    }
}

fn get_binary_op_name(id: u32) -> &'static str {
    match id {
        0 => "BINARY_OP_ADD",
        1 => "BINARY_OP_MUL",
        2 => "BINARY_OP_MIN",
        3 => "BINARY_OP_MAX",
        4 => "BINARY_OP_AND",
        5 => "BINARY_OP_OR",
        _ => "BINARY_OP_UNKNOWN",
    }
}

pub fn run(input_path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let mut file = File::open(input_path)?;
    let mut buffer = Vec::new();
    file.read_to_end(&mut buffer)?;

    if buffer.len() % 8 != 0 {
        return Err("Binary bytecode file size must be a multiple of 8 bytes".into());
    }

    let instruction_count = buffer.len() / 8;
    println!("\n# Textual Disassembly of {:?}", input_path);
    println!("; Instruction Count: {}", instruction_count);
    println!("; -------------------------------------------------------------");

    for pc in 0..instruction_count {
        let offset = pc * 8;
        let slice = &buffer[offset..offset + 8];
        let instr = unsafe { &*(slice.as_ptr() as *const impulse_instruction_t) };

        let op_name = get_opcode_name(instr.opcode);
        let mut flags_parts = Vec::new();
        if instr.flags & 0x01 != 0 { flags_parts.push("BITSET"); }
        if instr.flags & 0x02 != 0 { flags_parts.push("ACCUMULATE"); }
        if instr.flags & 0x04 != 0 { flags_parts.push("INVERT"); }
        if instr.flags & 0x08 != 0 { flags_parts.push("OFFHEAP"); }
        let flags_str = if flags_parts.is_empty() {
            "0".to_string()
        } else {
            flags_parts.join("|")
        };

        let mut output_str = format!("0x{:04X}: {:<25}", pc, op_name);

        // Decode operands based on opcode category
        match instr.opcode {
            // No operands / halt
            0x00 | 0x56 | 0xFF => {
                // OP_NOP, OP_RET, OP_HALT
            }
            // Jumps / branches (rel offset in payload)
            0x50 | 0x51 | 0x52 => {
                // OP_JMP, OP_JZ, OP_JNZ
                let offset = instr.payload as i32;
                output_str.push_str(&format!("{:+}", offset));
            }
            // Loop branch (R_dst + rel offset)
            0x53 => {
                // OP_LOOP_DECR
                let offset = instr.payload as i32;
                output_str.push_str(&format!("R{}, {:+}", instr.dst_reg, offset));
            }
            // Constant Float loader
            0x05 => {
                // OP_LOAD_CONST_FLOAT
                let fval = f32::from_bits(instr.payload);
                output_str.push_str(&format!("R{}, {}f", instr.dst_reg, fval));
            }
            // Constant Int / Prefix loaders
            0x03 | 0x04 | 0x06 | 0x12 | 0x71 => {
                // OP_LOAD_CONST_INT, OP_MAP_KEYS_TO_DENSE, OP_LOAD_CONST_STR_PREFIX, OP_CSR_DEGREE, OP_CLEAR_REG
                output_str.push_str(&format!("R{}, {}", instr.dst_reg, instr.payload));
            }
            // 2-operand reg-to-reg / mapping
            0x01 | 0x02 | 0x33 | 0x35 | 0x54 | 0x70 | 0x90 | 0x91 | 0x92 => {
                // OP_INIT_INPUT_NODE, OP_INIT_INPUT_SET, OP_SET_CARDINALITY, OP_VECTOR_REDUCE_SUM, OP_STABLE_CHECK, OP_MOV, OP_COLLECT_BITSET, OP_COLLECT_ARRAY, OP_MAP_DENSE_TO_KEYS
                output_str.push_str(&format!("R{}, R{}", instr.dst_reg, instr.payload));
            }
            // CSR Walk: payload holds src_reg (lower 16) and rel_id (upper 16)
            0x10 | 0x11 | 0x14 | 0x15 | 0x16 | 0x17 => {
                let src_reg = instr.payload & 0xFFFF;
                let rel_id = (instr.payload >> 16) & 0xFFFF;
                output_str.push_str(&format!("R{}, R{}, REL_{}", instr.dst_reg, src_reg, rel_id));
            }
            // Element-wise combinations: src1 (byte 0), src2 (byte 1), op_id (byte 2)
            0x43 | 0x44 => {
                // OP_EWISE_ADD, OP_EWISE_MULT
                let src1 = instr.payload & 0xFF;
                let src2 = (instr.payload >> 8) & 0xFF;
                let op_id = (instr.payload >> 16) & 0xFFFF;
                output_str.push_str(&format!("R{}, R{}, R{}, {}", instr.dst_reg, src1, src2, get_binary_op_name(op_id)));
            }
            // Vector Division: num_reg (lower 16) and denom_reg (upper 16)
            0x36 => {
                // OP_VECTOR_DIV
                let num = instr.payload & 0xFFFF;
                let denom = (instr.payload >> 16) & 0xFFFF;
                output_str.push_str(&format!("R{}, R{}, R{}", instr.dst_reg, num, denom));
            }
            // Reduction: src_reg (byte 0), op_id (byte 2)
            0x45 => {
                // OP_REDUCE
                let src = instr.payload & 0xFF;
                let op_id = (instr.payload >> 16) & 0xFFFF;
                output_str.push_str(&format!("R{}, R{}, {}", instr.dst_reg, src, get_binary_op_name(op_id)));
            }
            // SpMV: src_vec (byte 0), rel_id (byte 1), semiring_id (byte 2)
            0x41 | 0x42 => {
                // OP_MXV, OP_VXM
                let src = instr.payload & 0xFF;
                let rel = (instr.payload >> 8) & 0xFF;
                let semiring = (instr.payload >> 16) & 0xFFFF;
                output_str.push_str(&format!("R{}, R{}, REL_{}, {}", instr.dst_reg, src, rel, get_semiring_name(semiring)));
            }
            // Island Detect: src1 (byte 0), src2 (byte 1), rel_id (byte 2)
            0x65 => {
                let src1 = instr.payload & 0xFF;
                let src2 = (instr.payload >> 8) & 0xFF;
                let rel = (instr.payload >> 16) & 0xFFFF;
                output_str.push_str(&format!("R{}, R{}, R{}, REL_{}", instr.dst_reg, src1, src2, rel));
            }
            _ => {
                // Default fallback
                output_str.push_str(&format!("R{}, flags={}, payload=0x{:08X}", instr.dst_reg, flags_str, instr.payload));
            }
        }

        println!("{}", output_str);
    }
    println!("; -------------------------------------------------------------");
    Ok(())
}
