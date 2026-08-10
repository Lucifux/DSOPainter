#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS 1

#define LWIP_IPV4 1
#define LWIP_UDP 1
#define LWIP_TCP 1
#define LWIP_DNS 1
#define LWIP_DHCP 1

#define LWIP_RAW 1
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0

#define MEM_ALIGNMENT 4
#define MEM_SIZE 4000

#define PBUF_POOL_SIZE 24

#endif