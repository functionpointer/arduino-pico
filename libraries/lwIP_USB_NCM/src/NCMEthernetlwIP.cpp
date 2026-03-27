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
#ifdef __FREERTOS
	CoreMutex m(&USB.mutex);
	// in freertos we can afford to block, as long as no other code uses usb and lwip at the same time
#else
	if (!mutex_try_enter(&USB.mutex, NULL)) {
		// couldn't get usb mutex, try again later
		// we can't block here as that would be a deadlock as we are in irq context (_recv_irq_worker)
		worker->work_pending = true;
		return;
	}
#endif
	// we have both mutexes now, set marker
	if (_ncm_ethernet_instance->_marker) {
		panic("marker already set. how?");
	}
	_ncm_ethernet_instance->_marker = true;

	_ncm_ethernet_instance->_try_process_xmit_queue();
	int limit = 10;
	do {
		_ncm_ethernet_instance->_tud_recv_cb_called = false;
		limit--;
		tud_network_recv_renew();
		tud_task();
	} while (_ncm_ethernet_instance->_tud_recv_cb_called && limit > 0);
	_ncm_ethernet_instance->_try_process_xmit_queue();

	_ncm_ethernet_instance->_marker = false;
#ifdef __FREERTOS
#else
	mutex_exit(&USB.mutex);
#endif
}

void NCMEthernetlwIP::_call_handlepackets() {
    // We get called by tud_network_recv_cb only when _holding_both_mutexes==1.
    // Therefore it is safe to call lwip functions.

    // We can not call this->_irq(), because that includes a __inLWIP check.
    // When __inLWIP>0, it drops the packet. Okay for real pin-based irq, not ok for NCM.

    // We can also not call this->_lwipCallback, as that includes ethernet_arch_lwip_gpio_unmask().
    // Unmasking without having masked first will screw up any gpio interrupts the sketch may have set up.

    // So we call the contents of _lwipCallback
    this->handlePackets();
    sys_check_timeouts();
}
