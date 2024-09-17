#include "usb_fpga.h"
#include "util.h"

#include "esp_timer.h"

#include <string.h>

#include <stdint.h>
#include "driver/spi_master.h"

#define DEBUG_CNTX "usb-fpga"
#define USB_DEBUG 1

#define MAX_WRITE_TIME (1000 * 1000) //us

enum {
    kCMDWrite,
    kCMDRead
};

enum {
    kCMDData,
    kCMDRxCount,
    kCMDFlags,
    kCMDAddress,
    kCMDSetCMD
};

#define BUILD_CMD(r, cmd, args) (r << 7) | (cmd << 4) | (args & 0xf)


int usb_internal_read_flags(spi_device_handle_t spi, USBFlags_t *flags, size_t count, uint8_t start_endp) {
    uint8_t cmd = BUILD_CMD(kCMDRead, kCMDFlags, start_endp);
    esp_err_t ret;
    spi_transaction_t transaction = {
        .tx_buffer = &cmd,
        .rx_buffer = NULL,
        .length = 8,
        .flags = SPI_TRANS_CS_KEEP_ACTIVE
    };

    spi_device_acquire_bus(spi, portMAX_DELAY);
    ret = spi_device_transmit(spi, &transaction);
    if(ret != ESP_OK) {
        spi_device_release_bus(spi);
        return -1;
    }

    memset(&transaction, 0, sizeof(spi_transaction_t));
    transaction.rx_buffer = flags;
    transaction.length = count * 8;

    ret = spi_device_transmit(spi, &transaction);
    if(ret != ESP_OK) {
        spi_device_release_bus(spi);
        return -1;
    }

    spi_device_release_bus(spi);
    
    //check flags consistensy
    /*
    empty   full    consistent
    0       0       1 
    0       1       1
    1       0       1
    1       1       0
    this may happen only when there is a hardware comunication issue
    so ignore this flags by now
    */
    for (int i = 0; i < count; i ++)
        if ((flags[i].rx_empty && flags[i].rx_full) || (flags[i].tx_empty && flags[i].tx_full)) {
            DEBUG("Inconsistent flags!");
            return -1;
        }
    return 0;
}

int usb_internal_read_rx_count(spi_device_handle_t spi, uint16_t *count, uint8_t endp) {

    uint8_t cmd = BUILD_CMD(kCMDRead, kCMDRxCount, endp);
    esp_err_t ret;
    spi_transaction_t transaction = {
        .tx_buffer = &cmd,
        .rx_buffer = NULL,
        .length = 8,
        .flags = SPI_TRANS_CS_KEEP_ACTIVE
    };

    spi_device_acquire_bus(spi, portMAX_DELAY);
    ret = spi_device_transmit(spi, &transaction);
    if(ret != ESP_OK) {
        spi_device_release_bus(spi);
        return -1;
    }

    memset(&transaction, 0, sizeof(spi_transaction_t));
    transaction.rx_buffer = count;
    transaction.length = 2 * 8;

    ret = spi_device_transmit(spi, &transaction);
    if(ret != ESP_OK) {
        spi_device_release_bus(spi);
        return -1;
    }

    spi_device_release_bus(spi);
    return 0;
}

#define MAX_XFER_SIZE 16


int usb_internal_read_data(spi_device_handle_t spi, uint8_t *buffer, size_t count , uint8_t endp) {

    uint8_t cmd = BUILD_CMD(kCMDRead, kCMDData, endp);
    esp_err_t ret;
    spi_transaction_t transaction = {
        .tx_buffer = &cmd,
        .rx_buffer = NULL,
        .length = 8,
        .flags = SPI_TRANS_CS_KEEP_ACTIVE
    };

    spi_device_acquire_bus(spi, portMAX_DELAY);
    ret = spi_device_transmit(spi, &transaction);
    if(ret != ESP_OK) {
        spi_device_release_bus(spi);
        return -1;
    }


    int xfer_size = MAX_XFER_SIZE;

    while (count) {
        if (xfer_size > count) 
            xfer_size = count;

        memset(&transaction, 0, sizeof(spi_transaction_t));
        transaction.rx_buffer = buffer;
        transaction.length = xfer_size * 8;
        transaction.flags = xfer_size == MAX_XFER_SIZE ? SPI_TRANS_CS_KEEP_ACTIVE : 0;

        ret = spi_device_transmit(spi, &transaction);
        if(ret != ESP_OK) {
            spi_device_release_bus(spi);
            return -1;
        }

        count -= xfer_size;
        buffer += xfer_size;
    }

    spi_device_release_bus(spi);
    return 0;
}

int usb_internal_set_cmd(spi_device_handle_t spi, USBCMDs_t usb_cmd, uint8_t endp) {
    uint8_t cmd[2] = {BUILD_CMD(kCMDWrite, kCMDSetCMD, endp), usb_cmd};
    esp_err_t ret;
    spi_transaction_t transaction = {
        .tx_buffer = &cmd,
        .rx_buffer = NULL,
        .length = sizeof(cmd) * 8
    };

    ret = spi_device_transmit(spi, &transaction);
    if(ret != ESP_OK) {
        return -1;
    }
    return 0;
}

static int usb_internal_write_data(spi_device_handle_t spi, uint8_t *buffer, size_t count , uint8_t endp) {
    uint8_t cmd = BUILD_CMD(kCMDWrite, kCMDData, endp);
    esp_err_t ret;
    spi_transaction_t transaction = {
        .tx_buffer = &cmd,
        .rx_buffer = NULL,
        .length = 8,
        .flags = SPI_TRANS_CS_KEEP_ACTIVE
    };

    spi_device_acquire_bus(spi, portMAX_DELAY);
    ret = spi_device_transmit(spi, &transaction);
    if(ret != ESP_OK) {
        spi_device_release_bus(spi);
        return -1;
    }

    int xfer_size = MAX_XFER_SIZE;
    while (count) {

        if (xfer_size > count)
            xfer_size = count;


        memset(&transaction, 0, sizeof(spi_transaction_t));
        transaction.tx_buffer = buffer;
        transaction.length = xfer_size * 8;
        transaction.flags = xfer_size == MAX_XFER_SIZE ? xfer_size == count ? 0: SPI_TRANS_CS_KEEP_ACTIVE : 0;

        ret = spi_device_transmit(spi, &transaction);
        if(ret != ESP_OK) {
            spi_device_release_bus(spi);
            return -1;
        }
        count-= xfer_size;
        buffer += xfer_size;
    }

    spi_device_release_bus(spi);
    
    #if 0
    /*send 0 packet length to nofity end*/
    if (transaction.flags)
        usb_internal_set_cmd(spi, kUSBCMDSend0DataLength, endp);
    #endif

    return 0;
}

static int usb_internal_set_address(spi_device_handle_t spi, uint8_t address) {
    uint8_t cmd[2] = {BUILD_CMD(kCMDWrite, kCMDAddress, 0), address};
    esp_err_t ret;
    spi_transaction_t transaction = {
        .tx_buffer = &cmd,
        .rx_buffer = NULL,
        .length = sizeof(cmd) * 8
    };

    ret = spi_device_transmit(spi, &transaction);
    if(ret != ESP_OK) {
        return -1;
    }
    return 0;
}


////////////////////////////////////// top level implmentation of fpga driver /////////////////////////////////


struct {
    spi_device_handle_t spi;
    EndpCallback_t callbacks[FPGA_ENDPOINTS];
} g_fpga_config = {0};


void usb_init(spi_device_handle_t spi) {
    memset(&g_fpga_config, 0, sizeof(g_fpga_config));
    g_fpga_config.spi = spi;

    usb_set_address(0);
}

void usb_set_endp_handler(EndpCallback_t callback, uint8_t endp) {
    g_fpga_config.callbacks[endp] = callback;
}

int usb_write_data(uint8_t *buffer, size_t count, uint16_t chunk_size, uint8_t endp) {
    int ret;
    USBFlags_t flags;
    uint32_t start;
    
    #if USB_DEBUG
    DEBUG("Send on endp %i", endp);
    hexdump(stdout, buffer, count, 16, 8);
    #endif
    
    while (count) {

        start = esp_timer_get_time();

        while (esp_timer_get_time() - start < MAX_WRITE_TIME) {

            ret = usb_internal_read_flags(g_fpga_config.spi, &flags, 1, endp);

            if (ret) {
                DEBUG("Failed to read flags from endp %i", endp);
                return ret;
            }

            if (flags.tx_empty)
                goto xfer_chunk;
            
            vTaskDelay(1);
        }

        DEBUG("Send timeout");
        //timeout
        return -2;

        xfer_chunk:
        
        if (chunk_size > count)
            chunk_size = count;
        
        ret = usb_internal_write_data(g_fpga_config.spi, buffer, chunk_size, endp);
        if (ret) {
            DEBUG("Failed to xfer chunk");
            return ret;
        }

        buffer += chunk_size;
        count -= chunk_size;
    }

    return 0;
}

int usb_set_cmd(USBCMDs_t cmd, uint8_t endp) {
    return usb_internal_set_cmd(g_fpga_config.spi, cmd, endp);
}

int usb_set_address(uint8_t address) {
    return usb_internal_set_address(g_fpga_config.spi, address);
}

void usb_poll(void) {
    USBFlags_t flags[FPGA_ENDPOINTS] = {0};
    uint8_t buffer[FPGA_ENDP_SIZE];

    if (usb_internal_read_flags(g_fpga_config.spi, flags, FPGA_ENDPOINTS, 0)) { 
        DEBUG("Failed to read USB flags");
        return;
    }

    for (int i = 0; i < FPGA_ENDPOINTS; i++) {

        if(!flags[i].rx_empty) {
            uint16_t len = 0;
            usb_internal_read_rx_count(g_fpga_config.spi, &len, i);
            /*This may happen on a communication error*/
            if (!len) {
                DEBUG("Inconsistent len!");
                continue;
            }
            if (usb_internal_read_data(g_fpga_config.spi, buffer, len, i)) {
                DEBUG("Failed to read data in endpoint %i", i);
                continue;
            }
            #if USB_DEBUG
            DEBUG("Data on endp %i", i);
            hexdump(stdout, buffer, len, 16, 8);
            #endif
            if (g_fpga_config.callbacks[i]) {
                g_fpga_config.callbacks[i](i, buffer, len);
            }
        }
    }
}