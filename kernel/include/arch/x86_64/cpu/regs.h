#pragma once

#define CR0_PE         0x00000001
#define CR0_MP         0x00000002
#define CR0_EM         0x00000004
#define CR0_TS         0x00000008
#define CR0_ET         0x00000010
#define CR0_NE         0x00000020
#define CR0_WP         0x00010000
#define CR0_AM         0x00040000
#define CR0_NW         0x20000000
#define CR0_CD         0x40000000
#define CR0_PG         0x80000000
#define CR4_VME        0x00000001
#define CR4_PVI        0x00000002
#define CR4_TSD        0x00000004
#define CR4_DE         0x00000008
#define CR4_PSE        0x00000010
#define CR4_PAE        0x00000020
#define CR4_MCE        0x00000040
#define CR4_PGE        0x00000080
#define CR4_PCE        0x00000100
#define CR4_OSFXSR     0x00000200
#define CR4_OSXMMEXCPT 0x00000400
#define CR4_UMIP       0x00000800
#define CR4_LA57       0x00001000
#define CR4_VMXE       0x00002000
#define CR4_SMXE       0x00004000
#define CR4_FSGSBASE   0x00010000
#define CR4_PCIDE      0x00020000
#define CR4_OSXSAVE    0x00040000
#define CR4_SMEP       0x00100000
#define CR4_SMAP       0x00200000
#define CR4_PKE        0x00400000

#define EFER_SCE 0x00000001
#define EFER_LME 0x00000100
#define EFER_LMA 0x00000400
#define EFER_NXE 0x00000800

#define MSR_PLATFORM_ID               0x00000017
#define MSR_APIC_BASE                 0x0000001b
#define MSR_TSC_ADJUST                0x0000003b
#define MSR_SPEC_CTRL                 0x00000048
#define SPEC_CTRL_IBRS                (1ull << 0)
#define SPEC_CTRL_STIBP               (1ull << 1)
#define SPEC_CTRL_SSBD                (1ull << 2)
#define MSR_SMI_COUNT                 0x00000034
#define MSR_PRED_CMD                  0x00000049
#define MSR_BIOS_UPDT_TRIG            0x00000079u
#define MSR_BIOS_SIGN_ID              0x0000008b
#define MSR_MTRRCAP                   0x000000fe
#define MSR_ARCH_CAPABILITIES         0x0000010a
#define ARCH_CAPABILITIES_RDCL_NO     (1ull << 0)
#define ARCH_CAPABILITIES_IBRS_ALL    (1ull << 1)
#define ARCH_CAPABILITIES_RSBA        (1ull << 2)
#define ARCH_CAPABILITIES_SSB_NO      (1ull << 4)
#define ARCH_CAPABILITIES_MDS_NO      (1ull << 5)
#define ARCH_CAPABILITIES_TSX_CTRL    (1ull << 7)
#define ARCH_CAPABILITIES_TAA_NO      (1ull << 8)
#define MSR_FLUSH_CMD                 0x0000010b
#define MSR_TSX_CTRL                  0x00000122
#define TSX_CTRL_RTM_DISABLE          (1ull << 0)
#define TSX_CTRL_CPUID_DISABLE        (1ull << 1)
#define MSR_SYSENTER_CS               0x00000174
#define MSR_SYSENTER_ESP              0x00000175
#define MSR_SYSENTER_EIP              0x00000176
#define MSR_MCG_CAP                   0x00000179
#define MSR_MCG_STATUS                0x0000017a
#define MSR_MISC_ENABLE               0x000001a0
#define MSR_MISC_ENABLE_TURBO_DISABLE (1ull << 38)
#define MSR_TEMPERATURE_TARGET        0x000001a2
#define MSR_ENERGY_PERF_BIAS          0x000001b0
#define MSR_MTRR_PHYSBASE0            0x00000200
#define MSR_MTRR_PHYSMASK0            0x00000201
#define MSR_MTRR_PHYSMASK9            0x00000213
#define MSR_MTRR_DEF_TYPE             0x000002ff
#define MSR_MTRR_FIX64K_00000         0x00000250
#define MSR_MTRR_FIX16K_80000         0x00000258
#define MSR_MTRR_FIX16K_A0000         0x00000259
#define MSR_MTRR_FIX4K_C0000          0x00000268
#define MSR_MTRR_FIX4K_F8000          0x0000026f
#define MSR_PAT                       0x00000277
#define MSR_TSC_DEADLINE              0x000006e0

#define MSR_X2APIC_APICID  0x00000802
#define MSR_X2APIC_VERSION 0x00000803

#define MSR_X2APIC_TPR  0x00000808
#define MSR_X2APIC_PPR  0x0000080A
#define MSR_X2APIC_EOI  0x0000080B
#define MSR_X2APIC_LDR  0x0000080D
#define MSR_X2APIC_SIVR 0x0000080F

#define MSR_X2APIC_ISR0 0x00000810
#define MSR_X2APIC_ISR1 0x00000811
#define MSR_X2APIC_ISR2 0x00000812
#define MSR_X2APIC_ISR3 0x00000813
#define MSR_X2APIC_ISR4 0x00000814
#define MSR_X2APIC_ISR5 0x00000815
#define MSR_X2APIC_ISR6 0x00000816
#define MSR_X2APIC_ISR7 0x00000817

#define MSR_X2APIC_TMR0 0x00000818
#define MSR_X2APIC_TMR1 0x00000819
#define MSR_X2APIC_TMR2 0x0000081A
#define MSR_X2APIC_TMR3 0x0000081B
#define MSR_X2APIC_TMR4 0x0000081C
#define MSR_X2APIC_TMR5 0x0000081D
#define MSR_X2APIC_TMR6 0x0000081E
#define MSR_X2APIC_TMR7 0x0000081F

#define MSR_X2APIC_IRR0 0x00000820
#define MSR_X2APIC_IRR1 0x00000821
#define MSR_X2APIC_IRR2 0x00000822
#define MSR_X2APIC_IRR3 0x00000823
#define MSR_X2APIC_IRR4 0x00000824
#define MSR_X2APIC_IRR5 0x00000825
#define MSR_X2APIC_IRR6 0x00000826
#define MSR_X2APIC_IRR7 0x00000827

#define MSR_X2APIC_ESR 0x00000828
#define MSR_X2APIC_ICR 0x00000830

#define MSR_X2APIC_LVT_CMCI    0x0000082F
#define MSR_X2APIC_LVT_TIMER   0x00000832
#define MSR_X2APIC_LVT_THERMAL 0x00000833
#define MSR_X2APIC_LVT_PMI     0x00000834
#define MSR_X2APIC_LVT_LINT0   0x00000835
#define MSR_X2APIC_LVT_LINT1   0x00000836
#define MSR_X2APIC_LVT_ERROR   0x00000837

#define MSR_X2APIC_INIT_COUNT 0x00000838
#define MSR_X2APIC_CUR_COUNT  0x00000839
#define MSR_X2APIC_DIV_CONF   0x0000083E

#define MSR_X2APIC_SELF_IPI 0x0000083F

#define MSR_EFER 0xc0000080

#define MSR_STAR  0xc0000081
#define MSR_LSTAR 0xc0000082
#define MSR_CSTAR 0xc0000083
#define MSR_FMASK 0xc0000084

#define MSR_FS_BASE        0xc0000100
#define MSR_GS_BASE        0xc0000101
#define MSR_KERNEL_GS_BASE 0xc0000102

#define MSR_TSC_AUX 0xc0000103

#define MSR_PM_ENABLE               0x00000770
#define MSR_HWP_CAPABILITIES        0x00000771
#define MSR_HWP_REQUEST             0x00000774
#define MSR_POWER_CTL               0x000001fc
#define MSR_RAPL_POWER_UNIT         0x00000606
#define MSR_PKG_POWER_LIMIT         0x00000610
#define MSR_PKG_ENERGY_STATUS       0x00000611
#define MSR_PKG_POWER_INFO          0x00000614
#define MSR_DRAM_POWER_LIMIT        0x00000618
#define MSR_DRAM_ENERGY_STATUS      0x00000619
#define MSR_PP0_POWER_LIMIT         0x00000638
#define MSR_PP0_ENERGY_STATUS       0x00000639
#define MSR_PP1_POWER_LIMIT         0x00000640
#define MSR_PP1_ENERGY_STATUS       0x00000641
#define MSR_PLATFORM_ENERGY_COUNTER 0x0000064d
#define MSR_PPERF                   0x0000064e
#define MSR_PERF_LIMIT_REASONS      0x0000064f
#define MSR_GFX_PERF_LIMIT_REASONS  0x000006b0
#define MSR_PLATFORM_POWER_LIMIT    0x0000065c

#define MSR_AMD_VIRT_SPEC_CTRL 0xc001011f
#define MSR_AMD_F10_DE_CFG     0xc0011029
#define MSR_AMD_LS_CFG         0xc0011020
#define MSR_K7_HWCR            0xc0010015

#define MSR_AMD_F10_DE_CFG_LFENCE_SERIALIZE (1 << 1)
#define AMD_LS_CFG_F15H_SSBD                (1ull << 54)
#define AMD_LS_CFG_F16H_SSBD                (1ull << 33)
#define AMD_LS_CFG_F17H_SSBD                (1ull << 10)
#define MSR_K7_HWCR_CPB_DISABLE             (1ull << 25)

#define MSR_KVM_PV_EOI_EN        0x4b564d04
#define MSR_KVM_PV_EOI_EN_ENABLE (1ull << 0)

#define FLAGS_CF            (1 << 0)
#define FLAGS_PF            (1 << 2)
#define FLAGS_AF            (1 << 4)
#define FLAGS_ZF            (1 << 6)
#define FLAGS_SF            (1 << 7)
#define FLAGS_TF            (1 << 8)
#define FLAGS_IF            (1 << 9)
#define FLAGS_DF            (1 << 10)
#define FLAGS_OF            (1 << 11)
#define FLAGS_STATUS_MASK   (0xfff)
#define FLAGS_IOPL_MASK     (3 << 12)
#define FLAGS_IOPL_SHIFT    (12)
#define FLAGS_NT            (1 << 14)
#define FLAGS_RF            (1 << 16)
#define FLAGS_VM            (1 << 17)
#define FLAGS_AC            (1 << 18)
#define FLAGS_VIF           (1 << 19)
#define FLAGS_VIP           (1 << 20)
#define FLAGS_ID            (1 << 21)
#define FLAGS_RESERVED_ONES 0x2
#define FLAGS_RESERVED      0xffc0802a
#define FLAGS_USER                                                                           \
    (FLAGS_CF | FLAGS_PF | FLAGS_AF | FLAGS_ZF | FLAGS_SF | FLAGS_TF | FLAGS_DF | FLAGS_OF | \
     FLAGS_NT | FLAGS_AC | FLAGS_ID)

#define DR6_B0 (1ul << 0)
#define DR6_B1 (1ul << 1)
#define DR6_B2 (1ul << 2)
#define DR6_B3 (1ul << 3)
#define DR6_BD (1ul << 13)
#define DR6_BS (1ul << 14)
#define DR6_BT (1ul << 15)

#define DR6_USER_MASK (DR6_B0 | DR6_B1 | DR6_B2 | DR6_B3 | DR6_BD | DR6_BS | DR6_BT)
#define DR6_MASK      (0xffff0ff0ul)

#define DR7_L0   (1ul << 0)
#define DR7_G0   (1ul << 1)
#define DR7_L1   (1ul << 2)
#define DR7_G1   (1ul << 3)
#define DR7_L2   (1ul << 4)
#define DR7_G2   (1ul << 5)
#define DR7_L3   (1ul << 6)
#define DR7_G3   (1ul << 7)
#define DR7_LE   (1ul << 8)
#define DR7_GE   (1ul << 9)
#define DR7_GD   (1ul << 13)
#define DR7_RW0  (3ul << 16)
#define DR7_LEN0 (3ul << 18)
#define DR7_RW1  (3ul << 20)
#define DR7_LEN1 (3ul << 22)
#define DR7_RW2  (3ul << 24)
#define DR7_LEN2 (3ul << 26)
#define DR7_RW3  (3ul << 28)
#define DR7_LEN3 (3ul << 30)

#define DR7_USER_MASK                                                                             \
    (DR7_L0 | DR7_G0 | DR7_L1 | DR7_G1 | DR7_L2 | DR7_G2 | DR7_L3 | DR7_G3 | DR7_RW0 | DR7_LEN0 | \
     DR7_RW1 | DR7_LEN1 | DR7_RW2 | DR7_LEN2 | DR7_RW3 | DR7_LEN3)

#define DR7_MASK ((1ul << 10) | DR7_LE | DR7_GE)

#define HW_DEBUG_REGISTERS_COUNT 4