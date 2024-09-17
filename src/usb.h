


#ifndef USB_H_
#define USB_H_

#include <stdint.h>
#include <stdlib.h>

#define PACKED __attribute__((packed))

typedef enum {
    kDescriptorDevice = 1,
    kDescriptorConfiguration,
    kDescriptorString,
    kDescriptorInterface,
    kDescriptorEnpoint,
    kDescriptorDeviceQualifier,
    kDescriptorOtherSpeedConfiguration,
    kDescriptorInterfacePower
} DescriptorTypes_t;

typedef struct {
    uint8_t length;
    DescriptorTypes_t type:8;
    uint16_t usb_version; // 0x200 -> v02.00
    uint8_t class; //usually 0
    uint8_t sub_class; //usually 0
    uint8_t protocol; //usually 0
    uint8_t packet_size; //8, 16, 32, 64. Refers to endp0
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_version; //BCD coded
    uint8_t str_index_manufacturer;
    uint8_t str_index_product;
    uint8_t str_index_serial_number;
    uint8_t configurations; //usually 1
} PACKED DeviceDescriptor_t;


enum {
    kConfigAttributeDefault = 0b10000000,
    kConfigAttributeSelfPower = 0b01000000,
    kConfigAttributeRemoteWakeup= 0b00100000,
};

typedef struct {
    uint8_t length;
    DescriptorTypes_t type:8;
    uint16_t total_length; // config + interface  + endp + class
    uint8_t interfaces_count;
    uint8_t config_id;
    uint8_t str_index_configuration;

    uint8_t attributes;
    uint8_t max_power; // x2 mA

} PACKED ConfigurationDescriptor_t;



typedef struct {
    uint8_t length;
    DescriptorTypes_t type:8;

    uint8_t interface_id;
    uint8_t alternate_settings; //to which config its realted to
    uint8_t endpoints_count;
    uint8_t class; 
    uint8_t sub_class; 
    uint8_t protocol; 
    uint8_t str_index_interface;
} PACKED InterfaceDescriptor_t;


enum {
    kEndpointDirectionOut,
    kEndpointDirectionIn = 0b10000000
};
enum {
    kEndpointAttributeControl,
    kEndpointAttributeIsochronous,
    kEndpointAttributeBulk,
    kEndpointAttributeInterrupt,
};

typedef struct {
    uint8_t length;
    DescriptorTypes_t type:8;

    uint8_t endp_address;
    uint8_t attributes;
    uint16_t max_packet_size;
    uint8_t interval;
} PACKED EndpointDescriptor_t;




typedef enum {
    kDevice2Host,
    kHost2Device,
} RequestDirection_t;

typedef enum {
    kTypeStandard,
    kTypeClass,
    kTypeVendor
} RequestType_t;

typedef enum {
    kRecipientDevice,
    kRecipientInterface,
    kRecipientEndpoint,
    kRecipientOther
} RequestRecipient_t;

/*Table 9-4*/
typedef enum {
    kRequestGetStatus,
    kRequestClearFeature,
    kRequestSetFeature = 3,
    kRequestSetAddress = 5, 
    kRequestGetDescriptor,
    kRequestSetDescriptor,
    kRequestGetConfiguration,
    kRequestSetConfiguration,
    kRequestGetInterface,
    kRequestSetInterface,
    kRequestSynchFrame
} RequestCodes_t;


typedef struct {
    struct  {
        /*table 9-2*/
        RequestRecipient_t recipient:5;
        RequestType_t type:2;
        RequestDirection_t direction:1;
    } PACKED request_type;
    
    RequestCodes_t request:8;
    
    
    union {
        struct {
            uint16_t value;
            uint16_t index;
            uint16_t length;
        } PACKED generic;
        
        struct {
            struct {
                uint8_t index;
                DescriptorTypes_t type:8;
            } PACKED; //value

            uint16_t language_id; //index
            uint16_t length;
        } PACKED descriptor;

        struct {
            uint8_t id;            
        } configuration;
        

        struct {
            uint8_t value:7;
        } PACKED address;
        
    } PACKED;


} PACKED USBControlRequest_t;


typedef void (*ControlHandler_t)(USBControlRequest_t *, uint16_t chunk_size, uint8_t endp);

void usb_add_class_control_handler(ControlHandler_t handler);
void usb_add_class_descriptor(uint8_t *descriptor, size_t length);
void usb_add_endppoint_descriptor(EndpointDescriptor_t *descriptor);
void usb_add_interface_descriptor(InterfaceDescriptor_t *descriptor);
void usb_add_configuration_descriptor(ConfigurationDescriptor_t *descriptor);
void usb_set_device_descriptor(DeviceDescriptor_t *decriptor); 
void usb_control_endp(uint8_t endp, uint8_t *buffer, size_t len);



void usb_control_deny_request(uint8_t endp);
void usb_control_accept_request(uint8_t endp);


#endif