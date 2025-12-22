#include "NCMEthernetlwIP.h"
#include <LwipEthernet.h>
#include <tusb.h>
#include <pico/async_context_threadsafe_background.h>
#include <Arduino.h>

NCMEthernetlwIP::NCMEthernetlwIP() {
}

bool NCMEthernetlwIP::begin(const uint8_t *macAddress, const uint16_t mtu) {
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

void NCMEthernetlwIP::_call_irq(__unused async_context_t *context, __unused async_when_pending_worker_t *worker) {
    LwipIntfDev<NCMEthernet> *d = static_cast<LwipIntfDev<NCMEthernet>*>(_ncm_ethernet_instance);
    d->_irq(d);
}
