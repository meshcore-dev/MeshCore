#pragma once

#define UDP_BRIDGE_IPV4 0x0
#define UDP_BRIDGE_IPV6 0x1

#define UDP_BRIDGE_MODE_BROADCAST 0x0
#define UDP_BRIDGE_MODE_MULTICAST 0x1
#define UDP_BRIDGE_MODE_DIRECT 0x2

namespace mesh {

//#include <IPAddress.h>
//#include <IPv6Address.h>

typedef struct UDPBridgeFlags {
    uint8_t network_bridge: 1;           // bridge udp network packets to lora
    uint8_t rx_bridge: 1;                // bridge all heard lora packets to udp
    uint8_t tx_bridge: 1;                // bridge all transmitted lora packets to udp
    uint8_t ip_version: 1;               // ip mode ipv4 or ipv6
    uint8_t mode : 2;                    // bridge mode(broadcast, multicast, direct)
    uint8_t reserved: 2;
}; 

typedef struct UDPBridgePrefs {
    UDPBridgeFlags flags; 
    uint16_t port;

    union {
        uint8_t ipv4[4];
        uint8_t ipv6[16];    
    };
};
}

//bool udp_bridge_flags;  // bridge_rx_enable, bridge_tx_enable, bridge_listen_enable, ip_mode(ipv4, ipv6),