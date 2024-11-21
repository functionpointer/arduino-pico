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

NCMEthernet::NCMEthernet(int8_t cs, SPIClass& spi, int8_t intr) : _spi(spi), _cs(cs), _intr(intr) {
}

bool NCMEthernet::begin(const uint8_t* mac_address, netif *net) {
    _netif = net;
    memcpy(_mac_address, mac_address, 6);

    //todo

    // Success
    return true;
}

void NCMEthernet::end() {

}

uint16_t NCMEthernet::readFrame(uint8_t* buffer, uint16_t bufsize) {
    uint16_t data_len = readFrameSize();

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
    setSn_IR(Sn_IR_RECV);

    uint16_t len = getSn_RX_RSR();

    if (len == 0) {
        return 0;
    }

    uint8_t  head[2];
    uint16_t data_len = 0;

    wizchip_recv_data(head, 2);
    setSn_CR(Sn_CR_RECV);

    data_len = head[0];
    data_len = (data_len << 8) + head[1];
    data_len -= 2;

    return data_len;
}

void NCMEthernet::discardFrame(uint16_t framesize) {
    wizchip_recv_ignore(framesize);
    setSn_CR(Sn_CR_RECV);
}

uint16_t NCMEthernet::readFrameData(uint8_t* buffer, uint16_t framesize) {
    wizchip_recv_data(buffer, framesize);
    setSn_CR(Sn_CR_RECV);

    // let lwIP deal with mac address filtering
    return framesize;
}

uint16_t NCMEthernet::sendFrame(const uint8_t* buf, uint16_t len) {
    ethernet_arch_lwip_gpio_mask(); // So we don't fire an IRQ and interrupt the send w/a receive!

    // Wait for space in the transmit buffer
    while (1) {
        uint16_t freesize = getSn_TX_FSR();
        if (getSn_SR() == SOCK_CLOSED) {
            ethernet_arch_lwip_gpio_unmask();
            return -1;
        }
        if (len <= freesize) {
            break;
        }
    };

    wizchip_send_data(buf, len);
    setSn_CR(Sn_CR_SEND);

    while (1) {
        uint8_t tmp = getSn_IR();
        if (tmp & Sn_IR_SENDOK) {
            setSn_IR(Sn_IR_SENDOK);
            // Packet sent ok
            break;
        } else if (tmp & Sn_IR_TIMEOUT) {
            setSn_IR(Sn_IR_TIMEOUT);
            // There was a timeout
            ethernet_arch_lwip_gpio_unmask();
            return -1;
        }
    }

    ethernet_arch_lwip_gpio_unmask();
    return len;
}
