/*
    Copyright (c) 2024 functionpointer

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/

#include "NCMEthernet.h"
#include <LwipEthernet.h>

static NCMEthernet *_ncm_ethernet_instance = NULL;

NCMEthernet::NCMEthernet(int8_t cs, SPIClass& spi, int8_t intr) : _spi(spi), _cs(cs), _intr(intr) {
}

bool NCMEthernet::begin(const uint8_t* mac_address, netif *net) {
  _netif = net;
  memcpy(tud_network_mac_address, _mac_address, 6);

  _ncm_ethernet_instance = this;
  return true;
}

void NCMEthernet::end() {

}

uint16_t NCMEthernet::readFrame(uint8_t* buffer, uint16_t bufsize) {
    uint16_t data_len = readFrameSize();

    if (data_len == 0) {
        return 0;
    }

    if (data_len > bufsize) {
        // Packet is bigger than buffer - drop the packet
        discardFrame(data_len);
        return 0;
    }

    return readFrameData(buffer, data_len);
}

uint16_t NCMEthernet::readFrameSize() {
  if(this->received_frame == NULL) {
    return 0;
  }
  return this->received_frame->tot_len;
}

void NCMEthernet::discardFrame(uint16_t framesize) {
    wizchip_recv_ignore(framesize);
    setSn_CR(Sn_CR_RECV);
}

uint16_t NCMEthernet::readFrameData(uint8_t* buffer, uint16_t framesize) {

    wizchip_recv_data(buffer, framesize);
    setSn_CR(Sn_CR_RECV);

    // let lwIP deal with mac address filtering
    return framesize;
}

uint16_t NCMEthernet::sendFrame(const uint8_t* buf, uint16_t len) {
    // this is basically linkoutput_fn

  for (;;) {
    /* if TinyUSB isn't ready, we must signal back to lwip that there is nothing we can do */
    if (!tud_ready())
      return 0;

    /* if the network driver can accept another packet, we make it happen */
    if (tud_network_can_xmit(len)) {
      tud_network_xmit(buf, 0 /* unused here */);
      return len;
    }

    /* transfer execution to TinyUSB in the hopes that it will finish transmitting the prior packet */
    tud_task();
  }
}

bool NCMEthernet::tud_network_init_cb() {
  /* if the network is re-initializing and we have a leftover packet, we must do a cleanup */
  if (_ncm_ethernet_instance -> received_frame != NULL) {
    pbuf_free(_ncm_ethernet_instance->received_frame);
    _ncm_ethernet_instance->received_frame = NULL;
  }
}

bool NCMEthernet::tud_network_recv_cb(const uint8_t *src, uint16_t size) {
  this->handlepackets();
}

extern "C" {
  uint8_t tud_network_mac_address[6] = {0};

  void tud_network_init_cb(void) {
    if (_ncm_ethernet_instance == NULL){
      return;
    }
    tud_network_init_cb->tud_network_init_cb();
  }

  bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
    if (_ncm_ethernet_instance == NULL) {
      return false;
    }
    return _ncm_ethernet_instance->tud_network_recv_cb(src, size);
  }

  uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
    struct pbuf *p = (struct pbuf *) ref;

    (void) arg; /* unused for this example */

    return pbuf_copy_partial(p, dst, p->tot_len, 0);
  }

}

