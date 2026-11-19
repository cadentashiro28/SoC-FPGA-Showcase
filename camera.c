#include <stdint.h>
#include "bsp.h"
#include "i2c.h"
#include "riscv.h"
#include "clint.h"
#include "plic.h"
#include "userDef.h"
#include "camera.h"

#define CAM_ADDR        0x10    // IMX219 7-bit I2C address
#define I2C_FREQUENCY   100000  // 100 kHz

#define MODE_LEN (sizeof(mode_1920_1080_regs) / sizeof(struct imx219_reg))
static const struct imx219_reg mode_1920_1080_regs[] = {
	{0x0100, 0x00},
	{0x30eb, 0x05},
	{0x30eb, 0x0c},
	{0x300a, 0xff},
	{0x300b, 0xff},
	{0x30eb, 0x05},
	{0x30eb, 0x09},
	{0x0114, 0x01},
	{0x0128, 0x00},
	{0x012a, 0x18},
	{0x012b, 0x00},
	{0x0162, 0x0d},
	{0x0163, 0x78},
	{0x0164, 0x02},
	{0x0165, 0xa8},
	{0x0166, 0x0a},
	{0x0167, 0x27},
	{0x0168, 0x02},
	{0x0169, 0xb4},
	{0x016a, 0x06},
	{0x016b, 0xeb},
	{0x016c, 0x07},
	{0x016d, 0x80},
	{0x016e, 0x04},
	{0x016f, 0x38},
	{0x0170, 0x01},
	{0x0171, 0x01},
	{0x0174, 0x00},
	{0x0175, 0x00},
	{0x018c, 0x0a},
	{0x018d, 0x0a},
	{0x0301, 0x05},
	{0x0303, 0x01},
	{0x0304, 0x03},
	{0x0305, 0x03},
	{0x0306, 0x00},
	{0x0307, 0x39},
	{0x0309, 0x0a},
	{0x030b, 0x01},
	{0x030c, 0x00},
	{0x030d, 0x72},
	{0x455e, 0x00},
	{0x471e, 0x4b},
	{0x4767, 0x0f},
	{0x4750, 0x14},
	{0x4540, 0x00},
	{0x47b4, 0x14},
	{0x4713, 0x30},
	{0x478b, 0x10},
	{0x478f, 0x10},
	{0x4793, 0x10},
	{0x4797, 0x0e},
	{0x479b, 0x0e},

	{0x0172, 0x03},
	{0x0162, 0x0d},
	{0x0163, 0x78},
};

void camera_i2c_write_8_bits(uint16_t regAddr, uint8_t value) {
    uint8_t send = value & 0xFF;
    i2c_writeData_w_ack(I2C_CTRL, CAM_ADDR << 1, regAddr, &send, 1);
}

void camera_i2c_write_16_bits(uint16_t regAddr, uint16_t value) {
    uint8_t send[2];
    send[0] = value >> 8;
    send[1] = value & 0xFF;
    i2c_writeData_w_ack(I2C_CTRL, CAM_ADDR << 1, regAddr, send, 2);
}


void i2c_init(){
    I2c_Config i2c;
    i2c.samplingClockDivider    = 3;
    i2c.timeout                 = I2C_CTRL_HZ/10;               // 0.1 s timeout
    i2c.tsuDat                  = I2C_CTRL_HZ/I2C_FREQUENCY/3;
    i2c.tLow                    = I2C_CTRL_HZ/I2C_FREQUENCY/2;
    i2c.tHigh                   = I2C_CTRL_HZ/I2C_FREQUENCY/2;
    i2c.tBuf                    = I2C_CTRL_HZ/I2C_FREQUENCY;

    i2c_applyConfig(I2C_CTRL, &i2c);
    i2c_enableInterrupt(I2C_CTRL, I2C_INTERRUPT_DROP);
    plic_set_enable(BSP_PLIC, BSP_PLIC_CPU_0, I2C_CTRL_PLIC_INTERRUPT, 1);
    plic_set_priority(BSP_PLIC, I2C_CTRL_PLIC_INTERRUPT, 1);
}

uint8_t camera_id_check() {
    bsp_printf("IMX219 chip ID test\r\n");

    u8 slaveAddr = (CAM_ADDR << 1) & 0xFF;   // shift left: low bit is read/write
    u8 id[2] = {0, 0};

    // Chip ID lives in 16-bit registers 0x0000 (high byte) and 0x0001 (low byte)
    i2c_readData_w_ack(I2C_CTRL, slaveAddr, 0x0000, id, 2);

    bsp_printf("Chip ID: 0x%x%02x (expect 0x219)\r\n", id[0], id[1]);
    if (id[0] == 0x02 && id[1] == 0x19) {
        bsp_printf("PASS: camera is answering over I2C\r\n");
        return 1;
    } else {
        bsp_printf("FAIL: unexpected ID\r\n");
        return 0;
    }
}

void i2c_intc(){
    if (i2c_getInterruptFlag(I2C_CTRL) & I2C_INTERRUPT_DROP)
        bsp_printf("I2C Transfer is dropped due to timeout! (camera did not answer)\r\n");
    else
        bsp_printf("I2C Interrupt is triggered!\r\n");

    i2c_clearInterruptFlag(I2C_CTRL, I2C_INTERRUPT_DROP);
}

void camera_init() {
    bsp_printf("Initializing camera startup over I2C: Programming camera. \r\n");
    uint8_t id_pass = camera_id_check(); 
    if (id_pass == 0) {
        bsp_printf("Exited programming because I2C ID was unexpected \r\n");
        return; 
    }

    // General 1080p setup registry
    for (int i = 0; i < MODE_LEN; i++) {
        camera_i2c_write_8_bits(mode_1920_1080_regs[i].addr, mode_1920_1080_regs[i].val);
    }

    // VTS
    camera_i2c_write_16_bits(IMX219_REG_VTS, IMX219_VTS_30FPS_1080P);

    // User Controls
    camera_i2c_write_16_bits(IMX219_REG_EXPOSURE, IMX219_EXPOSURE_DEFAULT);
    camera_i2c_write_8_bits(IMX219_REG_ANALOG_GAIN, IMX219_ANA_GAIN_DEFAULT);
    camera_i2c_write_16_bits(IMX219_REG_DIGITAL_GAIN, IMX219_DGTL_GAIN_DEFAULT);
    
    // Test picture (CHANGE THIS WHEN WANT TO STREAM REAL)
    camera_i2c_write_16_bits(IMX219_REG_TEST_PATTERN, IMX219_TEST_PATTERN_COLOR_BARS);

    // Start stream
    camera_i2c_write_8_bits(IMX219_REG_MODE_SELECT, IMX219_MODE_STREAMING);
}