MEMORY
{
    dram0_0_seg (RWX) :  org = 0xc2000000, len = 0x4000000
    dram0_1_seg (RWX) :  org = 0xe0000000, len = 0x10000000
    /* VA = 0xe0000000 + type*1MiB; type 0x8d unused on this dump */
    dram0_2_seg (RWX) :  org = 0xe8d00000, len = 0x100000
    magic_info_seg (R) : org = 0xffff0000, len = 0x100
}

INCLUDE configuration_riscv.lds

ENTRY(CANBUS_MInit)
INCLUDE section.lds
