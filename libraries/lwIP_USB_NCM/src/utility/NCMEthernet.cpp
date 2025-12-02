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

  if (!critical_section_is_initialized(&_ncmethernet_pkg_critical_section)) {
      critical_section_init(&_ncmethernet_pkg_critical_section);
      critical_section_enter_blocking(&_ncmethernet_pkg_critical_section);
      _ncmethernet_pkg.size = 0;
      _ncmethernet_pkg.src = nullptr;
      critical_section_exit(&_ncmethernet_pkg_critical_section);
  }

  return true;
}

void NCMEthernet::end() {

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
    return _ncmethernet_pkg.size;
}

uint16_t NCMEthernet::readFrameData(uint8_t* buffer, uint16_t framesize) {
    critical_section_enter_blocking(&_ncmethernet_pkg_critical_section);

    size_t size = _ncmethernet_pkg.size;
    memcpy(buffer, (const void*)_ncmethernet_pkg.src, size);
    _ncmethernet_pkg.size = 0;

    critical_section_exit(&_ncmethernet_pkg_critical_section);

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