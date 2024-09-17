

#ifndef USB_FPGA_H_
#define USB_FPGA_H_

#include <stdlib.h>
#include <stdint.h>
#include "driver/spi_master.h"


#define FPGA_ENDPOINTS 5
#define FPGA_ENDP_SIZE 1024

typedef struct {
    uint8_t rx_full:1;
    uint8_t rx_empty:1;
    uint8_t tx_full:1;
    uint8_t tx_empty:1;
    uint8_t :4;
} __attribute__((packed)) USBFlags_t;

typedef enum {
    kUSBCMDNone,
    kUSBCMDSendStall,
    kUSBCMDSend0DataLength
} USBCMDs_t;

typedef void (*EndpCallback_t)(uint8_t endp, uint8_t *buffer, size_t size);

void usb_init(spi_device_handle_t spi);
void usb_set_endp_handler(EndpCallback_t callback, uint8_t endp);
int usb_write_data(uint8_t *buffer, size_t count, uint16_t chunk_size, uint8_t endp);
int usb_set_cmd(USBCMDs_t cmd, uint8_t endp);
int usb_set_address(uint8_t address);
void usb_poll(void);

#endif