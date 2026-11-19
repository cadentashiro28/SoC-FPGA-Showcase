// write to 0x0, write 0x01 in that address
// read from 0x04, read 4 bytes from there
// *volatile  
#include "axi.h"
#define AXI_OFFSET 0x01200000
#define AXI_BASE  (SYSTEM_AXI_A_BMB + AXI_OFFSET) // 0x01200000 is the base address of AXI 3
#define REG_CONTROL (AXI_BASE + 0x00)
#define REG_STATUS  (AXI_BASE + 0x04)

// write 32-bit value to the register at 0x00
void axi_write(uint32_t val) {
    write_u32(val, REG_CONTROL);
}

// read + return 32-bit value at 0x04
uint32_t axi_read(void) {
    return read_u32(REG_STATUS);
}
