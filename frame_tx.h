#include <stdint.h>
#include "lwip/udp.h"

struct udp_pcb* init_udp(ip_addr_t ipaddr, uint16_t port);
void frame_tx(struct udp_pcb *udp_pcb, uint32_t *frame_data, uint32_t total_frame_size);
