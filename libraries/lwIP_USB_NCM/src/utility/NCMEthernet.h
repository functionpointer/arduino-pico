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
#include <LwipIntfDev.h>

#ifdef __FREERTOS
#include "FreeRTOS.h"
#include "semphr.h"
#include "freertos/freertos-lwip.h"
#else
#include "pico/util/queue.h"
#include <pico/async_context_threadsafe_background.h>
#endif

#ifndef NCMETHERNET_RECV_QUEUE_LENGTH
// tud_network_recv_cb relies on handlePackets to fetch data from the recv queue
// for memory safety it has to flush any unhandled packets from the recv queue before returning
// handlePackets will stop fetching after 10 packets.
// it can also stop before that, when pbuf_alloc fails. 1 minimizes potential for lost packets
#define NCMETHERNET_RECV_QUEUE_LENGTH 1
#endif

#ifndef NCMETHERNET_XMIT_QUEUE_LENGTH
// only used on baremetal, not FreeRTOS
#define NCMETHERNET_XMIT_QUEUE_LENGTH 12
#endif

extern "C" {
    typedef struct _ncmethernet_packet_t {
        const uint8_t *src;
        uint16_t size;
    } ncmethernet_packet_t;
}


class NCMEthernet;

extern "C" {
    extern NCMEthernet *_ncm_ethernet_instance;
}

class NCMEthernet {
public:
    // constructor and methods as required by LwipIntfDev

    NCMEthernet(int8_t cs, arduino::SPIClass &spi, int8_t intrpin);

    bool begin(const uint8_t *address, netif *netif);
    void end();

    uint16_t sendFrame(struct pbuf *pbuf);

    uint16_t readFrameSize();

    uint16_t readFrameData(uint8_t *buffer, uint16_t bufsize);

    uint16_t readFrame(uint8_t* buffer, uint16_t bufsize);

    void discardFrame(uint16_t ign);

    bool interruptIsPossible() {
        return false;
    }

    PinStatus interruptMode() {
        return HIGH;
    }

    constexpr bool needsSPI() const {
        return false;
    }

    void usbInterfaceCB(int itf, uint8_t *dst, int len);

#ifdef __FREERTOS
	ncmethernet_packet_t *_recv_pkg = nullptr;
	QueueHandle_t _xmit_queue;
#else
	queue_t _recv_queue;
	queue_t _xmit_queue;
	volatile bool _marker = false;
	bool _tud_recv_cb_called = false;

    async_when_pending_worker_t _recv_irq_worker;
    void _try_process_xmit_queue(__unused async_context_t *context, __unused async_at_time_worker_t *worker);
#endif
protected:
    netif *_netif;
    uint8_t _id;
    uint8_t _epIn;
    uint8_t _epNotif;
    uint8_t _epOut;
    uint8_t _strID;
    uint8_t _strMac;
    char macAddrStr[6 * 2 + 2] = {0};

    static void _usb_interface_cb(int itf, uint8_t *dst, int len, void *param) {
        ((NCMEthernet *)param)->usbInterfaceCB(itf, dst, len);
    }

};
#endif  // NCM_ETHERNET_H
