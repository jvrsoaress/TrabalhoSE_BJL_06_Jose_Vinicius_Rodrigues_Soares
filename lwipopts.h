#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_TCP 1
#define LWIP_UDP 1
#define MEM_ALIGNMENT 4
#define MEM_SIZE 3072 // Reduzido para liberar SRAM
#define MEMP_NUM_PBUF 12 // Reduzido para economizar memória
#define PBUF_POOL_SIZE 12 // Reduzido para economizar memória
#define MEMP_NUM_UDP_PCB 4
#define MEMP_NUM_TCP_PCB 4
#define MEMP_NUM_TCP_SEG 24 // Suficiente para TCP_SND_QUEUELEN
#define LWIP_IPV4 1
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define LWIP_DHCP 1
#define LWIP_AUTOIP 1
#define LWIP_DNS 1
#define LWIP_HTTPD 0
#define TCP_WND 3072 // Ajustado para HTML ~600 bytes
#define TCP_SND_BUF 3072 // Ajustado para HTML ~600 bytes
#define LWIP_NETIF_HOSTNAME 1

#endif