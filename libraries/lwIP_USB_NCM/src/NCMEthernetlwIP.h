#pragma once

#include <LwipIntfDev.h>
#include <utility/NCMEthernet.h>
#include <LwipEthernet.h>
#include <WiFi.h>

class NCMEthernetlwIP: public LwipIntfDev<NCMEthernet> {
    bool tud_network_recv_cb(const uint8_t *src, uint16_t size);
    void tud_network_init_cb(void);
}
