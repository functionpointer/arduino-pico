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
#include <NCMEthernetlwIP.h>

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
    _xmit_queue = xQueueCreate(NCMETHERNET_XMIT_QUEUE_LENGTH, sizeof(struct pbuf*));
    if (!_xmit_queue) {
        panic("Unable to allocate NCMEthernet xmit queue");
    }
#else
	queue_init(&this->_recv_queue, sizeof(ncmethernet_packet_t), NCMETHERNET_RECV_QUEUE_LENGTH);
	queue_init(&this->_xmit_queue, sizeof(struct pbuf*), NCMETHERNET_XMIT_QUEUE_LENGTH);

	// calls this->handlePacket() to fetch packets from _recv_queue
    this->_recv_irq_worker.user_data = this;
	// this->_recv_irq_worker.do_work will be set by NCMEthernetlwIP
	// can't do that here because it has to call _irq() which isn't defined in this class yet
	async_context_add_when_pending_worker(__getEthernetContext(), &this->_recv_irq_worker);
#endif

	if (_ncm_ethernet_instance != nullptr) {
		panic("multiple NCM interfaces not supported");
	}
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
#ifdef __FREERTOS
	if (this->_recv_pkg == nullptr) {
		return 0;
	}
	return this->_recv_pkg->size;
#else
	ncmethernet_packet_t p;
	if(!this->_marker) {
		if(!queue_is_empty(&this->_recv_queue)) {
			panic("recv_queue not empty but marker set");
		}
		async_context_set_work_pending(__getEthernetContext(), &_ncm_ethernet_instance->_recv_irq_worker);
		return 0;
	}
	if(!queue_try_peek(&this->_recv_queue, &p)) {
		return 0;
	}
	return p.size;
#endif
}

uint16_t NCMEthernet::readFrameData(uint8_t* buffer, uint16_t framesize) {
#ifdef __FREERTOS
	if (this->_recv_pkg == nullptr) {
		return 0;
	}
	memcpy(buffer, (const void*)this->_recv_pkg.src, min(framesize, p.size));
	uint16_t ret = this->_recv_pkg->size;
	this->_recv_pkg = nullptr;
	return ret;
#else
	ncmethernet_packet_t p;
	if(!this->_marker) {
		if(!queue_is_empty(&this->_recv_queue)) {
			panic("recv_queue not empty but marker set");
		}
		return 0;
	}
	if(!queue_try_remove(&this->_recv_queue, &p)) {
		return 0;
	}

  	memcpy(buffer, (const void*)p.src, min(framesize, p.size));
	debug_put(NCM_RECV_LARGE_PACKET, false);
	return p.size;
#endif
}

void NCMEthernet::discardFrame(uint16_t ign) {
	debug_put(NCM_DISCARDFRAME, true);
#ifdef __FREERTOS
	this->_recv_pkg = nullptr;
#else
	queue_try_remove(&this->_recv_queue, NULL);
#endif
	debug_put(NCM_DISCARDFRAME, false);
}

#ifdef __FREERTOS
uint16_t NCMEthernet::sendFrame(struct pbuf *p) {
	// in case of freeRTOS we will be in the lwip task
	// we can't block to get usb mutex, as we may be called tud_network_recv_cb() via _call_handlepackets()
	xQueueSend(this->_xmit_queue, p, 2);

	if(xSemaphoreTake(__get_freertos_mutex_for_ptr(&USB.mutex), 0)) {
		_process_xmit_queue();
		xSemaphoreGive(__get_freertos_mutex_for_ptr(&USB.mutex));
	}
}
void NCMEthernet::_process_xmit_queue() {
	struct pbuf *p;
	while(true) {
		if (!tud_ready()) {
			return;
		}
		if(xQueueReceive(this->_xmit_queue, p, 0) != pdPASS) {
			return;
		}

		/* if the network driver can accept another packet, we make it happen */
		if (tud_network_can_xmit(p->tot_len)) {
			debug_put(TUD_NETWORK_XMIT, true);
			tud_network_xmit(p, 0);
			debug_put(TUD_NETWORK_XMIT, false);

			hmm problem here pbuf_free(p);
		}

		/* transfer execution to TinyUSB in the hopes that it will finish transmitting the prior packet */
		tud_task();
	}
}
#else
volatile static int xmitpkgcount = 0;
uint16_t NCMEthernet::sendFrame(struct pbuf *p) {
	// in case of baremetal we are probably in IRQ context
	// we should be holding lwip mutex
	// maybe also USB mutex if we were called by NCMEthernetlwIP::_call_irq (i.e. p is an answer packet)
	debug_put(NCM_SENDFRAME, true);
	if(!queue_try_add(&_ncm_ethernet_instance->_xmit_queue, &p)) {
		// queue full, drop packet
		debug_put(NCM_SENDFRAME_QUEUE_FULL, true);
		NCMEthernet::_try_process_xmit_queue(nullptr, nullptr);
		debug_put(NCM_SENDFRAME_QUEUE_FULL, false);
		return 0;
	}
	// tell lwip we are still using it
	pbuf_ref(p);

	// USB mutex is probably free, so we call _try_process_xmit_queue
	// it tries to get the mutex and will send send all packets from the queue
	uint16_t ret = p->tot_len;
	NCMEthernet::_try_process_xmit_queue(nullptr, nullptr);
	debug_put(NCM_SENDFRAME, false);
	return ret;
}

void NCMEthernet::_try_process_xmit_queue(__unused async_context_t *context, __unused async_at_time_worker_t *worker) {
	NCMEthernet *me = _ncm_ethernet_instance;

	bool has_usb_mutex = false;
	if (!me->_marker) {
		if(mutex_try_enter(&USB.mutex, NULL)) {
			has_usb_mutex = true;
		} else {
			debug_put(NCM_RECV_IRQ_PENDING, true);
			async_context_set_work_pending(__getEthernetContext(), &_ncm_ethernet_instance->_recv_irq_worker);
			return;
		}
	}

	struct pbuf *p;
	while(true) {
		if(!tud_ready()) {
			break;
		}
		if(!queue_try_peek(&me->_xmit_queue, &p)) {
			break;
		}
		if(tud_network_can_xmit(p->tot_len)) {
			debug_put(TUD_NETWORK_XMIT, true);
			tud_network_xmit(p, 0);
			debug_put(TUD_NETWORK_XMIT, false);
			if (!queue_try_remove(&me->_xmit_queue, nullptr)) {
				panic("couldn't remove packet from queue after transmitting");
			}
			pbuf_free(p);
		}
		tud_task();
	}

	if(!queue_is_empty(&me->_xmit_queue)) {
		debug_put(NCM_RECV_IRQ_PENDING, true);
		async_context_set_work_pending(__getEthernetContext(), &_ncm_ethernet_instance->_recv_irq_worker);
	}

	if(has_usb_mutex) {
		mutex_exit(&USB.mutex);
	}
}
#endif

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
					debug_put(NCM_TUD_NETWORK_RECV_FALSE, true);
					return false;
			}
			debug_put(NCM_TUD_NETWORK_RECV_CB, true);
			ncmethernet_packet_t p;
			p.src = src;
			p.size = size;
#ifdef __FREERTOS
			// we get called as part of tud_task() from somewhere
			// might be in freertosUSBTask(), might be in SerialUSB stuff, delay(), or something else
			// however, we will be holding the non-recursive FreeRTOS Semaphore __get_freertos_mutex_for_ptr(&USB.mutex)
			// also we won't be in IRQ context
			if(_ncm_ethernet_instance->_recv_pkg != nullptr) {
				panic("_recv_pkg not null");
			}
			_ncm_ethernet_instance->_recv_pkg = &p;
			NCMEthernetlwIP::_call_handlepackets();
			_ncm_ethernet_instance->_recv_pkg = nullptr;
			tud_network_recv_renew();
			return true;
#else
		// we may or may not be in irq context, as tud_task() is called by usbTaskIRQ but also plenty of libraries
		// we should be holding &USB.mutex
		// also lwip mutex, if we were called by NCMEthernetlwIP::_call_irq
		// but also maybe not. we can't check, because lwip mutex is a recursive mutex and the owner is cpu core number
		// this means in IRQ context acquiring lwip mutex will always succeed
		// so we sidestep this issue with "marker", which is only set and cleared by _call_irq()
		// and only while holding both mutexes. i.e. if marker==true we can safely call handlePackets

		if(!_ncm_ethernet_instance->_marker) {
			// not in lwip, can't call pbuf_alloc inside handlePackets safely.
			debug_put(NCM_RECV_IRQ_PENDING, true);
			debug_put(NCM_TUD_NETWORK_RECV_FALSE, true);
			debug_put(NCM_TUD_NETWORK_RECV_CB, false);
			async_context_set_work_pending(__getEthernetContext(), &_ncm_ethernet_instance->_recv_irq_worker);
			return false;
		}
		_ncm_ethernet_instance->_tud_recv_cb_called = true;

		try_again:
		bool added = queue_try_add(&_ncm_ethernet_instance->_recv_queue, &p);

		static_cast<NCMEthernetlwIP*>(_ncm_ethernet_instance)->_call_handlepackets();

		if(queue_is_empty(&_ncm_ethernet_instance->_recv_queue)) {
			if(!added) {
				goto try_again;
			}
			debug_put(NCM_TUD_NETWORK_RECV_FALSE, false);
			debug_put(NCM_TUD_NETWORK_RECV_CB, false);
			return true;
		} else {
			// handlePackets has not taken the packet for some reason
			// we can't leave the pointer in _recv_queue, as tinyusb will free or reeuse it after we return
			debug_put(NCM_RECV_QUEUE_NOT_EMPTY, true);
			while(queue_try_remove(&_ncm_ethernet_instance->_recv_queue, NULL)) {
			}
			debug_put(NCM_RECV_QUEUE_NOT_EMPTY, false);
			// signal tinyusb that we couldn't get this packet processed. schedule worker to try again later
			debug_put(NCM_RECV_IRQ_PENDING, true);
			async_context_set_work_pending(__getEthernetContext(), &_ncm_ethernet_instance->_recv_irq_worker);
			debug_put(NCM_TUD_NETWORK_RECV_FALSE, true);
			debug_put(NCM_TUD_NETWORK_RECV_CB, false);
			return false;
		}
#endif
    }

    uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
        // this is called by tud_network_xmit, which is called by NCMEthernet::sendFrame
        // we are in IRQ context but we have both the lwip and the USB mutex

        struct pbuf *p = (struct pbuf *) ref;
				(void) arg;

				return pbuf_copy_partial(p, dst, p->tot_len, 0);
		}
}