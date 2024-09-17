#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h" // Para la configuracion y manejo de GPIOs
#include "esp_timer.h"
#include "util.h"
#include "usb_fpga.h"
#include "usb.h"

#define PIN_NUM_MISO 12
#define PIN_NUM_MOSI 15
#define PIN_NUM_CLK 13
#define PIN_NUM_CS 2
#define PIN_BUTTON_UP GPIO_NUM_32    // Asigna el pin adecuado para el boton Up
#define PIN_BUTTON_LEFT GPIO_NUM_25  // Asigna el pin adecuado para el boton Left
#define PIN_BUTTON_RIGHT GPIO_NUM_26 // Asigna el pin adecuado para el boton Right

#define SPI_HOST SPI2_HOST

#define ENDPOINTS 5

#define DEBUG_CNTX "main"

// Descriptor HID para un teclado
uint8_t hid_report_descriptor[] = {
  0x05, 0x01, // USAGE_PAGE (Generic Desktop)
    0x09, 0x06, // USAGE (Keyboard)
    0xa1, 0x01, // COLLECTION (Application)
    0x05, 0x07, //   USAGE_PAGE (Key Codes)
    0x19, 0xE0, //   USAGE_MINIMUM (Left Control)
    0x29, 0xE7, //   USAGE_MAXIMUM (Right GUI)
    0x15, 0x00, //   LOGICAL_MINIMUM (0)
    0x25, 0x01, //   LOGICAL_MAXIMUM (1)
    0x75, 0x01, //   REPORT_SIZE (1)
    0x95, 0x08, //   REPORT_COUNT (8)
    0x81, 0x02, //   INPUT (Data,Var,Abs)
    0x75, 0x08, //   REPORT_SIZE (8)
    0x95, 0x06, //   REPORT_COUNT (6)
    0x15, 0x00, //   LOGICAL_MINIMUM (0)
    0x25, 0xFF, //   LOGICAL_MAXIMUM (255)
    0x05, 0x07, //   USAGE_PAGE (Key Codes)
    0x19, 0x00, //   USAGE_MINIMUM (No Event)
    0x29, 0xFF, //   USAGE_MAXIMUM (Max Event)
    0x81, 0x00, //   INPUT (Data,Ary,Abs)
    0xC0,       // END_COLLECTION
 };

typedef enum
{
    kHIDRequestSetIdle = 0xa
} HIDRequest_t;

bool g_hid_running = false;

// Manejador de solicitudes de control HID
void hid_control_handler(USBControlRequest_t *control, uint16_t chunck_size, uint8_t endp)
{
    if (control->request_type.type == kTypeStandard)
    {
        switch (control->request)
        {
        case kRequestGetDescriptor:
            if (control->descriptor.type == 0x22)
            {
                usb_write_data(hid_report_descriptor, sizeof(hid_report_descriptor), chunck_size, endp);
                DEBUG("Report descriptor sent");
            }
            break;
        default:
            DEBUG("Unsupported standard request %u", control->request);
            goto deny_request;
        }
    }
    else
    {
        switch ((HIDRequest_t)control->request)
        {
        case kHIDRequestSetIdle:
            usb_control_accept_request(endp);
            g_hid_running = true;
            break;
        default:
            DEBUG("Unsupported HID request %u", control->request);
            goto deny_request;
        }
    }
    return;

deny_request:
    usb_control_deny_request(endp);
}

// Funcion para enviar el estado del teclado
void hid_send_keyboard_state(uint8_t modifier, uint8_t reserved, uint8_t keycode[6])
{
    uint8_t buffer[7] = {modifier, keycode[0], keycode[1], keycode[2], keycode[3], keycode[4], keycode[5]};

    if (usb_write_data(buffer, sizeof(buffer), 64, 2))
    {
        DEBUG("Failed to send keyboard state");
        g_hid_running = false;
    }
}

void app_main()
{
    esp_err_t ret;
    spi_device_handle_t usb_spi;
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 128,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1 * 1000 * 1000, // Clock speed 1 MHz
        .mode = 3,                         // SPI Mode 3
        .spics_io_num = PIN_NUM_CS,        // CS pin
        .queue_size = 100,
        .cs_ena_pretrans = 1};

    // Inicializa el bus SPI
    ret = spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ASSERT(ret == ESP_OK);

    // Añade el dispositivo SPI
    ret = spi_bus_add_device(SPI_HOST, &devcfg, &usb_spi);
    ASSERT(ret == ESP_OK);

    // Inicializa el USB
    usb_init(usb_spi);

    // Configura el descriptor del dispositivo
    DeviceDescriptor_t device_descriptor = {
        .class = 0x00, // Controlador USB genérico
        .sub_class = 0x00,
        .protocol = 0x00,
        .packet_size = 64,
        .vendor_id = 0x16c0,
        .product_id = 0x27da,
        .device_version = 0x100,
        .str_index_manufacturer = 0,
        .str_index_product = 0,
        .str_index_serial_number = 0,
    };

    // Configuracion del dispositivo USB
    ConfigurationDescriptor_t default_config = {
        .attributes = 0x80, // Configuración de bus de energía
        .max_power = 50,    // 100mA
        .str_index_configuration = 0};

    // Descripcion de la interfaz HID
    InterfaceDescriptor_t hid_interface = {
        .interface_id = 0,
        .endpoints_count = 2,
        .class = 0x03, // HID
        .str_index_interface = 0};

    // Descripcion de los endpoints
    EndpointDescriptor_t endp1 = {
        .endp_address = 1 | kEndpointDirectionOut,
        .attributes = kEndpointAttributeInterrupt,
        .max_packet_size = 64,
        .interval = 10};
    EndpointDescriptor_t endp2 = {
        .endp_address = 2 | kEndpointDirectionIn,
        .attributes = kEndpointAttributeInterrupt,
        .max_packet_size = 64,
        .interval = 10};

    // Descriptor de clase HID
    struct
    {
        uint8_t length;
        uint8_t type;
        uint16_t hid_version;
        uint8_t country_code;
        uint8_t class_descriptors;
        uint8_t class_descriptor_type;
        uint16_t class_descriptor_length;
    } PACKED hid_class_descriptor = {
        .length = 0x9,
        .type = 0x21,                  // HID class descriptor
        .hid_version = 0x0110,         // Version 1.10
        .country_code = 0,             // Usually 0
        .class_descriptors = 1,        // Usually 1
        .class_descriptor_type = 0x22, // Report descriptor
        .class_descriptor_length = sizeof(hid_report_descriptor)};

    // Configura los descriptores del USB
    usb_set_device_descriptor(&device_descriptor);
    usb_add_configuration_descriptor(&default_config);
    usb_add_interface_descriptor(&hid_interface);
    usb_add_endppoint_descriptor(&endp1);
    usb_add_endppoint_descriptor(&endp2);
    usb_add_class_descriptor((uint8_t *)&hid_class_descriptor, sizeof(hid_class_descriptor));
    usb_add_class_control_handler(hid_control_handler);

    // Configura el manejador del endpoint de control USB
    usb_set_endp_handler(usb_control_endp, 0);

    // Configura los pines de los botones
    gpio_set_direction(PIN_BUTTON_UP, GPIO_MODE_INPUT);
    gpio_set_direction(PIN_BUTTON_LEFT, GPIO_MODE_INPUT);
    gpio_set_direction(PIN_BUTTON_RIGHT, GPIO_MODE_INPUT);

    gpio_set_pull_mode(PIN_BUTTON_UP, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_BUTTON_LEFT, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_BUTTON_RIGHT, GPIO_PULLUP_ONLY);

    uint64_t ref_time = esp_timer_get_time();
    bool toggle = 0;
    while (1)
    {
        usb_poll();
        if (!g_hid_running)
            continue;

        if (esp_timer_get_time() - ref_time > 100 * 1000)
        {
            ref_time = esp_timer_get_time();

            uint8_t keycode[6] = {0};

            // Leer el estado de los botones y asignar teclas
            if (gpio_get_level(PIN_BUTTON_UP) == 0) // Boton presionado (con pull-up, nivel bajo significa presionado)
            {
                 DEBUG("SE PRESIONO DOWN ARROW");
                keycode[0] = 0x52; // Codigo de tecla para Up Arrow
            }
            else if (gpio_get_level(PIN_BUTTON_LEFT) == 0)
            {
                 DEBUG("SE PRESIONO LEFT ARROW");
                keycode[0] = 0x50; // Codigo de tecla para Left Arrow
            }
            else if (gpio_get_level(PIN_BUTTON_RIGHT) == 0)
            {
                 DEBUG("SE PRESIONO RIGHT ARROW");
                keycode[0] = 0x4F; // Codigo de tecla para Right Arrow
            }

            // Enviar el estado del teclado
            hid_send_keyboard_state(0, 0, keycode);
        }
    }
}
