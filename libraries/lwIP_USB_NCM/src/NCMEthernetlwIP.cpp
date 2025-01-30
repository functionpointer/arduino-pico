#include "NCMEthernetlwIP.h"
#include <LwipEthernet.h>
#include <tusb.h>
#include <pico/async_context_threadsafe_background.h>

NCMEthernetlwIP *NCMEthernetlwIP::instance = nullptr;

NCMEthernetlwIP::NCMEthernetlwIP() {
}

bool NCMEthernetlwIP::begin(const uint8_t *macAddress, const uint16_t mtu) {
    // super call
    bool ret = LwipIntfDev<NCMEthernet>::begin(macAddress, mtu);
    if (!ret) {
        return false;
    }

    queue_init(&_ncmethernet_recv_q, sizeof(_ncmethernet_packet), 10);

    _ncm_ethernet_recv_irq_worker.do_work = this->recv_irq_work;
    async_context_add_when_pending_worker(_context, &_ncm_ethernet_recv_irq_worker);

    return true;
}

void NCMEthernetlwIP::recv_irq_work(async_context_t *context, async_when_pending_worker_t *worker) {
  this->_irq(&this);
}

extern "C" {
/***
 * Interface to tinyUSB.
 */

uint8_t tud_network_mac_address[6] = {0};

void tud_network_init_cb(void) {
  /* if the network is re-initializing, and we have a leftover packet, we must do a cleanup */
  while (!queue_is_empty(_ncmethernet_recv_q)) {
    _ncmethernet_packet trash;
    queue_try_remove(_ncmethernet_recv_q, &trash);
  }
}

bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
  _ncmethernet_packet q = {
          .size = size,
          .src = src,
  };
  if(!queue_try_add(_ncmethernet_recv_q, &q)) {
    return false;
  }
  async_context_set_work_pending(_context, _ncm_ethernet_recv_irq_worker);
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