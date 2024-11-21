#include "NCMEthernetlwIP.h"
#include <LwipEthernet.h>
#include <tusb.h>
#include <pico/async_context_threadsafe_background.h>

async_when_pending_worker_t _ncm_ethernet_recv_irq_worker;
NCMEthernetlwIP *_ncm_ethernet_instance;

#define USBD_NCM_EPSIZE 64

// Need to define here so we don't have to include tusb.h in global header (causes problemw w/BT redefining things)
void NCMEthernetlwIP::interfaceCB(int itf, uint8_t *dst, int len) {
    uint8_t desc[TUD_CDC_NCM_DESC_LEN] = {
        // Interface number, description string index, MAC address string index, EP notification address and size, EP data address (out, in), and size, max segment size.
        TUD_CDC_NCM_DESCRIPTOR((uint8_t)itf, _strID, _strMac, _epNotif, USBD_NCM_EPSIZE, _epOut, _epIn, CFG_TUD_NET_ENDPOINT_SIZE, CFG_TUD_NET_MTU)
    };
    memcpy(dst, desc, len);
};

NCMEthernetlwIP::NCMEthernetlwIP() {
}

bool NCMEthernetlwIP::begin(const uint8_t *macAddress, const uint16_t mtu) {
    if (_running) {
        return false;
    }

    USB.disconnect();

    _epIn = USB.registerEndpointIn();
    _epOut = USB.registerEndpointOut();
    _epNotif = USB.registerEndpointIn();
    _strID = USB.registerString("Pico NCM");
    char macaddr_str[sizeof(tud_network_mac_address)*2+2] = {0};
    uint8_t len = 0;
    for (unsigned i=0; i<sizeof(tud_network_mac_address); i++) {
        macaddr_str[len++] = "0123456789ABCDEF"[(tud_network_mac_address[i] >> 4) & 0xf];
        macaddr_str[len++] = "0123456789ABCDEF"[(tud_network_mac_address[i] >> 0) & 0xf];
    }
    _strMac = USB.registerString(macaddr_str);

    _id = USB.registerInterface(2, _cb, (void *)this, TUD_CDC_NCM_DESC_LEN, 4, 0);

    USB.connect();
    _running = true;

    // super call
    bool ret = LwipIntfDev<NCMEthernet>::begin(macAddress, mtu);
    if (!ret) {
        return false;
    }

    async_context_threadsafe_background_config_t config = async_context_threadsafe_background_default_config();
    if (!async_context_threadsafe_background_init(&this->async_context, &config)) {
        return false;
    }

    _ncm_ethernet_instance = this;

    //queue_init(&_ncmethernet_recv_q, sizeof(_ncmethernet_packet_t), 10);

    _ncm_ethernet_recv_irq_worker.do_work = &NCMEthernetlwIP::recv_irq_work;
    async_context_add_when_pending_worker(&this->async_context.core, &_ncm_ethernet_recv_irq_worker);

    return true;
}

void NCMEthernetlwIP::end() {
    if (_running) {
        USB.disconnect();
        USB.unregisterInterface(_id);
        USB.unregisterEndpointIn(_epIn);
        USB.unregisterEndpointOut(_epOut);
        _running = false;
        USB.connect();
    }
}

void NCMEthernetlwIP::recv_irq_work(async_context_t *context, async_when_pending_worker_t *worker) {
    _ncm_ethernet_instance->_irq(_ncm_ethernet_instance);
}

extern "C" {
/***
 * Interface to tinyUSB.
 */

uint8_t tud_network_mac_address[6] = {0};

void tud_network_init_cb(void) {
  /* if the network is re-initializing, and we have a leftover packet, we must do a cleanup */
  while (!queue_is_empty(&_ncmethernet_recv_q)) {
    _ncmethernet_packet_t trash;
    queue_try_remove(&_ncmethernet_recv_q, &trash);
  }
}

bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
    if(_ncmethernet_pkg.size > 0) {
        return false;
    };

    critical_section_enter_blocking(&_ncmethernet_pkg_critical_section);
    _ncmethernet_pkg.src = src;
    _ncmethernet_pkg.size = size;
    critical_section_exit(&_ncmethernet_pkg_critical_section);

    if(_ncm_ethernet_instance != nullptr) {
        async_context_set_work_pending(&_ncm_ethernet_instance->async_context.core, &_ncm_ethernet_recv_irq_worker);
    }

    return true;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
  // this is called by tud_network_xmit, which is called by NCMEthernet::sendFrame,
  // which is called by LwipIntfDev<RawDev>::linkoutput_s
  // linkoutput_s gives us pbuf->payload and pbuf->len

  memcpy(dst, ref, arg);
  return arg;
}

}