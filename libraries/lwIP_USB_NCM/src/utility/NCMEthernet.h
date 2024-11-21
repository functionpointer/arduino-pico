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
#include "pico/util/queue.h"
#include <SPI.h>
#include <LwipEthernet.h>
#include <pico/async_context_threadsafe_background.h>
#include <pico/critical_section.h>

extern "C" {
extern queue_t _ncmethernet_recv_q;
typedef struct _ncmethernet_packet_t {
    const uint8_t *src;
    uint16_t size;
} _ncmethernet_packet_t;
extern critical_section_t _ncmethernet_pkg_critical_section;
extern _ncmethernet_packet_t _ncmethernet_pkg;
}

class NCMEthernet {
public:
    // constructor and methods as required by LwipIntfDev

    NCMEthernet(int8_t cs, arduino::SPIClass &spi, int8_t intrpin);

    bool begin(const uint8_t *address, netif *netif);
    void end();

    uint16_t sendFrame(const uint8_t *data, uint16_t datalen);

    uint16_t readFrameSize();

    uint16_t readFrameData(uint8_t *buffer, uint16_t bufsize);

    uint16_t readFrame(uint8_t* buffer, uint16_t bufsize);

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

protected:
    netif *_netif;
    _ncmethernet_packet_t _current_packet;

    bool _fill_current_packet();
    void _empty_current_packet();

private:
    bool _running = false;
};



#endif  // NCM_ETHERNET_H
