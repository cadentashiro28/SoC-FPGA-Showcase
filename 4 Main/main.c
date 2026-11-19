////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2013-2025 Efinix Inc. All rights reserved.              
// Full license header bsp/efinix/EfxSapphireSoc/include/LICENSE.MD
////////////////////////////////////////////////////////////////////////////////
#include <stdint.h>
#include <string.h>
#include "bsp.h"
#include "dmasg.h"
#include "lwip/err.h"
#include "lwip/udp.h"
#include "print.h"
#include "userDef.h"
// #include "prescaler.h"
// #include "timer.h"
#include "clint.h"
#include "riscv.h"
#include "plic.h"
#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
// #include "lwip/etharp.h"
// #include "lwip/icmp.h"
#include "ethernetif.h"
// #include "lwiperf.h"
#include "rtl8211fd.h"
#include "efx_tse_mac.h"
#include "ping.h"
#include "axi.h"
#include "pdma.h"
#include "frame_tx.h"
#include "camera.h"
#include "i2c.h"

void trap_entry();
/**************************** TSE **************************/
/*Static IP ADDRESS: IP_ADDR0.IP_ADDR1.IP_ADDR2.IP_ADDR3 */
 #define IP_ADDR0                    configIP_ADDR0
 #define IP_ADDR1                    configIP_ADDR1
 #define IP_ADDR2                    configIP_ADDR2
 #define IP_ADDR3                    configIP_ADDR3
 
 /*NETMASK*/
 #define NETMASK_ADDR0               255
 #define NETMASK_ADDR1               255
 #define NETMASK_ADDR2               255
 #define NETMASK_ADDR3                 0
 
 /*Gateway Address*/
 #define GW_ADDR0                    configIP_ADDR0
 #define GW_ADDR1                    configIP_ADDR1
 #define GW_ADDR2                    configIP_ADDR2
 #define GW_ADDR3                    1
 
 ip4_addr_t ipaddr;
 ip4_addr_t netmask;
 ip4_addr_t gw;
 ip4_addr_t client_addr;

/******************************************************************/
#define TIMER_PRESCALER_CTRL            (TIMER_CTRL + 0x00)
#define TIMER_0_CTRL                    (TIMER_CTRL +  0x40)
#define TIMER_CONFIG_WITH_PRESCALER     0x2
#define TIMER_CONFIG_WITHOUT_PRESCALER  0x1
#define TIMER_CONFIG_SELF_RESTART       0x10000

/******************************************************************/

#define PWM_PERIOD_OFFSET 0x00
#define PWM_PULSE_WIDTH_OFFSET 0x04
#define UART_INPUT_BUFFER_LENGTH 256
#define UART_MAX_ARGS 8
/*******************************************************UART & DMA-RELATED FUNCTIONS***************************************************/
struct netif gnetif;
int incoming_packet=0;

//Used on unexpected trap/interrupt codes
void crash(){
   int32_t mcause = csr_read(mcause);
   bsp_printf("\n*** CRASH *** mcause=0x%x interrupt=%d cause=%d\n",
              mcause, (mcause < 0) ? 1 : 0, mcause & 0xF);
   bsp_printf("mepc=0x%x mtval=0x%x\n", csr_read(mepc), csr_read(mtval));	 
   while(1);
}
volatile bool receivedPacket = false;

/* Video */
volatile bool pdma_vid_done = false;
volatile bool pdma_vid_overloaded = false;
bool streaming = false;
uint64_t stream_start_time = 0;

/* Ethernet Connection Variables */
#define CMD_IDENTIFIER "C: "
volatile bool tse_rx_pending = false;
ip_addr_t dest_ip;
uint16_t dest_port;
bool dest_valid = false;
struct udp_pcb *vid_pcb;

/* I2C */
volatile bool i2c_pending = false;

void isrRoutine(){
   uint32_t claim;
   // While there is pending interrupts
   while((claim = plic_claim(BSP_PLIC, BSP_PLIC_CPU_0))){
      switch(claim){
	  // TSE DMA INTERRUPT
			case TSE_RX_INTR:
				dmasg_interrupt_config(TSEMAC_DMASG_BASE, TSE_DMASG_RX_CH, DMASG_CHANNEL_INTERRUPT_LINKED_LIST_UPDATE_MASK);
				// ethernetif_input(&gnetif);
				tse_rx_pending = true;
				if (receivedPacket==false){
					bsp_printf("Recieved first packet\n\r");
					receivedPacket=true;
				}
				// dmasg_interrupt_config(TSEMAC_DMASG_BASE, TSE_DMASG_RX_CH, 1);
				// flush_data_cache();
				break;
			case PDMA_VID_INTR: {
				int ca = dmasg_ca(PDMA_BASE, PDMA_VID_CHANNEL);
				uint32_t pending = read_u32(ca + DMASG_CHANNEL_INTERRUPT_PENDING);
				dmasg_interrupt_pending_clear(PDMA_BASE, PDMA_VID_CHANNEL, pending);
				if (pending & DMASG_CHANNEL_INTERRUPT_CHANNEL_COMPLETION_MASK) {
					pdma_vid_overloaded = true;
				}
				pdma_vid_done = true;
				break;
			}
			case I2C_CTRL_PLIC_INTERRUPT: {
				i2c_intc();
				break;
			}
      default: crash(); break;
      }
      plic_release(BSP_PLIC, BSP_PLIC_CPU_0, claim); //unmask the claimed interrupt
   }
}

//Called by trap_entry on both exceptions and interrupts events
void trap(){
   int32_t mcause = csr_read(mcause);
   int32_t interrupt = mcause < 0; 
   int32_t cause     = mcause & 0xF;
   if(interrupt){
      switch(cause){
      case CAUSE_MACHINE_EXTERNAL: isrRoutine(); break;
      default: crash(); break;
      }
   } else {
      crash();
   }
}

void isrInit(){

	// RX FIFO not empty interrupt enable
   	uart_status_write(BSP_UART_TERMINAL,uart_status_read(BSP_UART_TERMINAL) | 0x02);   
   
   	// Configure PLIC
	// Cpu 0 accept all interrupts with priority above 0
   	plic_set_threshold(BSP_PLIC, BSP_PLIC_CPU_0, 0); 

   // Enable interrupts
   // Set the machine trap vector (../common/trap.S)
   csr_write(mtvec, trap_entry); 
   // Enable external interrupts
   csr_set(mie, MIE_MEIE);       
   csr_write(mstatus, csr_read(mstatus) | MSTATUS_MPP | MSTATUS_MIE);
}

void tse_rx_intr_enable(void){
    plic_set_enable(BSP_PLIC, BSP_PLIC_CPU_0, TSE_RX_INTR, 1);
    plic_set_priority(BSP_PLIC, TSE_RX_INTR, 1);
}

// void pdma_rx_intr_enable(void){
//     plic_set_enable(BSP_PLIC, BSP_PLIC_CPU_0, PDMA_RX_INTR, 1);
//     plic_set_priority(BSP_PLIC, PDMA_RX_INTR, 1);
// }

void pdma_vid_intr_enable(void){
	plic_set_enable(BSP_PLIC, BSP_PLIC_CPU_0, PDMA_VID_INTR, 1);
    plic_set_priority(BSP_PLIC, PDMA_VID_INTR, 1);
}

void additionalSetting(void){
	u32 Value = 0;
	Value = read_u32(TSEMAC_BASE+COMMAND_CONFIG);
	MacRxEn(1);
	MacTxEn(1);
	//mac_reg command_config Reg
	write_u32(0x00040053, (TSEMAC_BASE+COMMAND_CONFIG));
	Value = read_u32(TSEMAC_BASE+COMMAND_CONFIG);

	//set mac address
	write_u32(SRC_MAC_L, (TSEMAC_BASE+MAC_ADDR_LO));
	write_u32(SRC_MAC_H, (TSEMAC_BASE+MAC_ADDR_HI));

	//Set MDIO Divider
	write_u32(0xFF, (TSEMAC_BASE+DIVIDER_PRE));

	//set mac addr
	write_u32(0xFFFFFFFF, (TSEMAC_BASE+MAC_ADDR_MAKE_LO));
	write_u32(0x0000FFFF, (TSEMAC_BASE+MAC_ADDR_MAKE_HI));
}

u64_t sys_jiffies(void)
{
	u64 get_time;

	get_time = clint_getTime(BSP_CLINT);
    return ((get_time)/(BSP_CLINT_HZ/1000));
}

u64_t sys_now(void)
{
	u64 get_time;

	get_time = clint_getTime(BSP_CLINT);

	return ((get_time)/(BSP_CLINT_HZ/1000));

}
 
 void LwIP_Init(void)
 {
 
     IP4_ADDR(&ipaddr,IP_ADDR0,IP_ADDR1,IP_ADDR2,IP_ADDR3);
     IP4_ADDR(&netmask,NETMASK_ADDR0,NETMASK_ADDR1,NETMASK_ADDR2,NETMASK_ADDR3);
     IP4_ADDR(&gw,GW_ADDR0,GW_ADDR1,GW_ADDR2,GW_ADDR3); 
     /* Initilialize the LwIP stack without RTOS */
     lwip_init();
     /* add the network interface (IPv4/IPv6) without RTOS */
     netif_add(&gnetif, &ipaddr, &netmask, &gw, NULL,
               &ethernetif_init, &ethernet_input);
     /* Registers the default network interface */
     netif_set_default(&gnetif);
     if (netif_is_link_up(&gnetif))
     {
      	/*When the netif is fully configured this function must be called */
    	netif_set_up(&gnetif);

     }
     else
     {
         /* When the netif link is down this function must be called */
         netif_set_down(&gnetif);
     } 
 }
 
void clock_sel(int speed)
{
	int val=0;

	if(speed == TSE_Speed_1000Mhz)	
		val=0x03;
	else 
		val=0x00;
}

bool strEq(char* buffer, const char* string){
	for (int i=0; (buffer[i]!='\0'||buffer[i]!=' ')&&(string[i]!='\0'); i++){
		if (buffer[i]!=string[i]) return false;
	}
	return true;
}

int string_to_int(char * arg, int base){
    float res = 0;
    uint8_t negative = *arg=='-';
    if (arg[negative]=='0'){
        if (arg[negative+1]=='b'){
            base=2;
        } else if (arg[negative+1]=='x'){
            base=16;
        }
    }
    int curr_num=0;
    for (int i=negative+2*(arg[negative]=='0'); arg[i]!='\0'&&arg[i]!=','&&arg[i]!=' '; i++){
            curr_num=(int)arg[i]-(int)'0';
            if ((base>10)&&(curr_num>9)){
                if (curr_num>9){
                    curr_num-='A'-'9'-1; // Convert from capitol letters to decimal
                    if (curr_num>16){
                        curr_num-='a'-'A'; // Convert from upper case to lower case
                    }
                }
            }
            if (curr_num>=base || curr_num<0){
                bsp_printf("ERROR: %s contains character %c which is outside of %d\n", arg, arg[i], base);
                return 0;
            }
            res*=base;
            res+=curr_num;
    }
    return (negative ? -1*res : res);
}

int strLength(char* buffer){
	int i=0;
	while(buffer[i++]!='\0');
	return i;
}
int numSpaces(char* buffer){
	int count = 0;
	for (int i=0; buffer[i]!='\0'; buffer++){
		if (buffer[i]==' ') count++;
	}
	return count;
}

void interpretUART(char* buffer){
	char argv[UART_MAX_ARGS][UART_INPUT_BUFFER_LENGTH];
	int argc=0;
	int currentArg=0;
	int currentChar=0;
	bool space=false;
	for (int i=0; buffer[i]!='\0'&&(currentArg<UART_MAX_ARGS)&&(currentChar<UART_INPUT_BUFFER_LENGTH); i++){
		if (buffer[i]==' '){
			if (!space){
				argv[currentArg][currentChar]='\0';
				currentArg++;
				currentChar=0;
				space=true;
			}
		} else {
			space=false;
			argv[currentArg][currentChar]=buffer[i];
			currentChar++;
		}
	}
	
	argv[currentArg][currentChar]='\0';
	argc=currentArg+1;
	if (strEq(argv[0], "pwm")){
		if (argc == 1) {
			bsp_printf("pwm | uptime | period \r\n");
		}
		if (argc>1){
			int uptime = string_to_int(argv[1], 10);
			write_u32(uptime, (IO_APB_SLAVE_1_INPUT + PWM_PULSE_WIDTH_OFFSET));
		}
		if (argc>2){
			int period = string_to_int(argv[2], 10);
			write_u32(period, (IO_APB_SLAVE_1_INPUT + PWM_PERIOD_OFFSET));
		}
	}
	// bsp_printf("Finished arguments\n\r");
	else if (strEq(argv[0], "reset")){
		rtl8211_drv_wrdata(0, (rtl8211_drv_rddata(0) & 0xFFFF) | 0x1200); // autoneg enable + restart
	}

	else if (strEq(argv[0], "axi")){
		if(argc == 1){
			bsp_printf("axi <write/read> <value>\n\r");
		}
		if(argc>1){
			if (strEq(argv[1], "write")){
				if (argc>2){
					int val = string_to_int(argv[2], 16);
					axi_write(val);
					bsp_printf("Wrote 0x%x to AXI\n\r", val);
				} else {
					bsp_printf("axi write <value>\n\r");
				}
			} else if (strEq(argv[1], "read")){
				int val = axi_read();
				bsp_printf("Read 0x%x from AXI\n\r", val);
			} else {
				bsp_printf("Unknown command %s\n\r", argv[1]);
			}
		}

	}
	
	else if (strEq(argv[0], "stream")) {
		if (streaming) {
			streaming = false;
			udp_remove(vid_pcb);
			vid_pcb = NULL;
			pdma_stop(stream_start_time);
			return;
		}

		if (dest_valid) {
			vid_pcb = init_udp(dest_ip, VIDEO_PORT);
			stream_start_time = pdma_stream_start();
			streaming = true;
		} else {
			bsp_printf("Invalid Destination IP\n\r");
		}
	}

	else if (strEq(argv[0], "screenshot")) {
		if (dest_valid && !streaming) {
			vid_pcb = init_udp(dest_ip, VIDEO_PORT);
			pdma_single_tx(vid_pcb);
			udp_remove(vid_pcb);
			vid_pcb = NULL;
		} else if (streaming) {
			bsp_printf("End stream before attempting screenshot.");
		} else {
			bsp_printf("Invalid Destination IP\n\r");
		}
	}

	else if (strEq(argv[0], "dstream")) {
		randDelay = atoi(argv[1]);
	}
	
	else if (strEq(argv[0], "test")){
		bsp_printf("Tested\n\r");
	}
}	

static void on_command(void *arg, struct udp_pcb *pcb,
	struct pbuf *p, const ip_addr_t *addr, u16_t port)
	{
	bsp_printf("on_command triggered\n\r");
	ip_addr_copy(dest_ip, *addr);
	dest_port = port;
	dest_valid = true;
	struct pbuf *q = p;
	if (q != NULL && !pbuf_memcmp(q, 0, CMD_IDENTIFIER, strlen(CMD_IDENTIFIER))) {
		uint8_t cmd[256];
		uint16_t n = pbuf_copy_partial(q, cmd, sizeof(cmd) - 1, strlen(CMD_IDENTIFIER));
		cmd[n] = '\0';
		interpretUART((char *) cmd);
	}
	// PRINT OUT THE ENTIRE PBUF
	// for (q = p; q != NULL; q = q->next) {
	// 	uint8_t* payload = (uint8_t*) q->payload;
	// 	for (int i = 0; i < q->len; i++) {
	// 		bsp_printf("%c", payload[i]);
	// 	}
	// }
	// bsp_printf("\n\r");
	// bsp_printf("Finished Printing Pbuf \n\r");
	pbuf_free(p);

	struct pbuf *ack = pbuf_alloc(PBUF_TRANSPORT, 4, PBUF_RAM);
	memcpy(ack->payload, "ACK\0", 4);
	udp_sendto(pcb, ack, addr, port);
	pbuf_free(ack);
}


/****************************************************************MAIN**************************************************************/
int main() {
    mdio_wait_idle();
	write_u32(0,TSEMAC_BASE + COMMAND_CONFIG);
	int state;
	int n,speed=TSE_Speed_1000Mhz,link_speed=0;
	int check_connect;
	int bLink=0;

	MacRst(1, 1);
    bsp_printf("***Starting Ethernet***\n\r");
    
/******************************************************SETUP DMA & UART********************************************************/
    
	isrInit();
	bsp_Delay(10);

/****************************************************** SETUP ETHERNET LINK ********************************************************/
  	bsp_printf("Phy Init ..\n\r");
	rtl8211_drv_init();
	bsp_Delay(10);
	bsp_printf("Waiting Link Up ..\n\r");
	check_connect=rtl8211_drv_rddata(26);
	if ((check_connect & 0x04)){
		speed=rtl8211_drv_linkup();
		
		if(speed == TSE_Speed_1000Mhz)		
			link_speed = 1000;
		else if(speed == TSE_Speed_100Mhz)
			link_speed = 100;
		else if(speed == TSE_Speed_10Mhz)
			link_speed = 10;
		else							
			link_speed = 0;
		bLink =1;
		clock_sel(speed);
		bsp_Delay(10);
		// PhyDlySetRXTX(8, 0);
		MacNormalInit(speed);
	}
	bsp_Delay(10);
	additionalSetting();
	bsp_Delay(10);
	LwIP_Init();
	tse_rx_intr_enable();
	pdma_vid_intr_enable();
	pdma_ring_buffer_init();
	dmasg_interrupt_config(TSEMAC_DMASG_BASE, TSE_DMASG_RX_CH, DMASG_CHANNEL_INTERRUPT_LINKED_LIST_UPDATE_MASK);
	struct udp_pcb *cmd_pcb = udp_new();
	if (!(udp_bind(cmd_pcb, IP_ADDR_ANY, 5000)==ERR_OK)){
		bsp_printf("UDP bind failed\n\r");
	}
	else {bsp_printf("UDP bind success\n\r");}
	bsp_Delay(10);
	udp_recv(cmd_pcb, on_command, NULL);

	bsp_printf("=========================================\n\r");
	bsp_printf("============  Ethernet Setup  ===========\n\r");
	bsp_printf("=========================================\n\r");
	
	bsp_printf("====== IP: \t\t");
	bsp_printf("%d.%d.%d.%d\n\r",IP_ADDR0,IP_ADDR1,IP_ADDR2,IP_ADDR3);

	bsp_printf("====== Netmask: \t\t");
	bsp_printf("%d.%d.%d.%d\n\r",NETMASK_ADDR0,NETMASK_ADDR1,NETMASK_ADDR2,NETMASK_ADDR3);

	bsp_printf("====== GateWay: \t\t");
	bsp_printf("%d.%d.%d.%d\n\r",GW_ADDR0,GW_ADDR1,GW_ADDR2,GW_ADDR3);
	if (check_connect&0x04){
		bsp_printf("====== Link Speed: \t%d Mbps\n\r", link_speed);
	}
	bsp_printf("=========================================\n\r");
	bool init_eth = true;
	u64_t now=0, prev_reset_check = sys_now(), prev_check_connect=sys_now();
	int currentChar=0;
	char uartInputBuffer[UART_INPUT_BUFFER_LENGTH];

	camera_init();

    while (1) {
		now = sys_now();

		// Handle link status, with delay as this takes some time
		if ((now-prev_check_connect)>50){
			check_connect = rtl8211_drv_rddata(26); // Status register
			if((check_connect & 0x04) == 0 && (bLink)) {
				bLink = 0;
				bsp_printf("Disconnected -- \n\r");
				receivedPacket=false; // Prepare auto reset logic
			}
			else if((check_connect & 0x04) && (!bLink)) {
				speed = rtl8211_drv_linkup(); // Grab connection speed
				clock_sel(speed);
				MacNormalInit(speed);
				bLink = 1;
				bsp_printf("Connected -- \n\r");
				prev_reset_check=now; // Set auto reset to check 1 second from now
			}
			prev_check_connect=now;
		}
		
		// Handle UART 
		while (uart_readOccupancy(BSP_UART_TERMINAL)) {
			char c = uart_read(BSP_UART_TERMINAL);
			if (c=='\b'){ // Backspace character
				bsp_printf_c(c);
				currentChar = (currentChar>0 ? currentChar-1: 0);
				bsp_printf_c(' ');
				bsp_printf_c('\b');
			}
			else if (c=='\r'){ // Carriage return, sent when hitting enter
				bsp_printf_c(c);
				bsp_printf_c('\n');
				uartInputBuffer[currentChar]='\0';
				interpretUART(uartInputBuffer); // Interpret command
				currentChar=0; // Prepare for next command
			}
			else if (c>=' '){ // If the character is not a control character
				bsp_printf_c(c);
				uartInputBuffer[currentChar]=c;
				currentChar=(currentChar+1)&(UART_INPUT_BUFFER_LENGTH-1);
			}
		}

		// Process lwIP timeouts
		sys_check_timeouts();

		if (tse_rx_pending) {
			tse_rx_pending = false;
			ethernetif_input(&gnetif);
		}

		// VID Test DMA Handling
		if (pdma_vid_done) {
			pdma_vid_done = false;
        	pdma_stream_capture(vid_pcb);

		}

		if (streaming && pdma_vid_overloaded) {
			pdma_vid_overloaded = false;
			overloads++;
			dmasg_linked_list_start(PDMA_BASE, PDMA_VID_CHANNEL, (u32)&frame_ring_buf[ring_index]);
		}
		
		// Auto reset logic
		if ((now-prev_reset_check)>1000){
			if (!receivedPacket&&bLink){

				// This command resets the physical connection
				rtl8211_drv_wrdata(0, (rtl8211_drv_rddata(0) & 0xFFFF) | 0x1200);
				bsp_printf("Auto Reset\n\r");
			}
			prev_reset_check = now;
		}
	}
}
