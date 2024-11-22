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

class NCMEthernet {
public:
    /**
        Constructor
    */
    NCMEthernet();

    /**
        Initialise the Ethernet controller
        Must be called before sending or receiving Ethernet frames

        @param address the local MAC address for the Ethernet interface
        @return Returns true if setting up the Ethernet interface was successful
    */
    bool begin(const uint8_t* address, netif *net);

    /**
        Shut down the Ethernet controlled
    */
    void end();

    /**
        Send an Ethernet frame
        @param data a pointer to the data to send
        @param datalen the length of the data in the packet
        @return the number of bytes transmitted
    */
    uint16_t sendFrame(const uint8_t* data, uint16_t datalen);

    /**
        Read an Ethernet frame
        @param buffer a pointer to a buffer to write the packet to
        @param bufsize the available space in the buffer
        @return the length of the received packet
               or 0 if no packet was received
    */
    uint16_t readFrame(uint8_t* buffer, uint16_t bufsize);

    /**
        Check physical link
        @return true when physical link is up
    */
    bool isLinked() {
        //todo
        return true;
    }

    /**
        Report whether ::isLinked() API is implemented
        @return true when ::isLinked() API is implemented
    */
    constexpr bool isLinkDetectable() const {
        return true;
    }

    constexpr bool needsSPI() const {
        return false;
    }

protected:
    static constexpr bool interruptIsPossible() {
        return false;
    }

    static constexpr PinStatus interruptMode() {
        return LOW;
    }

    /**
        Read an Ethernet frame size
        @return the length of data do receive
               or 0 if no frame was received
    */
    uint16_t readFrameSize();

    /**
        discard an Ethernet frame
        @param framesize readFrameSize()'s result
    */
    void discardFrame(uint16_t framesize);

    /**
        Read an Ethernet frame data
           readFrameSize() must be called first,
           its result must be passed into framesize parameter
        @param buffer a pointer to a buffer to write the frame to
        @param framesize readFrameSize()'s result
        @return the length of the received frame
               or 0 if a problem occurred
    */
    uint16_t readFrameData(uint8_t* frame, uint16_t framesize);

    bool tud_network_recv_cb(const uint8_t *src, uint16_t size);
    void tud_network_init_cb(void);

private:
    netif *_netif;
    struct pbuf *received_frame;

};

#endif  // NCM_ETHERNET_H
