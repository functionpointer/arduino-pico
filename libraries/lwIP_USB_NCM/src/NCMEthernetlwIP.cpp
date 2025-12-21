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
	return true;
}

void NCMEthernetlwIP::packetReceivedIRQWorker(NCMEthernet *instance) {
    NCMEthernetlwIP *d = static_cast<NCMEthernetlwIP*>(instance);
    d->_irq(instance);
}
