/*
    Shared USB for the Raspberry Pi Pico RP2040
    Allows for multiple endpoints to share the USB controller

    Copyright (c) 2021 Earle F. Philhower, III <earlephilhower@yahoo.com>

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

#pragma once

#include <tusb.h>
#include <pico/mutex.h>

#ifdef __cplusplus
extern "C" {
#endif

enum DBG_PIN_REASON {
    USB_IRQ = 0,
    USB_NCM_TRY_PROCESS = 1,
    LWIP_NCM_RECV_IRQ = 2,
    LWIP_ETH_POLL = 3,
    NCM_TUD_NETWORK_RECV_CB = 4,
	NCM_RECV_IRQ_PENDING,
    
    LWIP_MUTEX,
	LWIP_MUTEX_TOO_OFTEN,
	LWIP_POLL_PENDING,
	LWIP_NEXT_TIMEOUT_AT_TIME_WORKER,
    
    LWIP_TCP_SLOWTMR,
    LWIP_TCP_SLOWTMR2,
    LWIP_TCP_FASTTMR,
    LWIP_TCP_FASTTMR2,
    LWIP_TCP_NETIF_IP_ADDR_CHANGED_PCBLIST,
    LWIP_TCP_NETIF_IP_ADDR_CHANGED_PCBLIST2,
    LWIP_TCP_INPUT,
    LWIP_TCP_INPUT2,
    LWIP_PBUF_FREE_OOSEQ,
    LWIP_PBUF_FREE_OOSEQ2,
    LWIP_TCP_INPUT_DELAYED_CLOSE,
    LWIP_TCP_INPUT_DELAYED_CLOSE2,
    LWIP_TCP_REMOVE,
    LWIP_TCP_REMOVE2,
	LWIP_MEM_TRIM,
	LWIP_ALIVE,
	LWIP_TCP_LISTS_OK,
    LWIP_TCP_CONNECT,
    LWIP_TCP_NEW,
	LWIP_MEMP_MALLOC,
	LWIP_MEMP_FREE,

    CLIENTCONTEXT_CLOSE,
    CLIENTCONTEXT_ERR_CB,
    CLIENT_CONNECT,
    CLIENT_PRINTLN,
    CLIENT_AVAILABLE,
    CLIENT_STOP,
	CLIENT_READ,
	NCM_TUD_NETWORK_RECV_RENEW_NORMAL,
	NCM_TUD_NETWORK_RECV_RENEW_QUEUE_FULL,
	NCM_TUD_NETWORK_RECV_RENEW_DISCARD,
	ETHERNET_HANDLEPACKETS,
    ETHERNET_READFRAMESIZE,
    ETHERNET_PBUF_ALLOC,
    ETHERNET_PBUF_FREE,
    ETHERNET_DISCARDFRAME,
    ETHERNET_READFRAMEDATA,
    ETHERNET_NETIF_INPUT,
	ETHERNET_HANDLEPACKETS_2ND_PACKET,
};

void debug_put(enum DBG_PIN_REASON reason, bool value);
void debug_put_gca(enum DBG_PIN_REASON reason, bool value);
void debug_toggle(enum DBG_PIN_REASON reason);
void debug_put_norecurse(enum DBG_PIN_REASON reason, bool value);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

/*d0 gray*/  
/*d1 brown*/ 
/*d2 red*/   
/*d3 orange*/
/*d4 yellow*/
/*d5 green */
/*d6 blue*/  
/*d7 purple*/
#define DEBUG_CHANNEL_MAP_SIZE 8
#define BIT(x) (1ull<<x)
inline constexpr uint64_t debug_chan_map[DEBUG_CHANNEL_MAP_SIZE] = {
	    BIT(USB_IRQ),
        BIT(ETHERNET_READFRAMESIZE)|BIT(ETHERNET_READFRAMEDATA),
        BIT(LWIP_NCM_RECV_IRQ)|BIT(LWIP_ETH_POLL),
        BIT(LWIP_MUTEX),
		BIT(LWIP_MEMP_MALLOC),
        BIT(LWIP_MEMP_FREE),
        BIT(CLIENTCONTEXT_CLOSE),
        BIT(CLIENTCONTEXT_ERR_CB),
};
         //BIT(CLIENT_CONNECT)|BIT(CLIENT_PRINTLN)|BIT(CLIENT_AVAILABLE)|BIT(CLIENT_STOP)|BIT(CLIENT_READ),
         //         BIT(NCM_TUD_NETWORK_RECV_RENEW_NORMAL)|BIT(NCM_TUD_NETWORK_RECV_RENEW_QUEUE_FULL)|BIT(NCM_TUD_NETWORK_RECV_RENEW_DISCARD),
//BIT(LWIP_TCP_SLOWTMR)|BIT(LWIP_TCP_FASTTMR)|BIT(LWIP_TCP_NETIF_IP_ADDR_CHANGED_PCBLIST)|BIT(LWIP_TCP_INPUT)|BIT(LWIP_PBUF_FREE_OOSEQ)|BIT(LWIP_TCP_INPUT_DELAYED_CLOSE)|BIT(LWIP_TCP_REMOVE),
         //BIT(LWIP_TCP_SLOWTMR2)|BIT(LWIP_TCP_FASTTMR2)|BIT(LWIP_TCP_NETIF_IP_ADDR_CHANGED_PCBLIST2)|BIT(LWIP_TCP_INPUT2)|BIT(LWIP_PBUF_FREE_OOSEQ2)|BIT(LWIP_TCP_INPUT_DELAYED_CLOSE2)|BIT(LWIP_TCP_REMOVE2),
inline constexpr int debug_chan_pins[DEBUG_CHANNEL_MAP_SIZE] = {
		18, 19, 20, 21, 15, 14, 13, 12
};
class USBClass {
public:
    USBClass() { }

    // Called by an object at global init time to register a HID device, returns a localID to be mapped using findHIDReportID
    // pidMask is the bits in the PID that should be XOR'd when this device is present.
    // 0 means don't invert anything, OTW select a single bitmask 1<<n.
    uint8_t registerHIDDevice(const uint8_t *descriptor, size_t len, int ordering, uint32_t pidMask);

    // Remove a HID device from the USB descriptor.  Only call after usbDisconnect or results could be unpredictable!
    void unregisterHIDDevice(unsigned int localid);

    // Called by an object at global init time to add a new interface (non-HID, like CDC or Picotool)
    uint8_t registerInterface(int interfaces, void (*cb)(int itf, uint8_t *dst, int len, void *param), void *param, size_t len, int ordering, uint32_t pidMask);

    // Remove a USB interface from the USB descriptor.  Only call after usbDisconnect or results could be unpredictable!
    void unregisterInterface(unsigned int localid);

    // Get the USB HID actual report ID from the localid
    uint8_t findHIDReportID(unsigned int localid);

    // Get the USB interface number from the localid
    uint8_t findInterfaceID(unsigned int localid);

    // Register a string for a USB descriptor
    uint8_t registerString(const char *str);

    // Get an unassigned in/cmd or out endpoint number
    uint8_t registerEndpointIn();
    uint8_t registerEndpointOut();
    void unregisterEndpointIn(int ep);
    void unregisterEndpointOut(int ep);

    // Disconnects the USB connection to allow editing the HID/interface list
    void disconnect();

    // Reconnects the USB connection to pick up the new descriptor
    void connect();

    // Override the hardcoded USB VID:PID, product, manufacturer, and serials
    void setVIDPID(uint16_t vid, uint16_t pid);
    void setManufacturer(const char *str);
    void setProduct(const char *str);
    void setSerialNumber(const char *str);

    // Called by main() to init the USB HW/SW.
    void begin();

    // Helper class for HID report sending with wait and timeout
    bool HIDReady();

    // Can't have multiple cores updating the TUSB state in parallel
    mutex_t mutex;

    // TinyUSB callbacks call bare C functions which jump to these
    const uint8_t *tud_descriptor_device_cb();
    const uint8_t *tud_descriptor_configuration_cb(uint8_t index);
    const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid);
    uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance);

#ifdef __FREERTOS
    // Should probably use a semaphore or something, but this works for now
    volatile bool initted = false;
#else
    // The user IRQ for the USB "task"
    uint8_t usbTaskIRQ;
#endif

    // Simple 1-interface updated for "easy" interfaces like Picotool or HIF
    static void simpleInterface(int itf, uint8_t *dst, int len, void *data) {
        memcpy(dst, data, len);
        dst[2] = itf; // Set the interface
    }

private:
    // We can't use non-trivial variables to hold the hid, interface, or string lists.  The global
    // initialization where things like the global Keyboard may be called before the non-trivial
    // objects (i.e. no std::vector).

    // Either a USB interface or HID device descriptor, kept in a linked list
    typedef struct Entry {
        void (*cb)(int itf, uint8_t *dst, int len, void *data); // unused for HID, only the report ID needs updating and we can do that inline
        const void *param; // CB param or HID descriptor
        unsigned int len        : 12;
        unsigned int interfaces : 4;
        unsigned int order      : 18;
        unsigned int localid    : 6;
        uint32_t mask;
        struct Entry *next;
    } Entry;

    // Add or remove Entry in a linked list, keeping things ordered by ordering
    uint8_t addEntry(Entry **head, int interfaces, void (*cb)(int itf, uint8_t *dst, int len, void *param), const void *param, size_t len, int ordering, uint32_t pidMask);
    void removeEntry(Entry **head, unsigned int localid);

    // Find the index (HID report ID or USB interface) of a given localid
    unsigned int findID(Entry *head, unsigned int localid);

    // Generate the binary blob for the device descriptor and HID reports
    void setupDescHIDReport();
    void setupUSBDescriptor();

    // Gets a pointer to the HID report structure, optionally returning the size in len
    uint8_t *getDescHIDReport(int *len);

    Entry *_hids = nullptr;
    Entry *_interfaces = nullptr;

    // USB strings kept in a list of pointers.  Can't use std::vector again because of CRT init non-ordering.
    const char **usbd_desc_str = nullptr;
    uint8_t usbd_desc_str_cnt = 0;
    uint8_t usbd_desc_str_alloc = 0;

    // HID report
    uint8_t _hid_interface = (unsigned uint8_t) -1;
    uint8_t _hid_endpoint = 0;
    uint16_t _hid_report_len = 0;
    uint8_t *_hid_report     = nullptr;

    // Global USB descriptor
    uint8_t *usbd_desc_cfg = nullptr;
    uint16_t usbd_desc_cfg_len = 0;

    // Available bitmask for endpoints, can never be EP 0
    uint16_t _endpointIn = 0xfffe;
    uint16_t _endpointOut = 0xfffe;

    // Overrides for the USB ID/etc.
    uint16_t _forceVID = 0;
    uint16_t _forcePID = 0;
    uint8_t _forceManuf = 0;
    uint8_t _forceProd = 0;
    uint8_t _forceSerial = 0;

    // USB device descriptor
    tusb_desc_device_t usbd_desc_device;

    // Periodic task
#ifdef __FREERTOS
    static void freertosUSBTask(void *param);
#else
    static void usbIRQ();
    static int64_t timerTask(__unused alarm_id_t id, __unused void *user_data);
#endif

};

extern USBClass USB;

#endif
