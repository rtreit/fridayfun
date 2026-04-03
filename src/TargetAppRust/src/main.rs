use std::sync::atomic::{AtomicBool, Ordering};

static RUNNING: AtomicBool = AtomicBool::new(true);

/// Console ctrl handler — sets RUNNING to false on Ctrl+C / Ctrl+Break.
unsafe extern "system" fn console_handler(ctrl_type: u32) -> i32 {
    // CTRL_C_EVENT = 0, CTRL_BREAK_EVENT = 1
    if ctrl_type == 0 || ctrl_type == 1 {
        RUNNING.store(false, Ordering::SeqCst);
        return 1; // TRUE — handled
    }
    0 // FALSE — not handled
}

/// A simple record struct matching the C++ TargetApp layout for memory inspection.
#[repr(C)]
struct Record {
    id: i32,
    name: [u8; 64],
    value: f64,
}

fn main() {
    // Register Ctrl+C handler.
    unsafe {
        windows_sys::Win32::System::Console::SetConsoleCtrlHandler(
            Some(console_handler),
            1, // TRUE — add handler
        );
    }

    let pid = std::process::id();
    println!("=== TargetApp (Rust) ===");
    println!("PID: {pid}");
    println!("Waiting for debugger... (Ctrl+C to quit)\n");

    // Allocate records on the heap so there's data to inspect.
    let names: [&str; 5] = ["alpha", "bravo", "charlie", "delta", "echo"];
    let mut records: Vec<Record> = Vec::with_capacity(5);

    for (i, name) in names.iter().enumerate() {
        let mut name_buf = [0u8; 64];
        let bytes = name.as_bytes();
        name_buf[..bytes.len()].copy_from_slice(bytes);
        records.push(Record {
            id: ((i + 1) * 100) as i32,
            name: name_buf,
            value: (i + 1) as f64 * 3.14,
        });
    }

    let ptr = records.as_ptr();
    let byte_size = records.len() * std::mem::size_of::<Record>();
    println!("Records allocated at {ptr:p} ({byte_size} bytes)");
    for (i, r) in records.iter().enumerate() {
        let name = std::str::from_utf8(&r.name)
            .unwrap_or("?")
            .trim_end_matches('\0');
        println!("  [{i}] id={} name={name:<10} value={:.2}", r.id, r.value);
    }
    println!();

    // Spin with a heartbeat counter — minimal overhead.
    let mut tick: u64 = 0;
    while RUNNING.load(Ordering::Relaxed) {
        tick += 1;
        if tick % 5_000_000 == 0 {
            print!("\rHeartbeat: {}  ", tick / 5_000_000);
            use std::io::Write;
            let _ = std::io::stdout().flush();
        }
    }

    println!("\nShutting down.");
}
