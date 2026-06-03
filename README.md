# SVMDriver

An experimental kernel extension for AMD SVM (Secure Virtual Machine) virtualization on macOS.

## Purpose
This driver enables hardware-accelerated virtualization on AMD Ryzen-based Hackintosh systems (macOS Tahoe+). It provides a foundation for native SVM acceleration in tools like Docker, colima, and AOSP builds.

## Status: Under Development ⚠️
**Experimental** — proof-of-concept, may cause kernel panics. Use at your own risk.

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

