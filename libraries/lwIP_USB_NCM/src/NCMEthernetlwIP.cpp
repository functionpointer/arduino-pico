#include "NCMEthernetlwIP.h"
#include <LwipEthernet.h>
#include <tusb.h>


// Weak function override to add our descriptor to the TinyUSB list
void __USBInstallNetworkControlModel() { /* noop */ }

NCMEthernetlwIP *NCMEthernetlwIP::instance = nullptr;

NCMEthernetlwIP::NCMEthernetlwIP() {
}

bool NCMEthernetlwIP::begin(const uint8_t *macAddress, const uint16_t mtu) {
  instance = this;
  return LwipIntfDev<NCMEthernet>::begin(macAddress, mtu);
}

uint16_t NCMEthernetlwIP::readFrame(uint8_t* buffer, uint16_t bufsize) {
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

uint16_t NCMEthernetlwIP::readFrameSize() {
  return this->_recv_size;
}

uint16_t NCMEthernetlwIP::readFrameData(uint8_t* buffer, uint16_t framesize) {
  if (this->_recv_size != framesize) {
    return 0;
  }
  memcpy(buffer, this->_recv_data, this->_recv_size);
  return this->_recv_size;
}

uint16_t NCMEthernetlwIP::sendFrame(const uint8_t* buf, uint16_t len) {
  // this is basically linkoutput_fn

  for (;;) {
    /* if TinyUSB isn't ready, we must signal back to lwip that there is nothing we can do */
    if (!tud_ready())
      return 0;

    /* if the network driver can accept another packet, we make it happen */
    if (tud_network_can_xmit(len)) {
      tud_network_xmit((void*)const_cast<uint8_t*>(buf), 0 /* unused here */);
      return len;
    }

    /* transfer execution to TinyUSB in the hopes that it will finish transmitting the prior packet */
    tud_task();
  }
}



void NCMEthernetlwIP::tud_network_init_cb() {
  /* if the network is re-initializing and we have a leftover packet, we must do a cleanup */
  this->_recv_data = NULL;
  this->_recv_size = 0;
}

/**
 * This gets called by tinyUSB when a packet is received.
 * We are expected to copy the data during the interrupt, leaving the buffer for tinyUSB to re-use.
 *
 * This style of interrupts is not directly compatible with LwipIntfDev.
 * LwipIntfDev expects a polling interface in the form of readFrameSize and readFrameData
 * Interrupt driven Ethernet works using attachInterrupt to a pin.
 * When fired, the interrupt simply causes a poll.
 *
 * tinyUSB doesn't change the level of a pin, so we can't use attachInterrupt.
 * Furthermore, tinyUSB may re-use the buffer it gave us here, so we can't wait for a poll.
 * Nor do we want to, that causes latency!
 *
 * To fix this, we override LwipIntfDev.
 * Class hierarchy is therefore this:
 * NCMEthernet <- LwipIntfDev <- NCMEthernetlwIP
 *
 * LwipIntfDev introduces the _irq() method, which is normally given to attachInterrupt().
 * We override LwipIntfDev to call it when tinyUSB fires its interrupt.
 * _irq() ultimately calls readFrameSize and readFrameData and copies the packet data into a lwIP packet (a pbuf).
 * This way, using only minimal modifications to LwipIntfDev, we can have no extra memcpy and no extra latency.
 *
 * The function doesn't actually call _irq() but rather its contents. We do this to protect _recv_data and _recv_size
 * using the locks inside ethernet_arch_lwip_begin() and ethernet_arch_lwip_end()
 * @param src
 * @param size
 * @return
 */
bool NCMEthernetlwIP::tud_network_recv_cb(const uint8_t *src, uint16_t size) {
  ethernet_arch_lwip_begin();
  this->handlePackets();

  //leave data for readFrameSize and readFrameData to find
  this->_recv_data = src;
  this->_recv_size = size;

  err_t result = this->handlePackets();

  //and clean it up again before giving the buffer back to tinyUSB
  this->_recv_data = NULL;
  this->_recv_size = 0;

  sys_check_timeouts();
  ethernet_arch_lwip_end();

  return result == ERR_OK;
}

extern "C" {
/***
 * Interface to tinyUSB.
 */

void tud_network_init_cb(void) {
  if (NCMEthernetlwIP::instance == NULL){
    return;
  }
  return NCMEthernetlwIP::instance -> tud_network_init_cb();
}

bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
  if (NCMEthernetlwIP::instance == NULL) {
    return false;
  }
  return NCMEthernetlwIP::instance->tud_network_recv_cb(src, size);
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
  struct pbuf *p = (struct pbuf *) ref;

  (void) arg; /* unused for this example */

  return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

}