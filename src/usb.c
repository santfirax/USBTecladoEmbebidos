
#include "usb.h"
#include "usb_fpga.h"
#include "util.h"

#include <string.h>

#define DEBUG_CNTX "usb"

#define MAX_CONFIGURATION 1
#define MAX_INTERFACES 5




DeviceDescriptor_t *g_device_descriptor = NULL;

struct {
    ConfigurationDescriptor_t *descriptor;
    struct {
        InterfaceDescriptor_t *descriptor;
        EndpointDescriptor_t *endpoints[FPGA_ENDPOINTS];
        uint8_t *class_descriptor;
        size_t class_size;
        ControlHandler_t class_handler;
    } interface_tree[MAX_INTERFACES];
    
} g_config_tree[MAX_CONFIGURATION] = {0};
uint8_t g_config_used = 0;
uint8_t g_config_selected = 0;

void usb_control_deny_request(uint8_t endp);
void usb_control_accept_request(uint8_t endp);


void usb_control_endp(uint8_t endp, uint8_t *buffer, size_t len) {
    USBControlRequest_t *control = (USBControlRequest_t *) buffer;
    int ret;
    if (len <= 2) {
        DEBUG("Got 0 packet rx");
        return;
    }

    //foward all class control request to class handlders
    if(control->request_type.type == kTypeClass) {
        goto foward_request;
    }

    switch (control->request) {
    case kRequestGetDescriptor:
        switch (control->descriptor.type) {
            case kDescriptorDevice:
                if (control->descriptor.index != 0) {
                    DEBUG("Requested descriptor != 0");
                    goto deny_request; 
                }
                ret = usb_write_data((uint8_t *)g_device_descriptor, sizeof(DeviceDescriptor_t), g_device_descriptor->packet_size, endp);
                if (ret) {
                    DEBUG("Failed to send device descriptor");
                    return;
                }
                DEBUG("Device descriptor sent");
            break;

            case kDescriptorConfiguration: {
                uint16_t xfer_len, config_length, build_size, copy_size;
                uint8_t *buffer, *build;
                ConfigurationDescriptor_t *config;
                InterfaceDescriptor_t *interface;
                EndpointDescriptor_t *endpoint;
                uint8_t *class;
                
                if (control->descriptor.index > g_config_used) {
                    DEBUG("Requested configuration unknown %i, configured %i", control->descriptor.index, g_config_used);
                    goto deny_request; 
                }

                config = g_config_tree[control->descriptor.index].descriptor;
                xfer_len = control->descriptor.length;
                config_length = g_config_tree[control->descriptor.index].descriptor->total_length;
                if (xfer_len > config_length) {
                    xfer_len = config_length;
                }

                buffer = malloc(xfer_len);
                if (!buffer) {
                    DEBUG("Failed to alloc memory to xfer descriptor");
                    goto deny_request;
                }
                build = buffer;
                build_size = xfer_len;

                //copy the config itself
                copy_size = sizeof(ConfigurationDescriptor_t);
                if (copy_size > build_size) copy_size = build_size;

                memcpy(build, config, copy_size);
                build += copy_size; build_size -= copy_size;

                for (int i = 0; i < config->interfaces_count; i++) {
                    interface = g_config_tree[control->descriptor.index].interface_tree[i].descriptor;
                   
                    copy_size = sizeof(InterfaceDescriptor_t);
                    if (copy_size > build_size) copy_size = build_size;

                    memcpy(build, interface, copy_size);
                    build += copy_size; build_size -= copy_size;
                    
                    //class descriptor shall go before endpoints descriptors 
                    class = g_config_tree[control->descriptor.index].interface_tree[i].class_descriptor;
                    copy_size = g_config_tree[control->descriptor.index].interface_tree[i].class_size;
                    if (copy_size > build_size) copy_size = build_size;

                    memcpy(build, class, copy_size);
                    build += copy_size; build_size -= copy_size;

                    for (int j = 0; j < interface->endpoints_count; j++) {
                        endpoint = g_config_tree[control->descriptor.index].interface_tree[i].endpoints[j];
                        copy_size = sizeof(EndpointDescriptor_t);
                        if (copy_size > build_size) copy_size = build_size;

                        memcpy(build, endpoint, copy_size);
                        build += copy_size; build_size -= copy_size;
                    }

                }

                ret = usb_write_data(buffer, xfer_len, g_device_descriptor->packet_size, endp);
                free(buffer);
                if (ret) {
                    DEBUG("Failed to send config descriptor");
                    return;
                }
                DEBUG("Device config sent");



            } break;
            default:

                //check if maybe it is a class type
                if ((control->descriptor.type & 0b1100000) == 0b100000) {
                    goto foward_request;
                }

                DEBUG("Requested descriptor %u not supported", control->descriptor.type);
                goto deny_request; 
            break;
        }
    break;

    case kRequestSetAddress:
        usb_control_accept_request(endp);
        usb_set_address(control->address.value);
        DEBUG("New USB address %u", control->address.value);
    break;

    case kRequestSetConfiguration:
        if (control->configuration.id > g_config_used) {
            DEBUG("Requested an invalid configuration");
            goto deny_request;
        }
        g_config_selected = control->configuration.id - 1; //id start from 0
        usb_control_accept_request(endp);
        DEBUG("Configuration %i set", g_config_selected);
    break;

    case kRequestSetFeature:
        usb_control_accept_request(endp);
        DEBUG("Set feature request. This is a dummy!!");
    break;

    default:
        goto deny_request; 
    break;
    }

    return;

    deny_request:
    usb_control_deny_request(endp);
    return;

    foward_request:
    DEBUG("Request forwarded");
    ConfigurationDescriptor_t *config = g_config_tree[g_config_selected].descriptor;
    for (int i = 0; i < config->interfaces_count; i++) {
        ControlHandler_t handler = g_config_tree[g_config_selected].interface_tree[i].class_handler;
        if (handler) {
            handler(control, g_device_descriptor->packet_size, endp);
        }
    }
}

void usb_set_device_descriptor(DeviceDescriptor_t *descriptor) {
    ASSERT(descriptor != NULL);
    g_device_descriptor = descriptor;
    g_device_descriptor->type = kDescriptorDevice;
    g_device_descriptor->length = sizeof(DeviceDescriptor_t);
    g_device_descriptor->configurations = 0;
    g_device_descriptor->usb_version = 0x200;
}

void usb_add_configuration_descriptor(ConfigurationDescriptor_t *descriptor) {
    ASSERT(descriptor != NULL);
    ASSERT(g_device_descriptor != NULL);

    g_config_tree[g_config_used].descriptor = descriptor;
    descriptor->length = sizeof(ConfigurationDescriptor_t);
    descriptor->type = kDescriptorConfiguration;

    descriptor->total_length = descriptor->length;
    descriptor->interfaces_count = 0;
    descriptor->config_id = ++g_config_used;
    descriptor->attributes |= kConfigAttributeDefault; //Ensure minimun

    g_device_descriptor->configurations++;
}



void usb_add_interface_descriptor(InterfaceDescriptor_t *descriptor) {
    ASSERT(descriptor != NULL);
    ASSERT(g_device_descriptor != NULL);
    ASSERT(g_config_used > 0);

    uint8_t config_index = g_config_used - 1;

    descriptor->length = sizeof(InterfaceDescriptor_t);
    descriptor->type = kDescriptorInterface;

    descriptor->alternate_settings = config_index;
    descriptor->endpoints_count = 0;

    uint8_t interface_index = g_config_tree[config_index].descriptor->interfaces_count++;
    g_config_tree[config_index].interface_tree[interface_index].descriptor = descriptor;
    g_config_tree[config_index].descriptor->total_length += descriptor->length;

}

void usb_add_endppoint_descriptor(EndpointDescriptor_t *descriptor) {
    ASSERT(descriptor != NULL);
    ASSERT(g_device_descriptor != NULL);
    ASSERT(g_config_used > 0);
    
    uint8_t config_index = g_config_used - 1;
    ASSERT(g_config_tree[config_index].descriptor->interfaces_count > 0);
    
    uint8_t interface_index = g_config_tree[config_index].descriptor->interfaces_count - 1;
    uint8_t endp_index = g_config_tree[config_index].interface_tree->descriptor->endpoints_count++;

    descriptor->length = sizeof(EndpointDescriptor_t);
    descriptor->type = kDescriptorEnpoint;

    g_config_tree[config_index].interface_tree[interface_index].endpoints[endp_index] = descriptor;
    g_config_tree[config_index].descriptor->total_length += descriptor->length;

}

void usb_add_class_descriptor(uint8_t *descriptor, size_t length) {
    ASSERT(descriptor != NULL);
    ASSERT(g_device_descriptor != NULL);
    ASSERT(g_config_used > 0);

    uint8_t config_index = g_config_used - 1;
    ASSERT(g_config_tree[config_index].descriptor->interfaces_count > 0);
    
    uint8_t interface_index = g_config_tree[config_index].descriptor->interfaces_count - 1;

    g_config_tree[config_index].interface_tree[interface_index].class_descriptor = descriptor;
    g_config_tree[config_index].interface_tree[interface_index].class_size = length;

    g_config_tree[config_index].descriptor->total_length += length;
}

void usb_add_class_control_handler(ControlHandler_t handler) {
    ASSERT(handler != NULL);
    ASSERT(g_device_descriptor != NULL);
    ASSERT(g_config_used > 0);
    
    uint8_t config_index = g_config_used - 1;
    ASSERT(g_config_tree[config_index].descriptor->interfaces_count > 0);
    
    uint8_t interface_index = g_config_tree[config_index].descriptor->interfaces_count - 1;
    g_config_tree[config_index].interface_tree[interface_index].class_handler = handler;
}


void usb_control_deny_request(uint8_t endp) {
    usb_set_cmd(kUSBCMDSendStall, endp);
}
void usb_control_accept_request(uint8_t endp){
    usb_set_cmd(kUSBCMDSend0DataLength, endp);
}