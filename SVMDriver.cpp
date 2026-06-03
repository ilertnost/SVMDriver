#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <mach/kmod.h>
#include "AMDSVM.h"

// External function defined in SVMDriver.S
extern "C" void svm_execute_vmrun(uint64_t *rsp_backup, uint64_t guest_pa, uint64_t host_pa);

#define super IOService

class com_amd_svm;

struct svm_vm {
    IOBufferMemoryDescriptor *vmcb_md;
    struct vmcb *vmcb;
    uint64_t vmcb_pa;
};

class com_amd_svm : public IOService {
    OSDeclareDefaultStructors(com_amd_svm)
public:
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    virtual IOReturn newUserClient(task_t, void *, UInt32, OSDictionary *, IOUserClient **) override;
    virtual IOReturn newUserClient(task_t, void *, UInt32, IOUserClient **) override;
};

class com_amd_svm_uc : public IOUserClient {
    OSDeclareDefaultStructors(com_amd_svm_uc)
    task_t fTask;
    com_amd_svm *fProvider;
    struct svm_vm *fVM;
public:
    virtual bool initWithTask(task_t, void *, UInt32, OSDictionary *) override;
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    virtual IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
                                    IOExternalMethodDispatch *dispatch, OSObject *target,
                                    void *reference) override;
};

OSDefineMetaClassAndStructors(com_amd_svm, IOService)
OSDefineMetaClassAndStructors(com_amd_svm_uc, IOUserClient)

static bool gSVMProbed = false;
static bool gSVMEnabled = false;

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    asm volatile("wrmsr" : : "a"((uint32_t)val), "d"((uint32_t)(val >> 32)), "c"(msr));
}

static inline void cpuid(uint32_t leaf, uint32_t sl,
                         uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    asm volatile("cpuid"
                 : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                 : "a"(leaf), "c"(sl));
}

static void probe_svm(void) {
    uint32_t eax, ebx, ecx, edx, max_ext;
    cpuid(0x80000000, 0, &max_ext, &ebx, &ecx, &edx);
    if (max_ext < 0x8000000A) return;
    cpuid(0x8000000A, 0, &eax, &ebx, &ecx, &edx);
    IOLog("SVM: CPUID[0x8000000A] eax=0x%x\n", eax);
    gSVMProbed = ((edx & 1) != 0);
}

static bool enable_svm(void) {
    // We now enable SVM locally in externalMethod to avoid deadlock during driver start.
    return true; 
}

static void disable_svm(void) {
    if (gSVMEnabled) {
        wrmsr(MSR_EFER, rdmsr(MSR_EFER) & ~EFER_SVME);
        gSVMEnabled = false;
    }
}

bool com_amd_svm::start(IOService *provider) {
    IOLog("SVM: starting driver...\n");
    if (!super::start(provider)) {
        IOLog("SVM: super::start failed\n");
        return false;
    }
    
    IOLog("SVM: probing svm...\n");
    probe_svm();
    if (!gSVMProbed) {
        IOLog("SVM: CPU does not support SVM\n");
        return false;
    }
    
    IOLog("SVM: enabling svm...\n");
    if (!enable_svm()) {
        IOLog("SVM: failed to enable svm\n");
        return false;
    }
    
    IOLog("SVM: registering service...\n");
    setProperty("SVM Supported", "Yes");
    registerService();
    IOLog("SVM: driver loaded successfully\n");
    return true;
}

void com_amd_svm::stop(IOService *provider) {
    disable_svm();
    super::stop(provider);
}

IOReturn com_amd_svm::newUserClient(task_t t, void *s, UInt32 type,
                                     IOUserClient **handler) {
    return newUserClient(t, s, type, NULL, handler);
}

IOReturn com_amd_svm::newUserClient(task_t t, void *s, UInt32 type,
                                     OSDictionary *p, IOUserClient **handler) {
    com_amd_svm_uc *uc = new com_amd_svm_uc;
    if (!uc) return kIOReturnNoMemory;
    if (!uc->initWithTask(t, s, type, p) || !uc->attach(this)) {
        uc->release();
        return kIOReturnIOError;
    }
    uc->start(this);
    *handler = uc;
    return kIOReturnSuccess;
}

extern "C" {
static kern_return_t _module_start(struct kmod_info *ki, void *data);
static kern_return_t _module_stop(struct kmod_info *ki, void *data);
}
KMOD_EXPLICIT_DECL(com.amd.svm, "1.0.0", _module_start, _module_stop)
kern_return_t _module_start(struct kmod_info *ki, void *data) {
    IOLog("SVM: module start\n"); return KERN_SUCCESS;
}
kern_return_t _module_stop(struct kmod_info *ki, void *data) {
    IOLog("SVM: module stop\n"); disable_svm(); return KERN_SUCCESS;
}

bool com_amd_svm_uc::initWithTask(task_t t, void *s, UInt32 type, OSDictionary *p) {
    if (!IOUserClient::initWithTask(t, s, type, p)) return false;
    fTask = t; fProvider = NULL; fVM = NULL;
    return true;
}

bool com_amd_svm_uc::start(IOService *provider) {
    if (!IOUserClient::start(provider)) return false;
    fProvider = OSDynamicCast(com_amd_svm, provider);
    return fProvider != NULL;
}

void com_amd_svm_uc::stop(IOService *provider) {
    if (fVM) {
        if (fVM->vmcb_md) {
            fVM->vmcb_md->complete(kIODirectionInOut);
            fVM->vmcb_md->release();
        }
        IOFree(fVM, sizeof(struct svm_vm));
        fVM = NULL;
    }
    IOUserClient::stop(provider);
}

IOReturn com_amd_svm_uc::externalMethod(uint32_t selector,
                                         IOExternalMethodArguments *args,
                                         IOExternalMethodDispatch *dispatch,
                                         OSObject *target, void *reference) {
    switch (selector) {
    case SVM_METHOD_CREATE_VM: {
        if (fVM) return kIOReturnBusy;
        fVM = (struct svm_vm *)IOMalloc(sizeof(struct svm_vm));
        if (!fVM) return kIOReturnNoMemory;
        bzero(fVM, sizeof(struct svm_vm));

        fVM->vmcb_md = IOBufferMemoryDescriptor::inTaskWithOptions(
            kernel_task, kIODirectionInOut | kIOMemoryKernelUserShared, PAGE_SIZE);
        if (!fVM->vmcb_md) {
            IOFree(fVM, sizeof(struct svm_vm)); fVM = NULL;
            return kIOReturnNoMemory;
        }
        if (fVM->vmcb_md->prepare(kIODirectionInOut) != kIOReturnSuccess) {
            fVM->vmcb_md->release(); IOFree(fVM, sizeof(struct svm_vm)); fVM = NULL;
            return kIOReturnIOError;
        }
        bzero(fVM->vmcb_md->getBytesNoCopy(), PAGE_SIZE);

        IOByteCount segLen = PAGE_SIZE;
        fVM->vmcb_pa = fVM->vmcb_md->getPhysicalSegment(0, &segLen, kIODirectionInOut);
        if (!fVM->vmcb_pa || segLen < PAGE_SIZE) {
            fVM->vmcb_md->release(); IOFree(fVM, sizeof(struct svm_vm)); fVM = NULL;
            IOLog("SVM-UC: getPhysicalSegment failed\n");
            return kIOReturnIOError;
        }
        fVM->vmcb = (struct vmcb *)fVM->vmcb_md->getBytesNoCopy();

        IOLog("SVM-UC: VM created, pa=0x%llx\n", fVM->vmcb_pa);
        args->scalarOutput[0] = fVM->vmcb_pa;
        return kIOReturnSuccess;
    }
    case SVM_METHOD_DESTROY_VM: {
        if (!fVM) return kIOReturnNoDevice;
        if (fVM->vmcb_md) {
            fVM->vmcb_md->complete(kIODirectionInOut);
            fVM->vmcb_md->release();
        }
        IOFree(fVM, sizeof(struct svm_vm)); fVM = NULL;
        IOLog("SVM-UC: VM destroyed\n");
        return kIOReturnSuccess;
    }
    case SVM_METHOD_VMRUN: {
        IOLog("SVM-UC: VMRUN called\n");
        if (!fVM || !fVM->vmcb_md) return kIOReturnNoDevice;
        if (!fVM->vmcb_pa) return kIOReturnIOError;

        uint8_t *v = (uint8_t *)fVM->vmcb;
        uint64_t guest_pa = fVM->vmcb_pa;
#define W64(o, x) (*(uint64_t *)(v + (o)) = (uint64_t)(x))
#define W32(o, x) (*(uint32_t *)(v + (o)) = (uint32_t)(x))
#define W16(o, x) (*(uint16_t *)(v + (o)) = (uint16_t)(x))

        // --- State-save area (VMCB + 0x400) per Linux 5.15 layout ---

        // Segments: ES,CS,SS,DS,FS,GS,GDTR,LDTR,IDTR,TR (16 bytes each)
        //     Layout: selector(2), attrib(2), limit(4), base(8)
        for (int i = 0; i < 10; i++) {
            int off = 0x400 + i * 16;
            W16(off, 0);          // selector
            W16(off + 2, 0x93);   // attrib: P=1,S=1,W=1
            W32(off + 4, 0xFFFF); // limit (64K for real mode)
            W64(off + 8, 0);      // base
        }
        // CS: real-mode code segment pointing to guest_pa + 0xF00
        W16(0x410, 0);
        W16(0x412, 0x93);       // present, read/write
        W32(0x414, 0xFFFF);
        W64(0x418, guest_pa + 0xF00);

        // CPL at 0x4CB
        W16(0x4CA, 0);           // vmpl=0, cpl=0

        // Control registers, EFER
        uint64_t host_cr0, host_cr3, host_cr4, host_efer;
        asm volatile("mov %%cr0, %0" : "=r"(host_cr0));
        asm volatile("mov %%cr3, %0" : "=r"(host_cr3));
        asm volatile("mov %%cr4, %0" : "=r"(host_cr4));
        host_efer = rdmsr(MSR_EFER);

        // Real-mode guest control registers (no paging, no protection)
        // EFER must match host: VMRUN does NOT restore EFER on VMEXIT.
        // If guest clears LME/SVME, host loses long mode after VMEXIT → crash.
        W64(0x4D0, host_efer);              // EFER = host value
        W64(0x548, 0);                      // CR4 = 0
        W64(0x550, 0);                      // CR3 = 0
        W64(0x558, 0x00000010);             // CR0 = ET (paging/protection off)

        // RFLAGS, RIP, RSP
        W64(0x570, 2);                      // RFLAGS (bit 1 = always 1)
        W64(0x578, 0);                      // RIP = 0 (offset within CS)
        W64(0x5D8, 0);                      // RSP = 0

        // RAX = 0
        W64(0x5F8, 0);

        // CR2 = 0
        W64(0x640, 0);

        // --- Control area ---
        // Intercept exceptions: bit 14 = #PF (Page Fault)
        W32(0x008, (1 << 14));

        // Intercept[3] at 0x00C: bit 18=CPUID, bit 24=HLT, bit 31=SHUTDOWN
        W32(0x00C, (1 << 18) | (1 << 24) | (1 << 31));

        // Guest ASID at offset 0x058 (per Linux HSAVE layout)
        W32(0x058, 1);

        // VMCB Clean at 0x0C0 = 0 (reload all on next VMRUN)
        W32(0x0C0, 0);

        // Write CPUID instruction (0x0F 0x0A) at offset 0xF00
        v[0xF00] = 0x0F;
        v[0xF01] = 0x0A;

        // Allocate host-save VMCB (4KB page for VMSAVE/VMLOAD and VMRUN host save)
        static IOBufferMemoryDescriptor *hsave_md = NULL;
        static uint64_t hsave_pa = 0;
        if (!hsave_md) {
            hsave_md = IOBufferMemoryDescriptor::inTaskWithOptions(
                kernel_task, kIODirectionInOut, PAGE_SIZE);
            if (hsave_md && hsave_md->prepare(kIODirectionInOut) == kIOReturnSuccess) {
                IOByteCount segLen = PAGE_SIZE;
                hsave_pa = hsave_md->getPhysicalSegment(0, &segLen, kIODirectionInOut);
            }
        }
        if (!hsave_pa) {
            IOLog("SVM-UC: no host save PA\n");
            return kIOReturnIOError;
        }
        bzero(hsave_md->getBytesNoCopy(), PAGE_SIZE);
        wrmsr(MSR_VM_HSAVE_PA, hsave_pa);

        // Allocate temp page to capture host TR via VMSAVE
        static IOBufferMemoryDescriptor *trmd = NULL;
        static uint64_t tr_pa = 0;
        if (!trmd) {
            trmd = IOBufferMemoryDescriptor::inTaskWithOptions(
                kernel_task, kIODirectionInOut, PAGE_SIZE);
            if (trmd && trmd->prepare(kIODirectionInOut) == kIOReturnSuccess) {
                IOByteCount segLen = PAGE_SIZE;
                tr_pa = trmd->getPhysicalSegment(0, &segLen, kIODirectionInOut);
            }
        }
        if (!tr_pa) {
            IOLog("SVM-UC: no TR save PA\n");
            return kIOReturnIOError;
        }
        bzero(trmd->getBytesNoCopy(), PAGE_SIZE);

        // Save host TR via VMSAVE, then copy it to guest VMCB so VMRUN preserves it
        asm volatile(".byte 0x0F, 0x01, 0xDB\n\t" : : "a"(tr_pa) : "memory");
        uint8_t *tr_page = (uint8_t *)trmd->getBytesNoCopy();
        // VMSAVE writes TR at VMCB offset 0x490 (state save base 0x400 + TR offset 0x090)
        for (int i = 0; i < 16; i++)
            v[0x490 + i] = tr_page[0x490 + i];

        // Ensure SVM is enabled before VMRUN
        uint64_t efer = rdmsr(MSR_EFER);
        if (!(efer & EFER_SVME)) {
            wrmsr(MSR_EFER, efer | EFER_SVME);
            efer = rdmsr(MSR_EFER);
            if (!(efer & EFER_SVME)) {
                IOLog("SVM-UC: SVM disabled at runtime\n");
                return kIOReturnNotReady;
            }
        }

        // Save host state that VMRUN does not preserve
        uint64_t save_fs_base   = rdmsr(0xC0000100); // MSR_FS_BASE
        uint64_t save_gs_base   = rdmsr(0xC0000101); // MSR_GS_BASE
        uint64_t save_kgs_base  = rdmsr(0xC0000102); // MSR_KERNEL_GS_BASE
        uint64_t save_star      = rdmsr(0xC0000081);
        uint64_t save_lstar     = rdmsr(0xC0000082);
        uint64_t save_cstar     = rdmsr(0xC0000083);
        uint64_t save_sfmask    = rdmsr(0xC0000084);
        uint64_t save_sys_cs    = rdmsr(0x174);
        uint64_t save_sys_esp   = rdmsr(0x175);
        uint64_t save_sys_eip   = rdmsr(0x176);
        uint64_t host_rsp_backup = 0;

        IOLog("SVM-UC: VMRUN pa=0x%llx EFER=0x%llx\n", guest_pa, efer);
        
        // Call the robust assembly wrapper
        svm_execute_vmrun(&host_rsp_backup, guest_pa, hsave_pa);
        
        // Restore host MSRs (VMRUN does not preserve these)
        wrmsr(0xC0000100, save_fs_base);
        wrmsr(0xC0000101, save_gs_base);
        wrmsr(0xC0000102, save_kgs_base);
        wrmsr(0xC0000081, save_star);
        wrmsr(0xC0000082, save_lstar);
        wrmsr(0xC0000083, save_cstar);
        wrmsr(0xC0000084, save_sfmask);
        wrmsr(0x174, save_sys_cs);
        wrmsr(0x175, save_sys_esp);
        wrmsr(0x176, save_sys_eip);

        // Debug: dump raw bytes around expected exit_code offsets
        uint64_t c0 = *(uint64_t *)(v + 0x060);
        uint64_t c1 = *(uint64_t *)(v + 0x068);
        uint64_t c2 = *(uint64_t *)(v + 0x070);
        uint64_t c3 = *(uint64_t *)(v + 0x078);
        uint64_t c4 = *(uint64_t *)(v + 0x080);
        IOLog("SVM-UC: VMEXIT raw 0x060=%016llx 0x068=%016llx 0x070=%016llx 0x078=%016llx 0x080=%016llx\n",
              c0, c1, c2, c3, c4);
        // Pick the one that looks like a valid exit code
        uint64_t exit_code  = c2;   // 0x070 per Linux 5.15
        uint64_t exit_info1 = c3;   // 0x078 per Linux 5.15
        if (c2 == 0 && c1 != 0) { exit_code = c1; exit_info1 = c2; }
        IOLog("SVM-UC: VMEXIT code=0x%llx info1=0x%llx\n", exit_code, exit_info1);

        args->scalarOutput[0] = exit_code;
        args->scalarOutput[1] = exit_info1;
        return kIOReturnSuccess;
    }
    case SVM_METHOD_GET_FEATURES: {
        args->scalarOutput[0] = gSVMEnabled ? 1 : 0;
        return kIOReturnSuccess;
    }
    case SVM_METHOD_GET_EXIT: {
        if (!fVM || !fVM->vmcb) return kIOReturnNoDevice;
        uint8_t *v = (uint8_t *)fVM->vmcb;
        args->scalarOutput[0] = *(uint64_t *)(v + 0x070);
        args->scalarOutput[1] = *(uint64_t *)(v + 0x078);
        return kIOReturnSuccess;
    }
    default:
        return kIOReturnUnsupported;
    }
}
