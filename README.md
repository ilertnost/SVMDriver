# SVMDriver

An experimental kernel extension for AMD SVM (Secure Virtual Machine) virtualization on macOS.

## Purpose
This driver is designed to enable hardware-accelerated virtualization on AMD Ryzen-based Mac systems (Hackintosh), specifically targeting macOS versions like macOS Tahoe. It aims to provide a foundation for virtualization tools (e.g., Docker, AOSP builds) to run with native SVM acceleration.

## Status: Under Development ⚠️
**This driver is currently in active development.** 

- **Experimental**: The code is a proof-of-concept and may be unstable.
- **No Guarantees**: Stability and functionality are not guaranteed.
- **Use at your own risk**: Loading an experimental kernel extension can lead to system instability or kernel panics.

## Development Goals
- Implement a minimal SVM environment (World Switch).
- Enable basic intercepts (CPUID, HLT, etc.).
- Provide a user-space interface via `IOUserClient` for testing and control.
- Verify correct state saving/restoring of host and guest registers.

## Current Implementation
- IOKit `IOService` for driver lifecycle.
- `IOUserClient` for userspace communication.
- Shared-memory VMCB for guest state configuration.
- Basic VMRUN loop for testing VMEXIT handling.
