#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <mach/kmod.h>
#include <machine/machine_routines.h>
#include "AMDSVM.h"

// External function defined in SVMDriver.S
extern "C" void svm_execute_vmrun(uint64_t *rsp_backup, uint64_t guest_pa,
                                  uint64_t host_pa, void *host_vaddr);

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
#define R64(o)    (*(uint64_t *)(v + (o)))

        // --- 64-bit Long Mode Guest Setup ---
        // Allocate and build identity-mapped page tables (PML4, PDPT, PD, PT)
        // 4KB paging (no 2MB huge pages) to avoid Zen 2 prefetcher issues.

        static IOBufferMemoryDescriptor *pml4_md = NULL;
        static uint64_t pml4_pa = 0;
        static IOBufferMemoryDescriptor *pdp_md = NULL;
        static uint64_t pdp_pa = 0;
        static IOBufferMemoryDescriptor *pd_md = NULL;
        static uint64_t pd_pa = 0;
        static IOBufferMemoryDescriptor *pt_md = NULL;
        static uint64_t pt_pa = 0;

        if (!pml4_md) {
            pml4_md = IOBufferMemoryDescriptor::inTaskWithOptions(
                kernel_task, kIODirectionInOut, PAGE_SIZE);
            pdp_md = IOBufferMemoryDescriptor::inTaskWithOptions(
                kernel_task, kIODirectionInOut, PAGE_SIZE);
            pd_md = IOBufferMemoryDescriptor::inTaskWithOptions(
                kernel_task, kIODirectionInOut, PAGE_SIZE);
            pt_md = IOBufferMemoryDescriptor::inTaskWithOptions(
                kernel_task, kIODirectionInOut, PAGE_SIZE);
            if (pml4_md && pdp_md && pd_md && pt_md &&
                pml4_md->prepare(kIODirectionInOut) == kIOReturnSuccess &&
                pdp_md->prepare(kIODirectionInOut) == kIOReturnSuccess &&
                pd_md->prepare(kIODirectionInOut) == kIOReturnSuccess &&
                pt_md->prepare(kIODirectionInOut) == kIOReturnSuccess) {
                IOByteCount segLen = PAGE_SIZE;
                pml4_pa = pml4_md->getPhysicalSegment(0, &segLen, kIODirectionInOut);
                pdp_pa = pdp_md->getPhysicalSegment(0, &segLen, kIODirectionInOut);
                pd_pa = pd_md->getPhysicalSegment(0, &segLen, kIODirectionInOut);
                pt_pa = pt_md->getPhysicalSegment(0, &segLen, kIODirectionInOut);
                bzero(pml4_md->getBytesNoCopy(), PAGE_SIZE);
                bzero(pdp_md->getBytesNoCopy(), PAGE_SIZE);
                bzero(pd_md->getBytesNoCopy(), PAGE_SIZE);
                bzero(pt_md->getBytesNoCopy(), PAGE_SIZE);
                // PML4[0] → PDPT (covers first 512GB)
                ((uint64_t *)pml4_md->getBytesNoCopy())[0] = pdp_pa | 0x03;
                // PDPT[0] → PD (covers first 1GB)
                ((uint64_t *)pdp_md->getBytesNoCopy())[0] = pd_pa | 0x03;
                // PD[pd_idx] → PT (4KB page table, no PS bit)
                uint64_t pd_idx = (guest_pa >> 21) & 0x1FF;
                ((uint64_t *)pd_md->getBytesNoCopy())[pd_idx] = pt_pa | 0x03;
                // PT[pte_idx] → 4KB identity page for guest_pa
                uint64_t pte_idx = (guest_pa >> 12) & 0x1FF;
                uint64_t page_base = guest_pa & 0xFFFFFFFFFFFFF000ULL;
                ((uint64_t *)pt_md->getBytesNoCopy())[pte_idx] = page_base | 0x03;
            }
        }
        if (!pml4_pa || !pdp_pa || !pd_pa || !pt_pa) {
            IOLog("SVM-UC: page table allocation failed\n");
            return kIOReturnIOError;
        }

        // --- Segments for 64-bit Long Mode ---
        // ES,CS,SS,DS,FS,GS at offsets 0x400..0x460
        // GDTR,LDTR,IDTR,TR stay zeroed (bzero above)
        //     attrib 0x2093 = P=1, DPL=0, S=1, W=1, G=1
        for (int i = 0; i < 6; i++) {
            int off = 0x400 + i * 16;
            W16(off, 0);          // selector = 0
            W16(off + 2, (i >= 4) ? 0xC093 : 0x2093); // attrib (0xC093 for FS/GS for Zen stability)
            W32(off + 4, 0xFFFFFFFF); // limit (flat)
            W64(off + 8, 0);      // base = 0
        }
        // CS at index 1 (offset 0x410): 64-bit code segment
        W16(0x410, 0x08);         // selector = 0x08
        W16(0x412, 0x209B);       // attrib: P=1,DPL=0,S=1,Code=1,L=1,D=0
        W32(0x414, 0xFFFFFFFF);   // limit
        W64(0x418, 0);            // base = 0

        // CPL = 0 at 0x4CA
        W16(0x4CA, 0);

        // Ensure SVM is enabled before any SVM instruction (VMSAVE/VMRUN)
        uint64_t efer = rdmsr(MSR_EFER);
        if (!(efer & EFER_SVME)) {
            wrmsr(MSR_EFER, efer | EFER_SVME);
            efer = rdmsr(MSR_EFER);
            if (!(efer & EFER_SVME)) {
                IOLog("SVM-UC: SVM disabled at runtime\n");
                return kIOReturnNotReady;
            }
        }

        // --- 64-bit Long Mode control registers ---
        // Guest EFER = host EFER + LME|LMA, minus SVME (guest doesn't need SVM)
        // This ensures NXE, SCE, FFXSR bits match host (required for VMRUN in 64-bit mode)
        W64(0x4D0, (efer & ~EFER_SVME) | (1ULL << 8) | (1ULL << 10));
        W64(0x548, (1ULL << 5));                               // CR4 = PAE
        W64(0x550, pml4_pa);                                   // CR3 = guest PML4
        W64(0x558, (1ULL << 31) | (1ULL << 0) | (1ULL << 4) | (1ULL << 1)); // CR0 = PG|PE|ET|MP

        // RFLAGS, RIP, RSP (RSP within single-page allocation, just below code)
        W64(0x570, 2);                      // RFLAGS
        W64(0x578, guest_pa + 0xF00);       // RIP = identity-mapped code address
        W64(0x5D8, guest_pa + 0xE00);       // RSP = below guest code (stack grows down)

        // GPRs (RBX/RCX/RDX/RBP at offsets 0x600, 0x608, 0x610, 0x5E0)
        W64(0x5E0, 0);                      // RBP = 0
        W64(0x5F8, 0);                      // RAX = 0 (CPUID leaf 0)
        W64(0x600, 0);                      // RBX = 0
        W64(0x608, 0);                      // RCX = 0 (CPUID subleaf 0)
        W64(0x610, 0);                      // RDX = 0

        // CR2 = 0
        W64(0x640, 0);

        // Clear debug registers (host DR state must not interfere with guest)
        W64(0x560, 0); // DR7
        W64(0x568, 0); // DR6

        // --- Control area ---
        // Intercept #PF (exception vector 14)
        W32(0x008, (1 << 14));
        // Intercept instructions: INTR(0)|NMI(1)|CPUID(5)|HLT(7)|SHUTDOWN(14)
        // Without HLT, guest HLT hangs VMRUN forever. Without SHUTDOWN, triple fault hangs.
        // Without CPUID, our test guest won't VMEXIT. INTR ensures interrupts wake VMRUN.
        W32(0x00C, (1 << 0) | (1 << 1) | (1 << 5) | (1 << 7) | (1 << 14));
        W32(0x058, 1);                      // Guest ASID = 1
        W32(0x0C0, 0);                      // VMCB Clean = 0 (reload all)

        // Write CPUID (0F A2) + HLT (F4) at offset 0xF00
        // CPUID with intercept causes VMEXIT code 0x72
        v[0xF00] = 0x0F;
        v[0xF01] = 0xA2;
        v[0xF02] = 0xF4;

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

        // Synchronize x86_64 syscall MSRs with host to avoid Zen microcode hangs
        W64(0x594, rdmsr(0xC0000081)); // Guest STAR = host STAR
        W64(0x59C, rdmsr(0xC0000082)); // Guest LSTAR = host LSTAR
        W64(0x5A4, rdmsr(0xC0000083)); // Guest CSTAR = host CSTAR
        W64(0x5AC, rdmsr(0xC0000084)); // Guest SFMASK = host SFMASK

        // Disable interrupts: prevents thread migration during all SVM ops below
        boolean_t prev_intr = ml_set_interrupts_enabled(FALSE);

        // Re-enable SVME on current core (thread may have migrated since initial check)
        uint64_t efer2 = rdmsr(MSR_EFER);
        if (!(efer2 & EFER_SVME))
            wrmsr(MSR_EFER, efer2 | EFER_SVME);

        // Save host TR via VMSAVE, then copy to guest VMCB (VMRUN needs valid TR)
        asm volatile(".byte 0x0F, 0x01, 0xDB\n\t" : : "a"(tr_pa) : "memory");
        uint8_t *tr_page = (uint8_t *)trmd->getBytesNoCopy();

        // VMSAVE writes state-save format. TR segment lives at state-save offset 0x090.
        // In full VMCB (0x400 base), TR = 0x490. In block format (base 0), TR = 0x090.
        // Log both possibilities to see which has data:
        uint64_t tr0_lo = *(uint64_t *)(tr_page + 0x090);
        uint64_t tr0_hi = *(uint64_t *)(tr_page + 0x098);
        uint64_t tr4_lo = *(uint64_t *)(tr_page + 0x490);
        uint64_t tr4_hi = *(uint64_t *)(tr_page + 0x498);
        IOLog("SVM-UC: VMSAVE tr[0x090]=%016llx:%016llx [0x490]=%016llx:%016llx\n",
              tr0_lo, tr0_hi, tr4_lo, tr4_hi);

        // TR selector (low 16 bits) must be non-zero for VMRUN to accept guest state.
        // Pick the offset that has valid-looking data (selector != 0).
        int tr_src = 0x490; // default to full-VMCB layout
        if (tr0_lo && !tr4_lo) tr_src = 0x090; // only 0x090 has data
        else if (!tr0_lo && tr4_lo) tr_src = 0x490; // only 0x490 has data
        IOLog("SVM-UC: TR copied from VMSAVE+0x%x\n", tr_src);
        for (int i = 0; i < 16; i++)
            v[0x490 + i] = tr_page[tr_src + i];

        void *hsave_vaddr = hsave_md->getBytesNoCopy();
        svm_execute_vmrun(&host_rsp_backup, guest_pa, hsave_pa, hsave_vaddr);

        ml_set_interrupts_enabled(prev_intr);
        
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

        // Read VMEXIT info from VMCB (AMD APM: exit_code@0x068, exit_info1@0x070)
        uint64_t exit_code  = *(uint64_t *)(v + 0x068);
        uint64_t exit_info1 = *(uint64_t *)(v + 0x070);
        uint64_t exit_info2 = *(uint64_t *)(v + 0x078);
        IOLog("SVM-UC: VMEXIT code=0x%llx info1=0x%llx info2=0x%llx\n",
              exit_code, exit_info1, exit_info2);

        // If exit code is unexpected, return error so user process can abort
        if (exit_code == 0) {
            IOLog("SVM-UC: ERROR - unexpected zero exit code (VMRUN likely rejected guest state)\n");
            args->scalarOutput[0] = exit_code;
            args->scalarOutput[1] = exit_info1;
            return kIOReturnIOError;
        }
        // Check for expected exit (CPUID = 0x72)
        if (exit_code != SVM_EXIT_CPUID) {
            IOLog("SVM-UC: WARN - unexpected exit code (expected 0x72, got 0x%llx)\n", exit_code);
        }

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
    case SVM_METHOD_HW_PROBE: {
        IOLog("SVM-UC: HW_PROBE started\n");

        // Diagnose SVM state before any instruction
        uint64_t vmcr = rdmsr(0xC0010114); // MSR_VM_CR
        uint64_t efer_hi = rdmsr(MSR_EFER);
        uint32_t a,b,c,d;
        cpuid(0x8000000A, 0, &a, &b, &c, &d);
        IOLog("SVM-UC: VM_CR=0x%llx SVM_DIS=%d SVM_LOCK=%d\n",
              vmcr, !!(vmcr & 0x10), !!(vmcr & 0x08));
        IOLog("SVM-UC: EFER=0x%llx SVME=%d\n", efer_hi, !!(efer_hi & 0x1000));
        IOLog("SVM-UC: CPUID[0x8000000A] SVM=%d rev=0x%x features=0x%x\n", (d & 1), a, d);

        // If SVM_DIS or locked, can't proceed
        if (vmcr & 0x18) { // SVM_DIS | SVM_LOCK
            IOLog("SVM-UC: ERROR — SVM is locked/disabled by firmware\n");
            args->scalarOutput[0] = 0xFFFFFFFF;
            return kIOReturnUnsupported;
        }

        // Allocate a page to capture VMSAVE output
        IOBufferMemoryDescriptor *probe_md = IOBufferMemoryDescriptor::inTaskWithOptions(
            kernel_task, kIODirectionInOut, PAGE_SIZE);
        if (!probe_md) return kIOReturnNoMemory;
        if (probe_md->prepare(kIODirectionInOut) != kIOReturnSuccess) {
            probe_md->release(); return kIOReturnIOError;
        }
        IOByteCount segLen = PAGE_SIZE;
        uint64_t probe_pa = probe_md->getPhysicalSegment(0, &segLen, kIODirectionInOut);
        if (!probe_pa) {
            probe_md->release(); return kIOReturnIOError;
        }
        bzero(probe_md->getBytesNoCopy(), PAGE_SIZE);
        IOLog("SVM-UC: probe_pa=0x%llx\n", probe_pa);

        // Enable SVME locally
        uint64_t orig_efer = efer_hi;
        if (!(efer_hi & EFER_SVME)) {
            wrmsr(MSR_EFER, efer_hi | EFER_SVME);
            efer_hi = rdmsr(MSR_EFER);
            IOLog("SVM-UC: EFER after SVME set = 0x%llx (SVME=%d)\n",
                  efer_hi, !!(efer_hi & 0x1000));
            if (!(efer_hi & EFER_SVME)) {
                IOLog("SVM-UC: ERROR — could not enable SVME\n");
                probe_md->release(); return kIOReturnIOError;
            }
        }

        // VMSAVE with interrupts ENABLED so any #UD is catchable
        asm volatile(".byte 0x0F, 0x01, 0xDB\n\t" : : "a"(probe_pa) : "memory");

        // Restore SVME if we changed it
        if (!(orig_efer & EFER_SVME))
            wrmsr(MSR_EFER, orig_efer);

        // Read VMSAVE output
        uint8_t *probe_page = (uint8_t *)probe_md->getBytesNoCopy();
        uint64_t tr090_lo = *(uint64_t *)(probe_page + 0x090);
        uint64_t tr090_hi = *(uint64_t *)(probe_page + 0x098);
        uint64_t tr490_lo = *(uint64_t *)(probe_page + 0x490);
        uint64_t tr490_hi = *(uint64_t *)(probe_page + 0x498);
        uint64_t gdtr060 = *(uint64_t *)(probe_page + 0x060);
        uint64_t gdtr068 = *(uint64_t *)(probe_page + 0x068);
        uint64_t ldtr070 = *(uint64_t *)(probe_page + 0x070);
        uint64_t ldtr078 = *(uint64_t *)(probe_page + 0x078);
        uint64_t idtr080 = *(uint64_t *)(probe_page + 0x080);
        uint64_t idtr088 = *(uint64_t *)(probe_page + 0x088);
        uint64_t gdtr460 = *(uint64_t *)(probe_page + 0x460);
        uint64_t gdtr468 = *(uint64_t *)(probe_page + 0x468);

        IOLog("SVM-UC: HW_PROBE VMSAVE output:\n");
        IOLog("  TR  [0x090]=%016llx:%016llx  [0x490]=%016llx:%016llx\n",
              tr090_lo, tr090_hi, tr490_lo, tr490_hi);
        IOLog("  GDTR[0x060]=%016llx:%016llx  [0x460]=%016llx:%016llx\n",
              gdtr060, gdtr068, gdtr460, gdtr468);
        IOLog("  LDTR[0x070]=%016llx:%016llx  IDTR[0x080]=%016llx:%016llx\n",
              ldtr070, ldtr078, idtr080, idtr088);

        args->scalarOutput[0] = tr090_lo;
        args->scalarOutput[1] = tr090_hi;
        args->scalarOutput[2] = tr490_lo;
        args->scalarOutput[3] = tr490_hi;

        probe_md->complete(kIODirectionInOut);
        probe_md->release();
        return kIOReturnSuccess;
    }
    default:
        return kIOReturnUnsupported;
    }
}
