#ifndef AMD_SVM_H
#define AMD_SVM_H

#include <stdint.h>
#include <mach/mach_types.h>

#define SVM_SERVICE_NAME "com.amd.svm"

#define SVM_CPUID_LEAF    0x8000000A

#define MSR_EFER          0xC0000080
#define EFER_SVME         (1ULL << 12)

#define MSR_VM_CR         0xC0010114
#define VM_CR_SVM_DIS     (1ULL << 4)
#define VM_CR_SVM_LOCK    (1ULL << 3)

#define MSR_VM_HSAVE_PA   0xC0010117

#define SVM_USER_CLIENT  "com_amd_svm_uc"

enum {
    SVM_METHOD_CREATE_VM    = 0,
    SVM_METHOD_DESTROY_VM   = 1,
    SVM_METHOD_VMRUN        = 2,
    SVM_METHOD_GET_FEATURES = 3,
    SVM_METHOD_GET_EXIT     = 4,
    SVM_METHOD_HW_PROBE     = 5,
};

enum vm_exit_codes {
    SVM_EXIT_CR0_READ  = 0x000,
    SVM_EXIT_CR0_WRITE = 0x060,
    SVM_EXIT_CPUID     = 0x072,
    SVM_EXIT_HLT       = 0x078,
    SVM_EXIT_MSR       = 0x07C,
    SVM_EXIT_VMMCALL   = 0x081,
    SVM_EXIT_IO        = 0x064,
    SVM_EXIT_NPF       = 0x400,
};

struct vmcb_control_area {
    uint16_t intercept_cr_read;
    uint16_t intercept_cr_write;
    uint16_t intercept_dr_read;
    uint16_t intercept_dr_write;
    uint32_t intercept_exceptions;
    uint64_t intercept_io1;
    uint64_t intercept_io2;
    uint64_t intercept_msr1;
    uint64_t intercept_msr2;
    uint16_t tsc_scale;
    uint8_t  pad1[6];
    uint32_t tsc_offset;
    uint32_t pad1b;
    uint64_t int_ctl;
    uint64_t int_vector;
    uint64_t int_state;
    uint8_t  pad2[24];
    uint64_t exit_code;
    uint64_t exit_info1;
    uint64_t exit_info2;
    uint64_t exit_int_info;
    uint64_t nest_ctl;
    uint8_t  pad3[40];
    uint64_t avic_apic_bar;
    uint8_t  pad4[8];
    uint64_t guest_asid;
    uint8_t  pad5[104];
    uint32_t vmcb_clean;
    uint8_t  pad6[188];
} __attribute__((packed));

struct vmcb_segment {
    uint16_t selector;
    uint16_t attrib;
    uint32_t limit;
    uint64_t base;
} __attribute__((packed));

struct vmcb_state_save {
    struct vmcb_segment es, cs, ss, ds;
    struct vmcb_segment fs, gs;
    struct vmcb_segment gdtr, ldtr, idtr, tr;
    uint8_t  pad1[28];
    uint64_t cr0, cr2, cr3, cr4;
    uint64_t cr8;
    uint64_t efer;
    uint8_t  pad2[40];
    uint64_t xss;
    uint64_t cr0_saved;
    uint64_t cr4_saved;
    uint64_t dr6;
    uint64_t dr7;
    uint64_t rip;
    uint64_t rsp;
    uint64_t rax;
    uint64_t rflags;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t star, lstar, cstar, sfmask;
    uint64_t kernel_gs_base;
    uint64_t sysenter_cs, sysenter_esp, sysenter_eip;
    uint64_t cr3_saved;
    uint8_t pad3[72];
    uint64_t g_pat;
    uint64_t dbgctl;
    uint64_t br_from, br_to, last_exc_from, last_exc_to;
} __attribute__((packed));

struct vmcb {
    struct vmcb_control_area control;
    uint8_t  pad0[828];
    struct vmcb_state_save save;
} __attribute__((packed));

#endif
