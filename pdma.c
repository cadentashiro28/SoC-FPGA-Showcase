#include <stdint.h>
#include <sys/types.h>
#include "bsp.h"
#include "dmasg.h"
#include "lwip/ip4_addr.h"
#include "lwip/udp.h"
#include "print.h"
#include "pdma.h"
#include "ethernetif.h"
#include "frame_tx.h"
#include "lwip/ip_addr.h"


#define FRAME_BUF_BASE  0x3000000   // 48 MB, above memtx and PDMA_SRC/DST
#define PDMA_SRC    ((uint8_t *)0x2100000)
#define PDMA_DST    ((uint8_t *)0x2101000)
#define LEN         256

// Ring buffer
volatile struct dmasg_descriptor frame_ring_buf[RING_BUFFER_SIZE] __attribute__((aligned(64)));
#define frame_buf ((volatile uint32_t (*)[NUM_WORDS_VID]) FRAME_BUF_BASE)
int ring_index = 0;
int frame_idx = 0;

// Testing
uint32_t randDelay = 0;
uint32_t overloads = 0;

// Sends a single frame
void pdma_single_tx(struct udp_pcb* vid_pcb) {
    dmasg_interrupt_pending_clear(PDMA_BASE, PDMA_VID_CHANNEL, 0xFFFFFFFF);
    dmasg_output_memory(PDMA_BASE, PDMA_VID_CHANNEL, (u32)frame_buf[0], 64);
    dmasg_input_stream(PDMA_BASE, PDMA_VID_CHANNEL, 0, 1, 1);
    dmasg_interrupt_config(PDMA_BASE, PDMA_VID_CHANNEL, DMASG_CHANNEL_INTERRUPT_DESCRIPTOR_COMPLETION_MASK | DMASG_CHANNEL_INTERRUPT_CHANNEL_COMPLETION_MASK);
    dmasg_direct_start(PDMA_BASE, PDMA_VID_CHANNEL, FRAME_BYTE_SIZE, 0);
    write_u32(0x1, (IO_APB_SLAVE_1_INPUT + 0x8));
    uint64_t start = sys_now();
    uint64_t elapsed = 0;
    while(dmasg_busy(PDMA_BASE, PDMA_VID_CHANNEL))
    {
        elapsed = sys_now() - start;
        if (elapsed > 1000) {
            bsp_printf("dma timed out");
        }
    }
    write_u32(0x0, (IO_APB_SLAVE_1_INPUT + 0x8));
    flush_data_cache();

    frame_tx(vid_pcb, (uint32_t *) frame_buf[0], sizeof(frame_buf[0]));
}

// Sets up the linked list structure for the descriptors
void pdma_ring_buffer_init(void) {
    for (int i = 0; i < RING_BUFFER_SIZE; i++) {
        frame_ring_buf[i].control = (u32)(FRAME_BYTE_SIZE - 1);
        frame_ring_buf[i].from    = 0;
        frame_ring_buf[i].to      = (u32)frame_buf[i];
        frame_ring_buf[i].next    = (u32)&frame_ring_buf[(i + 1) % RING_BUFFER_SIZE];
        frame_ring_buf[i].status  = 0;
    }
    ring_index = 0;
    flush_data_cache();
}

// Call this to start the pdma stream + interrupts
uint64_t pdma_stream_start(void) {
    pdma_ring_buffer_init();
    dmasg_interrupt_pending_clear(PDMA_BASE, PDMA_VID_CHANNEL, 0xFFFFFFFF);
    pdma_vid_done = false;
    pdma_vid_overloaded = false;
    dmasg_output_memory(PDMA_BASE, PDMA_VID_CHANNEL, (u32)frame_buf[0], 64);
    dmasg_input_stream(PDMA_BASE, PDMA_VID_CHANNEL, 0, 1, 1);
    dmasg_interrupt_config(PDMA_BASE, PDMA_VID_CHANNEL, DMASG_CHANNEL_INTERRUPT_DESCRIPTOR_COMPLETION_MASK | DMASG_CHANNEL_INTERRUPT_CHANNEL_COMPLETION_MASK);
    dmasg_linked_list_start(PDMA_BASE, PDMA_VID_CHANNEL, (u32)&frame_ring_buf[0]);
    write_u32(0x1, (IO_APB_SLAVE_1_INPUT + 0x8)); // Start fake camera (Data Source)
    return sys_now();
}


void pdma_stream_stats_restart(void) {
    overloads = 0;
}

void pdma_stream_capture(struct udp_pcb* vid_pcb) {
    flush_data_cache();
    while (frame_ring_buf[ring_index].status & DMASG_DESCRIPTOR_STATUS_COMPLETED) {      
        volatile uint32_t *buf = frame_buf[ring_index];
        // if (frame_idx % 30 == 0) bsp_Delay(randDelay); 
        frame_tx(vid_pcb, (uint32_t *) buf, FRAME_BYTE_SIZE);
        frame_ring_buf[ring_index].status = 0;
        ring_index = (ring_index + 1) % RING_BUFFER_SIZE;
        frame_idx++;
    }
}

// Closes the pdma RX channel, has a timeout incase the dma does
// not shut down properly
// Should be called after a request from client ends to free up dma engine
void pdma_stop(uint64_t stream_start_time) {
    write_u32(0x0, (IO_APB_SLAVE_1_INPUT + 0x8));
    uint64_t stream_stop_time = sys_now();
    dmasg_interrupt_config(PDMA_BASE, PDMA_VID_CHANNEL, 0);   // before dmasg_stop
    dmasg_stop(PDMA_BASE, PDMA_VID_CHANNEL);
    flush_data_cache();
    bsp_Delay(1);
    int timeout = 1000000;
    while (dmasg_busy(PDMA_BASE, PDMA_VID_CHANNEL)) {
        if (--timeout == 0) {
            bsp_printf("PDMA ch%d stop timed out\n\r", (int)PDMA_VID_CHANNEL);
            return;
        }
    }
    for (int i = 0; i < RING_BUFFER_SIZE; i++) {
        frame_ring_buf[i].status = 0;
    }
    dmasg_interrupt_pending_clear(PDMA_BASE, PDMA_VID_CHANNEL, 0xFFFFFFFF);
    pdma_vid_overloaded = false; 
    pdma_vid_done = false;
    bsp_printf("%d, overloads \n\r", overloads);
    pdma_stream_stats_restart();
    uint64_t expected_frames = 30 * (stream_stop_time - stream_start_time) / 1000;
    bsp_printf("%d expected frames, cpu received %d frames \n\r", expected_frames, frame_idx + 1);
}

