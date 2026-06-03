#ifndef SVMCPU_H
#define SVMCPU_H

#include <stdint.h>

#define SVM_CPUID_FN        0x8000000A
#define SVM_CPUID_ECX_NPT   0
#define SVM_CPUID_ECX_LBR   1
#define SVM_CPUID_ECX_SVML  2
#define SVM_CPUID_ECX_NRIPS 3
#define SVM_CPUID_ECX_TSC   4
#define SVM_CPUID_ECX_VMCB 5
#define SVM_CPUID_ECX_VLS   6

#define MSR_EFER         0xC0000080
#define EFER_SVME        (1ULL << 12)
#define EFER_NXE         (1ULL << 11)

#define MSR_VM_CR        0xC0010114
#define VM_CR_SVM_DIS    (1ULL << 4)
#define VM_CR_SVM_LOCK   (1ULL << 3)
#define VM_CR_INIT_RD    (1ULL << 1)
#define VM_CR_INIT_WR    (1ULL << 0)

#define MSR_VM_HSAVE_PA  0xC0010117

struct svm_cpuid_result {
    uint32_t eax, ebx, ecx, edx;
};

static inline struct svm_cpuid_result svm_cpuid(uint32_t leaf, uint32_t subleaf) {
    struct svm_cpuid_result ret;
    asm volatile("cpuid"
                 : "=a"(ret.eax), "=b"(ret.ebx), "=c"(ret.ecx), "=d"(ret.edx)
                 : "a"(leaf), "c"(subleaf));
    return ret;
}

static inline uint64_t svm_read_msr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void svm_write_msr(uint32_t msr, uint64_t val) {
    asm volatile("wrmsr" : : "a"((uint32_t)val), "d"((uint32_t)(val >> 32)), "c"(msr));
}

#define VMCB_INTERCEPT_CR0_READ  (1ULL << 0)
#define VMCB_INTERCEPT_CR1_READ  (1ULL << 1)
#define VMCB_INTERCEPT_CR2_READ  (1ULL << 2)
#define VMCB_INTERCEPT_CR3_READ  (1ULL << 3)
#define VMCB_INTERCEPT_CR4_READ  (1ULL << 4)
#define VMCB_INTERCEPT_CR5_READ  (1ULL << 5)
#define VMCB_INTERCEPT_CR6_READ  (1ULL << 6)
#define VMCB_INTERCEPT_CR7_READ  (1ULL << 7)
#define VMCB_INTERCEPT_CR8_READ  (1ULL << 8)
#define VMCB_INTERCEPT_CR9_READ  (1ULL << 9)
#define VMCB_INTERCEPT_CR10_READ (1ULL << 10)
#define VMCB_INTERCEPT_CR11_READ (1ULL << 11)
#define VMCB_INTERCEPT_CR12_READ (1ULL << 12)
#define VMCB_INTERCEPT_CR13_READ (1ULL << 13)
#define VMCB_INTERCEPT_CR14_READ (1ULL << 14)
#define VMCB_INTERCEPT_CR15_READ (1ULL << 15)
#define VMCB_INTERCEPT_CR0_WRITE  (1ULL << 16)
#define VMCB_INTERCEPT_CR1_WRITE  (1ULL << 17)
#define VMCB_INTERCEPT_CR2_WRITE  (1ULL << 18)
#define VMCB_INTERCEPT_CR3_WRITE  (1ULL << 19)
#define VMCB_INTERCEPT_CR4_WRITE  (1ULL << 20)
#define VMCB_INTERCEPT_CR5_WRITE  (1ULL << 21)
#define VMCB_INTERCEPT_CR6_WRITE  (1ULL << 22)
#define VMCB_INTERCEPT_CR7_WRITE  (1ULL << 23)
#define VMCB_INTERCEPT_CR8_WRITE  (1ULL << 24)
#define VMCB_INTERCEPT_CR9_WRITE  (1ULL << 25)
#define VMCB_INTERCEPT_CR10_WRITE (1ULL << 26)
#define VMCB_INTERCEPT_CR11_WRITE (1ULL << 27)
#define VMCB_INTERCEPT_CR12_WRITE (1ULL << 28)
#define VMCB_INTERCEPT_CR13_WRITE (1ULL << 29)
#define VMCB_INTERCEPT_CR14_WRITE (1ULL << 30)
#define VMCB_INTERCEPT_CR15_WRITE (1ULL << 31)

struct __attribute__((packed, aligned(4))) svm_vmcb_ctrl {
    uint16_t intercept_cr_read;
    uint16_t intercept_cr_write;
    uint16_t intercept_dr_read;
    uint16_t intercept_dr_write;
    uint32_t intercept_exceptions;
    uint64_t intercept_io1;
    uint64_t intercept_io2;
    uint64_t intercept_msr1;
    uint64_t intercept_msr2;
    uint8_t  tsc_offset[8];
    uint64_t intercept_io_perms;
    uint64_t intercept_msr_perms;
    uint16_t tsc_scale;
    uint8_t  pad1[6];
    uint32_t tsc_offset2;
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
    uint64_t tsc_offset3;
    uint8_t  pad6[184];
    uint32_t vmcb_clean;
    uint8_t  pad7[4];
};

struct __attribute__((packed)) svm_vmcb_seg {
    uint16_t selector;
    uint16_t attrib;
    uint32_t limit;
    uint64_t base;
};

struct __attribute__((packed)) svm_vmcb_save {
    struct svm_vmcb_seg es;
    struct svm_vmcb_seg cs;
    struct svm_vmcb_seg ss;
    struct svm_vmcb_seg ds;
    struct svm_vmcb_seg fs;
    struct svm_vmcb_seg gs;
    struct svm_vmcb_seg gdtr;
    struct svm_vmcb_seg ldtr;
    struct svm_vmcb_seg idtr;
    struct svm_vmcb_seg tr;
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
    uint8_t  pad3[72];
    uint64_t g_pat;
    uint64_t dbgctl;
    uint64_t br_from, br_to, last_exc_from, last_exc_to;
};

struct __attribute__((packed)) svm_vmcb {
    struct svm_vmcb_ctrl ctrl;
    uint8_t pad1[832];
    struct svm_vmcb_save save;
};

#define VMCB_EXIT_CR0_READ   0
#define VMCB_EXIT_CR0_WRITE  0x60
#define VMCB_EXIT_CPUID      0x72
#define VMCB_EXIT_HLT        0x78
#define VMCB_EXIT_INVLPGA    0x7D
#define VMCB_EXIT_VMMCALL    0x81
#define VMCB_EXIT_MSR        0x7C
#define VMCB_EXIT_NPF        0x400

#endif
