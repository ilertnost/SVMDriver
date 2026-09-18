# SVMDriver

An experimental kernel extension for AMD SVM (Secure Virtual Machine) virtualization on macOS.

## Purpose
This driver enables hardware-accelerated virtualization on AMD Ryzen-based Hackintosh systems (macOS Tahoe+). It provides a foundation for native SVM acceleration in tools like Docker, colima, and AOSP builds.

## Status: Development Terminated ❌

**Project permanently frozen and not accepting contributions.**

### Reason

After analyzing the macOS Hypervisor.framework internals, it became clear that Apple's virtualization stack is hardcoded for **Intel VT-x (VMX)** instructions. There is no runtime abstraction layer that would allow a third-party kernel extension to substitute AMD SVM for Intel VMX.

Specifically:

- `Hypervisor.framework` is an API to the **XNU kernel**, not a loadable library. It cannot be patched, replaced, or intercepted from userspace or from a kext.
- The XNU kernel executes VMX instructions directly. On AMD hardware, these instructions raise `#UD` (Invalid Opcode), causing the entire system to freeze.
- The entitlement `com.apple.security.hypervisor` is required to use the framework, and it is issued by Apple only. Even with SIP fully disabled, the architectural dependency on VMX remains.
- XNU is open source, but `Hypervisor.framework` is proprietary and closed. Compiling XNU from source does not provide a replacement.

**Conclusion:** hardware-accelerated virtualization on AMD Hackintosh is not possible without patching the closed-source kernel binary — a task equivalent to writing a full KVM port for macOS. This is beyond the scope of a single developer and outside what this project can achieve.

The repository remains as a reference for anyone investigating AMD SVM on macOS.

### What was achieved

- IOKit `IOService` with `IOUserClient` and shared-memory VMCB
- VMRUN/VMSAVE/VMLOAD assembly wrappers
- 64-bit Long Mode guest with identity-mapped page tables
- Working `svm_test` tool that reaches the first VMEXIT

Development stopped at the point where the system-wide freeze on VMEXIT indicated an architectural dead end, not a code bug.

### Forking

Anyone who wants to continue this work is free to fork the repository and attempt to push it further. The code is provided as-is, with no guarantees. If you find a way around the architectural limitations described above, the community will thank you.

## Requirements

- AMD Ryzen CPU (Zen 2/Zen 3) with SVM support
- macOS 26.x (Tahoe) with KDK 26.5 installed
- SIP partially disabled (`csr-active-config = 0x803`)
- [AMFIPass.kext](https://github.com/ilertnost/AMFIPass/releases) for unsigned kext loading
- Xcode Command Line Tools

## Build

```bash
cd SVMDriver
make
```

## Install (runtime, without reboot)

```bash
sudo cp -R SVMDriver.kext /Library/Extensions/
sudo chown -R root:wheel /Library/Extensions/SVMDriver.kext
sudo kextutil -v /Library/Extensions/SVMDriver.kext
```

## Install (via OpenCore, persists across reboots)

1. Mount EFI partition:
   ```bash
   sudo mkdir -p /Volumes/EFI
   sudo mount -t msdos /dev/disk0s1 /Volumes/EFI
   ```
2. Copy kext:
   ```bash
   sudo cp -R SVMDriver.kext /Volumes/EFI/EFI/OC/Kexts/
   ```
3. Add `SVMDriver.kext` to `Kernel -> Add` in `config.plist` **after** AMFIPass.kext.
4. Unmount: `sudo diskutil unmount disk0s1`
5. Reboot.

## Userspace test tool

Build and run:

```bash
cd svm_test
cc -o svm_test svm_test.c
./svm_test
```

Expected output on success:

```
SVMDriver test
=============

SVM features: 0x1
SVM enabled: YES

VM created, handle: 0x...
VMEXIT: code=0x72 info1=0x0
VM destroyed

All tests passed!
```

## Implementation Details

- IOKit `IOService` (`com_amd_svm`) with `IOResources` provider
- `IOUserClient` (`com_amd_svm_uc`) with shared-memory VMCB via `IOBufferMemoryDescriptor`
- VMRUN/VMSAVE/VMLOAD via assembly wrapper (`SVMDriver_asm.S`)
- 64-bit Long Mode guest with identity-mapped page tables (PML4/PDPT/PD with 2MB pages)
- CPUID instruction for first VMEXIT (exit code 0x072, hardware intercept, no host handler needed)

## Architecture

```
┌──────────────┐     IOConnectCallScalarMethod     ┌──────────────┐
│  svm_test    │ ──────────────────────────────►   │ SVMDriver    │
│  (userspace) │                                   │ (kernel)     │
└──────────────┘                                   └───────┬──────┘
                                                           │ VMRUN
                                                           ▼
                                                    ┌──────────────┐
                                                    │ AMD CPU SVM  │
                                                    │ (hardware)   │
                                                    └──────────────┘
```

## License

This project is licensed under the **GNU General Public License v3.0 (GPLv3)**.

You are free to:

- Use the code for any purpose
- Study how it works and modify it
- Redistribute copies
- Distribute modified versions
- Use any patents held by contributors (explicit patent grant)

Under the following conditions:

- Any distributed modifications must also be licensed under GPLv3
- The original copyright notice must be preserved
- No warranty is provided — the software is provided "as is"
- **Anti-tivoization**: you cannot distribute the code in a way that prevents users from running modified versions
- Any networked use of modified code must also provide source to users

See the [LICENSE](LICENSE) file for the full text.

If you fork this project and build on it, your fork must remain open under the same license. Any patents covering your modifications must also be licensed to all users of the fork.
