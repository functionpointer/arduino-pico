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

#ifndef __FREERTOS
	this->_recv_irq_worker.do_work = &this->_call_irq;
#endif
	return true;
}

void NCMEthernetlwIP::_call_irq(async_context_t *context, async_when_pending_worker_t *worker) {
	debug_put(NCM_RECV_IRQ_PENDING, false);
	debug_put(LWIP_NCM_RECV_IRQ, true);
	if (!mutex_try_enter(&USB.mutex, NULL)) {
		// couldn't get usb, try again later
		async_context_set_work_pending(context, worker);
		debug_put(LWIP_NCM_RECV_IRQ, false);
		return;
	}
	// we have both mutexes now, set marker
	if (_ncm_ethernet_instance->_marker) {
		panic("marker already set. how?");
	}
	_ncm_ethernet_instance->_marker = true;

	_ncm_ethernet_instance->_try_process_xmit_queue(NULL, NULL);
	tud_network_recv_renew();
	_ncm_ethernet_instance->_try_process_xmit_queue(NULL, NULL);

	_ncm_ethernet_instance->_marker = false;
	mutex_exit(&USB.mutex);
	debug_put(LWIP_NCM_RECV_IRQ, false);
}

void NCMEthernetlwIP::_call_handlepackets() {
	LwipIntfDev<NCMEthernet> *d = static_cast<LwipIntfDev<NCMEthernet>*>(_ncm_ethernet_instance);
	if (d != this) {
		panic("only one NCM instance allowed");
	}
	// we don't actually call d->_irq(d), because that includes a __inLWIP check.
	// when __inLWIP>0, it drops the packet. okay for real pin-based irq, not ok for NCM.
	lwip_callback(d->_lwipCallback, d, &d->_irqBuffer);
}
