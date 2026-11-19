#include "lwip/pbuf.h"
#include "pdma.h"
#include "ethernetif.h"
#include "frame_tx.h"

#define CHUNK_DATA_SIZE ((1500 - 20 - 8 - sizeof(struct frame_chunk)) & ~3u)  // 1464
#define FRAME_CHUNKS_SIZE (int) (FRAME_BYTE_SIZE / CHUNK_DATA_SIZE)

extern int frame_idx;

struct frame_chunk {
    uint16_t chunk_idx;
    uint16_t frame_idx;
    uint16_t chunk_size; // byte size of the total frame, helps PC identify last chunk
} __attribute__((packed));

struct udp_pcb* init_udp(ip_addr_t ipaddr, uint16_t port) {
    struct udp_pcb *video_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (video_pcb == NULL) {
        bsp_printf("Couldn't intiialize Video Stream UDP Link");
        return NULL;
    }

    udp_bind(video_pcb, IP_ADDR_ANY, port);
    udp_connect(video_pcb, &ipaddr, port);
    return video_pcb;
}

void send_chunk_udp(struct udp_pcb *pcb, uint32_t index, uint32_t *frame_data, uint16_t current_chunk_size) {
    uint16_t pbuf_size = sizeof(struct frame_chunk) + current_chunk_size;
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, pbuf_size, PBUF_RAM);
    if (p != NULL) {
        struct frame_chunk header;
        header.chunk_idx = index;
        header.frame_idx = frame_idx;
        header.chunk_size = current_chunk_size;

        pbuf_take(p, &header, sizeof(struct frame_chunk));
        pbuf_take_at(p, frame_data, current_chunk_size, sizeof(struct frame_chunk));
        udp_send(pcb, p);
        pbuf_free(p);
    }
}

void frame_tx(struct udp_pcb *udp_pcb, uint32_t *frame_data, uint32_t total_frame_size) {
    uint32_t bytes_left = total_frame_size;
    uint32_t chunk_index = 0;
    uint32_t *frame_ptr = frame_data;

    while (bytes_left > 0) {
        uint16_t current_chunk_size = (bytes_left >= CHUNK_DATA_SIZE) ? CHUNK_DATA_SIZE : bytes_left;
        send_chunk_udp(udp_pcb, chunk_index, frame_ptr, current_chunk_size);
        frame_ptr += current_chunk_size / 4;
        chunk_index++;
        bytes_left -= current_chunk_size;
    }
}




