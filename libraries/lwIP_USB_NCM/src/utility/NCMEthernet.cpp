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
#include "USB.h"

#ifdef __FREERTOS
#include "FreeRTOS.h"
#include "semphr.h"
#include "freertos/freertos-lwip.h"
#endif

#define USBD_NCM_EPSIZE 64

NCMEthernet::NCMEthernet(int8_t cs, arduino::SPIClass &spi, int8_t intrpin) {
    (void) cs;
    (void) spi;
    (void) intrpin;
}

bool NCMEthernet::begin(const uint8_t* mac_address, netif *net) {
    (void) net;
    memcpy(tud_network_mac_address, mac_address, 6);

#ifdef __FREERTOS
    _recv_queue = xQueueCreate(NCMETHERNET_RECV_QUEUE_LENGTH, sizeof(ncmethernet_packet_t));
    if (!_recv_queue) {
        panic("Unable to allocate NCMEthernet recv queue");
    }
#else
    if (!critical_section_is_initialized(&this->_recv_critical_section)) {
        critical_section_init(&this->_recv_critical_section);
        critical_section_enter_blocking(&this->_recv_critical_section);
        this->_recv_pkg.size = 0;
        this->_recv_pkg.src = nullptr;
        critical_section_exit(&this->_recv_critical_section);
    }

    async_context_threadsafe_background_config_t config = async_context_threadsafe_background_default_config();
    if (!async_context_threadsafe_background_init(&this->_async_context, &config)) {
        return false;
    }

    this->_recv_irq_worker.user_data = this;
    this->_recv_irq_worker.do_work = &NCMEthernet::_recv_irq_work;
    async_context_add_when_pending_worker(&this->_async_context.core, &this->_recv_irq_worker);
#endif

    _ncm_ethernet_instance = this;

    USB.disconnect();

    _epIn = USB.registerEndpointIn();
    _epOut = USB.registerEndpointOut();
    _epNotif = USB.registerEndpointIn();
    _strID = USB.registerString("Pico NCM");

    // mac address we give to the PC
    // must be different than our own
    uint8_t len = 0;
    for (unsigned i = 0; i < 6; i++) {
        uint8_t mac_byte = tud_network_mac_address[i];
        if (i == 5) { // invert last byte
            mac_byte ^= 0xFF;
        }
        macAddrStr[len++] = "0123456789ABCDEF"[(mac_byte >> 4) & 0xf];
        macAddrStr[len++] = "0123456789ABCDEF"[(mac_byte >> 0) & 0xf];
    }
    _strMac = USB.registerString(macAddrStr);

    _id = USB.registerInterface(2, _usb_interface_cb, (void *)this, TUD_CDC_NCM_DESC_LEN, 3, 0);

    USB.connect();

    return true;
}

void NCMEthernet::end() {
    USB.disconnect();
    USB.unregisterInterface(_id);
    USB.unregisterEndpointIn(_epIn);
    USB.unregisterEndpointIn(_epNotif);
    USB.unregisterEndpointOut(_epOut);
    USB.connect();
}

// Need to define here so we don't have to include tusb.h in global header (causes problemw w/BT redefining things)
void NCMEthernet::usbInterfaceCB(int itf, uint8_t *dst, int len) {
    uint8_t desc[TUD_CDC_NCM_DESC_LEN] = {
        // Interface number, description string index, MAC address string index, EP notification address and size, EP data address (out, in), and size, max segment size.
        TUD_CDC_NCM_DESCRIPTOR((uint8_t)itf, _strID, _strMac, _epNotif, USBD_NCM_EPSIZE, _epOut, _epIn, CFG_TUD_NET_ENDPOINT_SIZE, CFG_TUD_NET_MTU)
    };
    memcpy(dst, desc, len);
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
    ncmethernet_packet_t p;
#ifdef __FREERTOS
    if (!xQueuePeek(this->_recv_queue, &p, 0)) {
        // no packet in queue
        return 0;
    }
    return p.size;
#endif
}

uint16_t NCMEthernet::readFrameData(uint8_t* buffer, uint16_t framesize) {
#ifdef __FREERTOS
    ncmethernet_packet_t p;
    if (!xQueueReceive(this->_recv_queue, &p, 0)) {
        return 0;
    }
    memcpy(buffer, (const void*)p.src, min(framesize, p.size));
    // do we need __get_freertos_mutex_for_ptr(&USB.mutex) for this?
    // we certainly could block to get it without an issue
    // just slower, might cause more task switches
    tud_network_recv_renew();
    return p.size;
#endif
}

void NCMEthernet::discardFrame(uint16_t ign) {
#ifdef __FREERTOS
    ncmethernet_packet_t p;
    xQueueReceive(this->_recv_queue, &p, 0);
    tud_network_recv_renew();
#endif
}

uint16_t NCMEthernet::sendFrame(const uint8_t* buf, uint16_t len) {
    // this is basically linkoutput_fn
    // in case of freeRTOS we will be in the lwip task

    // blocking get of __get_freertos_mutex_for_ptr(&USB.mutex)
    // may block lwip task and thats ok, see tud_network_recv_cb()
    CoreMutex m(&USB.mutex, false);

    for (;;) {
        /* if TinyUSB isn't ready, we must signal back to lwip that there is nothing we can do */
        if (!tud_ready()) {
            return 0;
        }

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
    // data transfer between tinyUSB callbacks and NCMEthernet class
    NCMEthernet *_ncm_ethernet_instance = nullptr;

    /*
        Interface to tinyUSB.
    */
    uint8_t tud_network_mac_address[6] = {0};

    void tud_network_init_cb(void) {
    }

    bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
        if (_ncm_ethernet_instance == nullptr) {
            return false;
        }
#ifdef __FREERTOS
        // we get called as part of tud_task() from somewhere
        // might be in freertosUSBTask(), might be in SerialUSB stuff, delay(), or something else
        // however, we will be holding the non-recursive FreeRTOS Semaphore __get_freertos_mutex_for_ptr(&USB.mutex)
        // also we won't be in IRQ context

        // ultimately, we just want to call _ncm_ethernet_instance.getNetIf()->input()
        // however, we can't do this directly as we are not in lwip task.
        // furthermore, input() can cause answer packets to be created
        // which lwip will end up sending by calling NCMEthernet::sendFrame()
        // which would cause tud_network_xmit() to be called while in tud_network_recv_cb()
        // probably cause a crash in tinyusb

        // so we want to switch task to lwip
        // we have two ways of doing that: either directly using lwip_wrap.cpp, where we could call ethernet_input()
        // or by adding a receive queue and scheduling the lwip task to fetch it from there
        // the former option is a deadlock
        // as the wrapped functions block the current task to wait for a return value
        // if answer packets are generated hey end up in sendFrame trying to get __get_freertos_mutex_for_ptr(&USB.mutex)
        // but thats already held by this task which is blocked. thus deadlock

        // a transmit queue could solve this, but that is inefficient as the pbuf pointer sendFrame gets may be freed
        // or reused after exiting sendFrame(). so we would have to memcpy
        // furthermore, dropping frames in sendFrame() is inefficient, as we would waste the cpu time that created the packet

        // Instead, we use a receive queue. dropped packets have barely been processed, so it is more efficient with cpu time
        // sendFrame() will still try to get __get_freertos_mutex_for_ptr(&USB.mutex), but if that blocks lwip doesnt deadlock.
        // Thats because this task can keep enqueueing packets to the receive queue without waiting for lwip.
        // It will be finished eventually and drop __get_freertos_mutex_for_ptr(&USB.mutex).
        // That will unblock lwip and allow it to send the answers.

        // all this means after enqueueing we must get lwip task to fetch packets WITHOUT BLOCKING THIS TASK
        // ideally we would want to do this:
        // LWIPWork w;
        // w.op = __callback;
        // __callback_req req = { this->_lwipCallback, &this };
        // w.req = &req;
        // w.wakeup = 0;
        // xQueueSend(__lwipQueue, &w, 0);
        // not just nonblocking but also without yield, saving task switches when multiple packets are in tinyusb's buffer
        // however, we can't do this because it breaches encapsulation of LwipIntfDev.h and freertos-lwip.cpp

        // instead, we call _irq() to do almost the same thing

        // technically, there is an even better option than a receive queue:
        // just use __lwipQueue as the receive queue
        // instead of specifying this->_lwipCallback we could specify the packet directly and a new __lwip_op
        // but thats a bunch of hassle just for this specific use case and one extra queue isn't that expensive


        ncmethernet_packet_t p;
        p.src = src;
        p.size = size;
        // enqueue packet to recv queue without waiting for a response from lwip task
        // lwip task may have same or lower priority than us
        // so we allow ourselves to be blocked for a small amount of time to give lwip time to process the packets
        if (!xQueueSend(_ncm_ethernet_instance->_recv_queue, &p, 2)) {
            // if the time isn't enough we are overwhelmed so we drop the packet.
            // should cause sender to slow down thanks to TCP

            // blocking too long may cause USB to fail
            // blocking too short may cause unnecessarily dropped packets
            return false;
        }

        // call _irq() to get lwip to fetch the packets

        // calling _irq() this way is fragile
        // it expects to be called from ISR context, but we call it from some random task context instead
        // it will cause xQueueSendFromISR to be used, which isn't ideal from non-ISR context
        // but IMPORTANTLY it won't block this task waiting for a response

        // _irq() ends up writing to and enqueuing LwipIntfDev::_irqBuffer inside lwip_callback()
        // normally that is safe as _irq() disables further interrupts until the lwip task as finished the callback
        // however, that doesn't do anything in our case
        // should be fine anyways as the data is always the same (LwipIntfDev<NCMEthernet>::_lwipCallback, _ncm_ethernet_instance)

		_ncm_ethernet_instance->packetReceivedIRQWorker(_ncm_ethernet_instance);

#else
        critical_section_enter_blocking(&_ncm_ethernet_instance->_recv_critical_section);
        _ncm_ethernet_instance->_recv_pkg.src = src;
        _ncm_ethernet_instance->_recv_pkg.size = size;
        critical_section_exit(&_ncm_ethernet_instance->_recv_critical_section);

        async_context_set_work_pending(&_ncm_ethernet_instance->_async_context.core, &_ncm_ethernet_instance->_recv_irq_worker);
#endif
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