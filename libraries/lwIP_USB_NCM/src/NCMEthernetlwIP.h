#pragma once

#include <LwipIntfDev.h>
#include <utility/NCMEthernet.h>
#include <LwipEthernet.h>
#include <WiFi.h>

class NCMEthernetlwIP: public LwipIntfDev<NCMEthernet> {
public:
    NCMEthernetlwIP();

    bool begin(const uint8_t* macAddress = nullptr, const uint16_t mtu = DEFAULT_MTU);
    void end();

    bool tud_network_recv_cb(const uint8_t *src, uint16_t size);
    void tud_network_init_cb(void);

    async_context_threadsafe_background_t async_context;
protected:

    static void recv_irq_work(async_context_t *context, async_when_pending_worker_t *worker);

};

extern "C" {
/***
 * Interface to tinyUSB.
 * See NCMEthernetlwIP.cpp
 */
extern uint8_t tud_network_mac_address[6];

}
