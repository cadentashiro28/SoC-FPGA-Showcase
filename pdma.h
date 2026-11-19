#include <stdint.h>
#include <stdio.h>
#include "bsp.h"
#include "soc.h"
#include "dmasg.h"
#include "lwip/udp.h"

#define PDMA_BASE   IO_APB_SLAVE_2_INPUT
#define PDMA_RX_CHANNEL 0
#define PDMA_TX_CHANNEL 1
#define PDMA_VID_CHANNEL 2

#define RING_BUFFER_SIZE 4
#define NUM_WORDS_VID 648000 // subject to change based on the size of each frame
#define FRAME_BYTE_SIZE (NUM_WORDS_VID * 4)
#define VIDEO_PORT  5001

// Testing Delay variable
extern uint32_t randDelay;
extern volatile struct dmasg_descriptor frame_ring_buf[RING_BUFFER_SIZE] __attribute__((aligned(64)));
extern int ring_index;

extern volatile bool pdma_vid_done;       
extern volatile bool pdma_vid_overloaded;  
extern uint32_t overloads;

extern u64 sys_now(void);

void pdma_single_tx(struct udp_pcb* vid_pcb);
void pdma_ring_buffer_init(void);
void pdma_stream_stats_restart(void);
uint64_t pdma_stream_start(void);
void pdma_stream_capture(struct udp_pcb* vid_pcb);
void pdma_stop(uint64_t stream_start_time);
