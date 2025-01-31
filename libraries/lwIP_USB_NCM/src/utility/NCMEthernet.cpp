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
#include <tusb.h>

// Weak function override to add our descriptor to the TinyUSB list
void __USBInstallNetworkControlModel() { /* noop */ }

NCMEthernet::NCMEthernet(int8_t cs, arduino::SPIClass &spi, int8_t intrpin) {
  (void) cs;
  (void) spi;
  (void) intrpin;
}

bool NCMEthernet::begin(const uint8_t* mac_address, netif *net) {
  (void) net;
  memcpy(tud_network_mac_address, mac_address, 6);

  this->_current_packet.size = 0;
  this->_current_packet.src = nullptr;

  if (!critical_section_is_initialized(&_ncmethernet_pkg_critical_section)) {
      critical_section_init(&_ncmethernet_pkg_critical_section);
  }
  return true;
}

void NCMEthernet::end() {

}

bool NCMEthernet::_fill_current_packet() {
    if(this->_current_packet.size > 0) {
        return true;
    }

    critical_section_enter_blocking(&_ncmethernet_pkg_critical_section);
    this->_current_packet.src = _ncmethernet_pkg.src;
    this->_current_packet.size = _ncmethernet_pkg.size;
    critical_section_exit(&_ncmethernet_pkg_critical_section);

  return this->_current_packet.size > 0;
}

void NCMEthernet::_empty_current_packet() {
    if (this->_current_packet.size == 0) {
        return;
    }
    critical_section_enter_blocking(&_ncmethernet_pkg_critical_section);
    _ncmethernet_pkg.src = nullptr;
    _ncmethernet_pkg.size = 0;
    critical_section_exit(&_ncmethernet_pkg_critical_section);

    this->_current_packet.size = 0;
    this->_current_packet.src = nullptr;
    tud_network_recv_renew();
}

uint16_t NCMEthernet::readFrame(uint8_t* buffer, uint16_t bufsize) {
  uint16_t data_len = this->readFrameSize();

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
    if (!this->_fill_current_packet()) {
        return 0;
    }
    return this->_current_packet.size;
}

uint16_t NCMEthernet::readFrameData(uint8_t* buffer, uint16_t framesize) {
  if (!this->_fill_current_packet()) {
    return 0;
  }
  if (this->_current_packet.size != framesize) {
      return 0;
  }

  size_t size = this->_current_packet.size;
  memcpy(buffer, (const void*)this->_current_packet.src, size);

  this->_empty_current_packet();

  return size;
}

uint16_t NCMEthernet::sendFrame(const uint8_t* buf, uint16_t len) {
  // this is basically linkoutput_fn

  for (;;) {
    /* if TinyUSB isn't ready, we must signal back to lwip that there is nothing we can do */
    if (!tud_ready())
      return 0;

    /* if the network driver can accept another packet, we make it happen */
    if (tud_network_can_xmit(len)) {
      tud_network_xmit((void*)const_cast<uint8_t*>(buf), len);
      return len;
    }

    /* transfer execution to TinyUSB in the hopes that it will finish transmitting the prior packet */
    tud_task();
  }
}

extern "C" {
  // queue to transfer packet pointers between tinyusb tud_network_recv_cb
  // and readFrameSize/readFrameData
  critical_section_t _ncmethernet_pkg_critical_section;
  _ncmethernet_packet_t _ncmethernet_pkg;
}