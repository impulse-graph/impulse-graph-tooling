use std::fs::File;
use std::io::Read;
use std::path::Path;
use std::ffi::CString;
use impulse_graph::ffi::{
    impulse_snapshot_t, impulse_vm_context_t, impulse_instruction_t, impulse_vm_state_t,
    impulse_snapshot_open, impulse_snapshot_close, impulse_snapshot_max_node_count,
    impulse_vm_context_create, impulse_vm_context_destroy,
    impulse_vm_execute,
    impulse_vm_context_get_float_vector, impulse_vm_context_bitset_get_word,
};

pub fn run(
    snapshot_path: &Path,
    bytecode_path: &Path,
    input_val: u64,
) -> Result<(), Box<dyn std::error::Error>> {
    // 1. Open snapshot file via FFI
    let c_path = CString::new(snapshot_path.to_str().ok_or("Invalid snapshot path")?)?;
    let mut status = 0i32;
    let snap: *mut impulse_snapshot_t = unsafe { impulse_snapshot_open(c_path.as_ptr(), &mut status) };

    if snap.is_null() {
        return Err(format!("Failed to open snapshot: status code {}", status).into());
    }
    println!("Loaded snapshot: {:?}", snapshot_path);

    // 2. Create VM query context
    let ctx: *mut impulse_vm_context_t = unsafe { impulse_vm_context_create(snap) };
    if ctx.is_null() {
        unsafe { impulse_snapshot_close(snap); }
        return Err("Failed to create VM context".into());
    }

    // 3. Read bytecode program file
    let mut byte_file = File::open(bytecode_path)?;
    let mut buffer = Vec::new();
    byte_file.read_to_end(&mut buffer)?;

    if buffer.len() % 8 != 0 {
        unsafe {
            impulse_vm_context_destroy(ctx);
            impulse_snapshot_close(snap);
        }
        return Err("Bytecode file size must be a multiple of 8 bytes".into());
    }

    let instruction_count = buffer.len() / 8;
    let instructions = unsafe {
        std::slice::from_raw_parts(
            buffer.as_ptr() as *const impulse_instruction_t,
            instruction_count,
        )
    };

    // 4. Initialize VM execution state
    let mut state = impulse_vm_state_t {
        pc: 0,
        reserved: 0,
        flags: 0,
        registers: [0; 64],
        register_types: [0; 64], // starts as TYPE_NULL (0x00)
        query_context: ctx,
        call_stack: [0; 8],
        call_stack_depth: 0,
        reserved_padding2: 0,
    };

    // Initialize R0 as the primary input parameter (e.g. Root Node ID)
    state.registers[0] = input_val;
    state.register_types[0] = 0x01; // TYPE_INT64

    println!("Executing bytecode ({} instructions) with input R0 = {}...", instruction_count, input_val);
    let start_time = std::time::Instant::now();
    
    // 5. Run the VM execute loop
    let exec_status = unsafe {
        impulse_vm_execute(
            instructions.as_ptr(),
            instruction_count,
            &mut state,
            input_val,
        )
    };

    let elapsed = start_time.elapsed();
    println!("Execution completed in {:.3?} with code {}", elapsed, exec_status);

    if exec_status != 0 {
        unsafe {
            impulse_vm_context_destroy(ctx);
            impulse_snapshot_close(snap);
        }
        return Err(format!("VM returned error status: {}", exec_status).into());
    }

    // 6. Format and display the output register R63
    let return_reg = 63usize;
    let ret_type = state.register_types[return_reg];
    let ret_val = state.registers[return_reg];

    println!("\n# VM Execution Results:");
    println!("Return Register R63 Type: 0x{:02X}", ret_type);

    match ret_type {
        0x04 => {
            // TYPE_BITSET_HANDLE
            let handle = ret_val as usize;
            let num_nodes = unsafe { impulse_snapshot_max_node_count(snap) } as usize;
            let word_count = (num_nodes + 63) / 64;
            
            let mut active_nodes = Vec::new();
            for word_idx in 0..word_count {
                let word = unsafe { impulse_vm_context_bitset_get_word(ctx, handle, word_idx) };
                if word != 0 {
                    for bit in 0..64 {
                        if word & (1 << bit) != 0 {
                            let node_id = word_idx * 64 + bit;
                            if node_id < num_nodes {
                                active_nodes.push(node_id);
                            }
                        }
                    }
                }
            }
            println!("Output BitSet Handle: {}", handle);
            println!("Cardinality: {} / {}", active_nodes.len(), num_nodes);
            if !active_nodes.is_empty() {
                let print_limit = 20usize;
                let print_len = std::cmp::min(active_nodes.len(), print_limit);
                println!("Active Nodes (first {}): {:?}", print_len, &active_nodes[..print_len]);
            }
        }
        0x0C => {
            // TYPE_FLOAT_VECTOR
            let handle = ret_val as usize;
            let num_nodes = unsafe { impulse_snapshot_max_node_count(snap) } as usize;
            let float_ptr = unsafe { impulse_vm_context_get_float_vector(ctx, handle) };
            
            if !float_ptr.is_null() {
                let slice = unsafe { std::slice::from_raw_parts(float_ptr, num_nodes) };
                let mut non_zero_count = 0;
                let mut first_values = Vec::new();
                
                for (i, &val) in slice.iter().enumerate() {
                    if val != 0.0 {
                        non_zero_count += 1;
                        if first_values.len() < 10 {
                            first_values.push((i, val));
                        }
                    }
                }
                println!("Output Float Vector Handle: {}", handle);
                println!("Total Vector Size: {}", num_nodes);
                println!("Non-Zero Elements Count: {}", non_zero_count);
                println!("First 10 non-zero elements: {:?}", first_values);
            } else {
                println!("Float vector pointer is NULL");
            }
        }
        _ => {
            // Scalar or fallback
            println!("Scalar Return Value: {}", ret_val);
        }
    }

    // 7. Cleanup VM and snapshot resources
    unsafe {
        impulse_vm_context_destroy(ctx);
        impulse_snapshot_close(snap);
    }

    Ok(())
}
