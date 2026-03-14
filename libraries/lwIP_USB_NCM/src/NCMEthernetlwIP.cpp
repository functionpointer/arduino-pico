#include "NCMEthernetlwIP.h"
#include <LwipEthernet.h>
#include <tusb.h>
#include <pico/async_context_threadsafe_background.h>
#include <Arduino.h>

NCMEthernetlwIP::NCMEthernetlwIP() {
}

bool NCMEthernetlwIP::begin(const uint8_t *macAddress, const uint16_t mtu) {
	#ifndef __FREERTOS
		this->_recv_irq_worker.do_work = &this->_call_irq;
	#endif

    // super call
    if(!LwipIntfDev<NCMEthernet>::begin(macAddress, mtu)) {
		return false;
	}
	__removeEthernetPacketHandler(this->_phID); // this is added bc LwipIntfDev thinks we must be polled
	// but we actually do interrupts. polling us anyway is inefficient at best, deadlock causing at worst

#ifdef __FREERTOS
	xTaskCreate(NCMEthernetlwIP::ncmTaskFunction, "ncmTask", 256, this, 1, &this->_ncmTask);
#else
	this->_recv_irq_worker.do_work = &this->_call_irq;
#endif
	return true;
}

#ifdef __FREERTOS
void NCMEthernetlwIP::ncmTaskFunction(void *param) {
    (void) param;
    while (true) {
		struct pbuf *p;
		if(xSemaphoreTake(_ncm_ethernet_instance->_recv_semaphore, portMAX_DELAY) != pdTRUE) {
			continue;
		}
		lwip_callback(NCMEthernetlwIP::_call_irq, nullptr);
    }
}
#endif

#ifdef __FREERTOS
void NCMEthernetlwIP::_call_irq(void *cbData) {
#else
void NCMEthernetlwIP::_call_irq(async_context_t *context, async_when_pending_worker_t *worker) {
#endif
	debug_put(NCM_RECV_IRQ_PENDING, false);
	debug_put(LWIP_NCM_RECV_IRQ, true);
#ifdef __FREERTOS
	CoreMutex m(&USB.mutex);
	// in freertos we can afford to block, as long as no other code uses usb and lwip at the same time
#else
	if (!mutex_try_enter(&USB.mutex, NULL)) {
		// couldn't get usb mutex, try again later
		// we can't block here as that would be a deadlock as we are in irq context (_recv_irq_worker)
		async_context_set_work_pending(context, worker);
		debug_put(NCM_RECV_IRQ_PENDING, true);
		debug_put(LWIP_NCM_RECV_IRQ, false);
		return;
	}
#endif
	// we have both mutexes now, set marker
	if (_ncm_ethernet_instance->_marker) {
		panic("marker already set. how?");
	}
	_ncm_ethernet_instance->_marker = true;
	debug_put(NCM_RECV_MARKER, true);

	_ncm_ethernet_instance->_try_process_xmit_queue();
	int limit = 10;
	do {
		_ncm_ethernet_instance->_tud_recv_cb_called = false;
		limit--;
		tud_network_recv_renew();
	} while (_ncm_ethernet_instance->_tud_recv_cb_called && limit > 0);
	_ncm_ethernet_instance->_try_process_xmit_queue();

	debug_put(NCM_RECV_MARKER, false);
	_ncm_ethernet_instance->_marker = false;
#ifdef __FREERTOS
#else
	mutex_exit(&USB.mutex);
#endif
	debug_put(LWIP_NCM_RECV_IRQ, false);
}

void NCMEthernetlwIP::_call_handlepackets() {
	// we can't call _irq(), because that includes a __inLWIP check.
	// when __inLWIP>0, it drops the packet. okay for real pin-based irq, not ok for NCM.
	this->_lwipCallback(this);
}
