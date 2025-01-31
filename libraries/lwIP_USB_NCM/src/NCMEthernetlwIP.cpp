#include "NCMEthernetlwIP.h"
#include <LwipEthernet.h>
#include <tusb.h>
#include <pico/async_context_threadsafe_background.h>

async_when_pending_worker_t _ncm_ethernet_recv_irq_worker;
NCMEthernetlwIP *_ncm_ethernet_instance;

NCMEthernetlwIP::NCMEthernetlwIP() {
}

bool NCMEthernetlwIP::begin(const uint8_t *macAddress, const uint16_t mtu) {
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