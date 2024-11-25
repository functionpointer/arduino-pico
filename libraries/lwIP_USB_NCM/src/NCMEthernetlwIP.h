#pragma once

#include <LwipIntfDev.h>
#include <utility/NCMEthernet.h>
#include <LwipEthernet.h>
#include <WiFi.h>

class NCMEthernetlwIP: public LwipIntfDev<NCMEthernet> {
public:
    NCMEthernetlwIP();

    bool begin(const uint8_t* macAddress = nullptr, const uint16_t mtu = DEFAULT_MTU);

    uint16_t sendFrame(const uint8_t *data, uint16_t datalen);

    uint16_t readFrameSize();

    uint16_t readFrameData(uint8_t *buffer, uint16_t bufsize);

    uint16_t readFrame(uint8_t* buffer, uint16_t bufsize);

    bool tud_network_recv_cb(const uint8_t *src, uint16_t size);
    void tud_network_init_cb(void);


    static NCMEthernetlwIP* instance;
protected:
    netif *_netif;

    /*
     * used when receiving data
     * see NCMEthernetlwIP.cpp
     */
    uint16_t _recv_size;
    const uint8_t *_recv_data;
};
