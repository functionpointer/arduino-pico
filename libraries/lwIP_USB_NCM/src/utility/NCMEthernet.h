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

#ifndef NCM_ETHERNET_H
#define NCM_ETHERNET_H

#include <stdint.h>
#include <Arduino.h>
#include <SPI.h>
#include <LwipEthernet.h>

class NCMEthernet {
public:
    // constructor and methods as required by LwipIntfDev

    NCMEthernet(int8_t cs, arduino::SPIClass &spi, int8_t intrpin);

    bool begin(const uint8_t *address, netif *netif);
    void end();

    uint16_t sendFrame(const uint8_t *data, uint16_t datalen);

    uint16_t readFrameSize();

    uint16_t readFrameData(uint8_t *buffer, uint16_t bufsize) {
      (void) buffer;
      return bufsize;
    }

    void discardFrame(uint16_t ign) {
      (void) ign;
    }

    bool interruptIsPossible() {
      return false;
    }

    PinStatus interruptMode() {
      return HIGH;
    }

    constexpr bool needsSPI() const {
      return false;
    }
};

extern "C" {
/***
 * Interface to tinyUSB.
 * See NCMEthernetlwIP.cpp
 */
extern uint8_t tud_network_mac_address[6];

}

#endif  // NCM_ETHERNET_H
