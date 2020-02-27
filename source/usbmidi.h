/*
 * usbmidi.h
 *
 *	Revision 1
 *
 *  Copyright 2020 Brady Etz, aka Wandering Sounds
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without modification,
 *  are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *
 *  3. Neither the name of the copyright holder nor the names of its contributors
 *     may be used to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 *  OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 *  OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef USBMIDI_H_
#define USBMIDI_H_

#define CONTROLLER_ID 						kUSB_ControllerEhci0
#define USB_HOST_INTERRUPT_PRIORITY 		3U
#define MIDI_IN_BUFFER_SIZE 				4U

#define USB_AUDIO_CLASS_CODE				0x01
#define USB_AUDIO_SUBCLASS_UNDEFINED 		0x00
#define USB_AUDIO_SUBCLASS_CONTROL			0x01
#define USB_AUDIO_SUBCLASS_STREAMING		0x02
#define USB_AUDIO_SUBCLASS_MIDISTREAMING	0x03
#define USB_AUDIO_PROTOCOL_V01_00			0x00
#define USB_AUDIO_PROTOCOL_V02_00			0x20
#define USB_AUDIO_PROTOCOL_V03_00			0x30

#include "usb_host_config.h"
#include "usb_host.h"
#include "fsl_device_registers.h"
#include "usb_host_ehci.h"
#include "usb_host_cdc.h"
#include "usb_host_devices.h"
#include "board.h"
#include "fsl_common.h"
#include "fsl_debug_console.h"

#if ((!USB_HOST_CONFIG_KHCI) && (!USB_HOST_CONFIG_EHCI) && (!USB_HOST_CONFIG_OHCI) && (!USB_HOST_CONFIG_IP3516HS))
#error Please enable USB_HOST_CONFIG_KHCI, USB_HOST_CONFIG_EHCI, USB_HOST_CONFIG_OHCI, or USB_HOST_CONFIG_IP3516HS in file usb_host_config.
#endif

#include "pin_mux.h"
#include "usb_phy.h"
#include "clock_config.h"


/*! @brief host app device attach/detach status, from example host applications */
typedef enum _usb_host_app_state
{
    kStatus_DEV_Idle = 0, /* no pending attach/detach */
    kStatus_DEV_Attached, /* device was just attached */
    kStatus_DEV_Detached, /* device was just detached */
} usb_host_app_state_t;

/*
 * MIDI descriptors, per the USB Device Class Definition for MIDI Devices
 */
typedef struct _usbmidi_descriptor_device
{
    uint8_t bLength;            /* Size of this descriptor, in bytes; 0x12 */
    uint8_t bDescriptorType;    /* DEVICE descriptor; 0x01 */
    uint8_t bcdUSB[2];          /* current revision of USB specification */
    uint8_t bDeviceClass;       /* Device defined at Interface level; 0x00 */
    uint8_t bDeviceSubClass;    /* Unused; 0x00 */
    uint8_t bDeviceProtocol;    /* Unused; 0x00 */
    uint8_t bMaxPacketSize0;    /* Max packet size for endpoint zero; 0x08 bytes */
    uint8_t idVendor[2];        /* Vendor ID; 0xXXXX */
    uint8_t idProduct[2];       /* Product ID; 0xXXXX */
    uint8_t bcdDevice[2];       /* Device release code in binary-coded decimal */
    uint8_t iManufacturer;      /* Index of string descriptor describing manufacturer; 0x01 */
    uint8_t iProduct;           /* Index of string descriptor describing product; 0x02 */
    uint8_t iSerialNumber;      /* Unused; 0x00 */
    uint8_t bNumConfigurations; /* Number of Configurations; typ. 0x01 */
} usbmidi_descriptor_device_t;

typedef struct _usbmidi_descriptor_configuration
{
    uint8_t bLength;             /* Size of this descriptor, in bytes; 0x09 */
    uint8_t bDescriptorType;     /* CONFIGURATION descriptor; 0x02 */
    uint8_t wTotalLength[2];     /* Length of total configuration block,
    							  *	including this descriptor, in bytes */
    uint8_t bNumInterfaces;      /* Number of interfaces in this configuration; typ. 0x02 */
    uint8_t bConfigurationValue; /* ID of this configuration; typ. 0x01 */
    uint8_t iConfiguration;      /* Unused; 0x00 */
    uint8_t bmAttributes;        /* Configuration characteristics */
    uint8_t bMaxPower;           /* Max. power consumption, 2 mA units; e.g. 0x32 == 100mA */
} usbmidi_descriptor_configuration_t;

typedef struct _usbmidi_descriptor_interface
{
    uint8_t bLength;			/* Size of this descriptor, in bytes; 0x09 */
    uint8_t bDescriptorType;	/* INTERFACE descriptor; 0x04 */
    uint8_t bInterfaceNumber;	/* Index of this interface; typ. 0x00 for control */
    uint8_t bAlternateSetting;	/* Index of this setting; typ. 0x00 for control */
    uint8_t bNumEndpoints;		/* 0 endpoints typ. for control; 0x00 or 0x0X */
    uint8_t bInterfaceClass;	/* AUDIO interface class; 0x01 */
    uint8_t bInterfaceSubClass; /* AUDIO_CONTROL or MIDISTREAMING interface subclass; 0x01 or 0x03 */
    uint8_t bInterfaceProtocol; /* Unused; 0x00 */
    uint8_t iInterface;			/* Unused; 0x00 */
} usbmidi_descriptor_interface_t;

typedef struct _usbmidi_descriptor_endpoint
{
    uint8_t bLength;			/* Size of this descriptor, in bytes; 0x09 */
    uint8_t bDescriptorType;	/* ENDPOINT descriptor; 0x05 */
    uint8_t bEndpointAddress;	/* OUT = 0x0X, IN = 0x8X */
    uint8_t bmAttributes;		/* Bulk, not shared; 0x02 */
    uint8_t wMaxPacketSize[2];	/* 64 bytes per packet; 0x0040 */
    uint8_t bInterval;			/* Ignored for bulk; 0x00 */
    uint8_t bRefresh;			/* Unused; 0x00 */
    uint8_t bSynchAddress;		/* Unused; 0x00 */
} usbmidi_descriptor_endpoint_t;


/*! @brief USB-MIDI 32-bit Event Packet structure */
typedef struct _usbmidi_event_packet
{
	uint8_t CCIN;				/* Byte 0 contains the cable number (first 4 bits) and code index number (last 4 bits) */
	uint8_t MIDI_0;				/* Byte 1 contains the first MIDI event byte */
	uint8_t MIDI_1;				/* Byte 2 contains the second MIDI event byte */
	uint8_t MIDI_2;				/* Byte 3 contains the final MIDI event byte */
} usbmidi_event_packet_t;


/*! @brief host app run status, adapted from the generic host CDC example */
typedef enum _usb_host_midi_run_state
{
    kUSBMIDIRunState_Idle = 0,                      /*!< idle */
	kUSBMIDIRunState_SetInterfaces,           		/*!< set control interfaces */
	kUSBMIDIRunState_WaitSetInterfaces,				/*!< set control interfaces done, proceed */
	kUSBMIDIRunState_SetPacketInfo,					/*!< set packet info */
	kUSBMIDIRunState_WaitSetPacketInfo,				/*!< set packet info done, proceed */
	kUSBMIDIRunState_SetProtocol,					/*!< set communication protocol */
	kUSBMIDIRunState_WaitSetProtocol,				/*!< set communication protocol done, proceed */
	kUSBMIDIRunState_Listening,						/*!< listen for keyboard commands */
	kUSBMIDIRunState_WaitListening,					/*!< ready for the next listen sequence */
	kUSBMIDIRunState_PrimeListening					/*!< something has broken us out of sequence,
													 *   reenter the listening routine */
} usb_host_midi_run_state_t;


/* cable numbers define a MIDI endpoint */
typedef enum _usbmidi_cable_number {
	kUSBMIDI_Cable_0 = 0x0,
	kUSBMIDI_Cable_1,
	kUSBMIDI_Cable_2,
	kUSBMIDI_Cable_3,
	kUSBMIDI_Cable_4,
	kUSBMIDI_Cable_5,
	kUSBMIDI_Cable_6,
	kUSBMIDI_Cable_7,
	kUSBMIDI_Cable_8,
	kUSBMIDI_Cable_9,
	kUSBMIDI_Cable_10,
	kUSBMIDI_Cable_11,
	kUSBMIDI_Cable_12,
	kUSBMIDI_Cable_13,
	kUSBMIDI_Cable_14,
	kUSBMIDI_Cable_15
} usbmidi_cable_number_t;

/* multiple channels per "cable" */
typedef enum _usbmidi_channel_number {
	kUSBMIDI_Channel_1 = 0x0,
	kUSBMIDI_Channel_2,
	kUSBMIDI_Channel_3,
	kUSBMIDI_Channel_4,
	kUSBMIDI_Channel_5,
	kUSBMIDI_Channel_6,
	kUSBMIDI_Channel_7,
	kUSBMIDI_Channel_8,
	kUSBMIDI_Channel_9,
	kUSBMIDI_Channel_10,
	kUSBMIDI_Channel_11,
	kUSBMIDI_Channel_12,
	kUSBMIDI_Channel_13,
	kUSBMIDI_Channel_14,
	kUSBMIDI_Channel_15,
	kUSBMIDI_Channel_16
} usbmidi_channel_number_t;

typedef enum _usbmidi_code_index_number {
	kUSBMIDI_CIN_Misc = 0x0,
	kUSBMIDI_CIN_Cable_Event,
	kUSBMIDI_CIN_Two_Byte_Common_Msg,
	kUSBMIDI_CIN_Three_Byte_Common_Msg,
	kUSBMIDI_CIN_SysEx_Start_Continue,
	kUSBMIDI_CIN_Single_Byte_Common_Msg,
	kUSBMIDI_CIN_SysEx_Ends_Two_Bytes,
	kUSBMIDI_CIN_SysEx_Ends_Three_Bytes,
	kUSBMIDI_CIN_Note_Off,
	kUSBMIDI_CIN_Note_On,
	kUSBMIDI_CIN_Poly_Keypress,
	kUSBMIDI_CIN_Control_Change,
	kUSBMIDI_CIN_Program_Change,
	kUSBMIDI_CIN_Channel_Pressure,
	kUSBMIDI_CIN_Pitchbend_Change,
	kUSBMIDI_CIN_System_Message
} usbmidi_code_index_number_t;

enum _usbmidi_channel_mode {
	kUSBMIDI_Ch_Mode_Omni_On_Polyphonic = 0x01,
	kUSBMIDI_Ch_Mode_Omni_On_Monophonic,
	kUSBMIDI_Ch_Mode_Omni_Off_Polyphonic,
	kUSBMIDI_Ch_Mode_Omni_Off_Monophonic
};

typedef enum _usbmidi_midi_ci_authority_level {
	kUSBMIDI_CI_Auth_Lvl_0x10		= 0x10U,
	kUSBMIDI_CI_Auth_Lvl_0x11,
	kUSBMIDI_CI_Auth_Lvl_0x12,
	kUSBMIDI_CI_Auth_Lvl_0x13,
	kUSBMIDI_CI_Auth_Lvl_0x14,
	kUSBMIDI_CI_Auth_Lvl_0x15,
	kUSBMIDI_CI_Auth_Lvl_0x16,
	kUSBMIDI_CI_Auth_Lvl_0x17,
	kUSBMIDI_CI_Auth_Lvl_0x18,
	kUSBMIDI_CI_Auth_Lvl_0x19,
	kUSBMIDI_CI_Auth_Lvl_0x1A,
	kUSBMIDI_CI_Auth_Lvl_0x1B,
	kUSBMIDI_CI_Auth_Lvl_0x1C,
	kUSBMIDI_CI_Auth_Lvl_0x1D,
	kUSBMIDI_CI_Auth_Lvl_0x1E,
	kUSBMIDI_CI_Auth_Lvl_0x1F,
	kUSBMIDI_CI_Auth_Lvl_0x20,
	kUSBMIDI_CI_Auth_Lvl_0x21,
	kUSBMIDI_CI_Auth_Lvl_0x22,
	kUSBMIDI_CI_Auth_Lvl_0x23,
	kUSBMIDI_CI_Auth_Lvl_0x24,
	kUSBMIDI_CI_Auth_Lvl_0x25,
	kUSBMIDI_CI_Auth_Lvl_0x26,
	kUSBMIDI_CI_Auth_Lvl_0x27,
	kUSBMIDI_CI_Auth_Lvl_0x28,
	kUSBMIDI_CI_Auth_Lvl_0x29,
	kUSBMIDI_CI_Auth_Lvl_0x2A,
	kUSBMIDI_CI_Auth_Lvl_0x2B,
	kUSBMIDI_CI_Auth_Lvl_0x2C,
	kUSBMIDI_CI_Auth_Lvl_0x2D,
	kUSBMIDI_CI_Auth_Lvl_0x2E,
	kUSBMIDI_CI_Auth_Lvl_0x2F,
	kUSBMIDI_CI_Auth_Lvl_0x30,
	kUSBMIDI_CI_Auth_Lvl_0x31,
	kUSBMIDI_CI_Auth_Lvl_0x32,
	kUSBMIDI_CI_Auth_Lvl_0x33,
	kUSBMIDI_CI_Auth_Lvl_0x34,
	kUSBMIDI_CI_Auth_Lvl_0x35,
	kUSBMIDI_CI_Auth_Lvl_0x36,
	kUSBMIDI_CI_Auth_Lvl_0x37,
	kUSBMIDI_CI_Auth_Lvl_0x38,
	kUSBMIDI_CI_Auth_Lvl_0x39,
	kUSBMIDI_CI_Auth_Lvl_0x3A,
	kUSBMIDI_CI_Auth_Lvl_0x3B,
	kUSBMIDI_CI_Auth_Lvl_0x3C,
	kUSBMIDI_CI_Auth_Lvl_0x3D,
	kUSBMIDI_CI_Auth_Lvl_0x3E,
	kUSBMIDI_CI_Auth_Lvl_0x3F,
	kUSBMIDI_CI_Auth_Lvl_0x40,
	kUSBMIDI_CI_Auth_Lvl_0x41,
	kUSBMIDI_CI_Auth_Lvl_0x42,
	kUSBMIDI_CI_Auth_Lvl_0x43,
	kUSBMIDI_CI_Auth_Lvl_0x44,
	kUSBMIDI_CI_Auth_Lvl_0x45,
	kUSBMIDI_CI_Auth_Lvl_0x46,
	kUSBMIDI_CI_Auth_Lvl_0x47,
	kUSBMIDI_CI_Auth_Lvl_0x48,
	kUSBMIDI_CI_Auth_Lvl_0x49,
	kUSBMIDI_CI_Auth_Lvl_0x4A,
	kUSBMIDI_CI_Auth_Lvl_0x4B,
	kUSBMIDI_CI_Auth_Lvl_0x4C,
	kUSBMIDI_CI_Auth_Lvl_0x4D,
	kUSBMIDI_CI_Auth_Lvl_0x4E,
	kUSBMIDI_CI_Auth_Lvl_0x4F,
	kUSBMIDI_CI_Auth_Lvl_0x50,
	kUSBMIDI_CI_Auth_Lvl_0x51,
	kUSBMIDI_CI_Auth_Lvl_0x52,
	kUSBMIDI_CI_Auth_Lvl_0x53,
	kUSBMIDI_CI_Auth_Lvl_0x54,
	kUSBMIDI_CI_Auth_Lvl_0x55,
	kUSBMIDI_CI_Auth_Lvl_0x56,
	kUSBMIDI_CI_Auth_Lvl_0x57,
	kUSBMIDI_CI_Auth_Lvl_0x58,
	kUSBMIDI_CI_Auth_Lvl_0x59,
	kUSBMIDI_CI_Auth_Lvl_0x5A,
	kUSBMIDI_CI_Auth_Lvl_0x5B,
	kUSBMIDI_CI_Auth_Lvl_0x5C,
	kUSBMIDI_CI_Auth_Lvl_0x5D,
	kUSBMIDI_CI_Auth_Lvl_0x5E,
	kUSBMIDI_CI_Auth_Lvl_0x5F,
	kUSBMIDI_CI_Auth_Lvl_0x60,
	kUSBMIDI_CI_Auth_Lvl_0x61,
	kUSBMIDI_CI_Auth_Lvl_0x62,
	kUSBMIDI_CI_Auth_Lvl_0x63,
	kUSBMIDI_CI_Auth_Lvl_0x64,
	kUSBMIDI_CI_Auth_Lvl_0x65,
	kUSBMIDI_CI_Auth_Lvl_0x66,
	kUSBMIDI_CI_Auth_Lvl_0x67,
	kUSBMIDI_CI_Auth_Lvl_0x68,
	kUSBMIDI_CI_Auth_Lvl_0x69,
	kUSBMIDI_CI_Auth_Lvl_0x6A,
	kUSBMIDI_CI_Auth_Lvl_0x6B,
	kUSBMIDI_CI_Auth_Lvl_0x6C,
	kUSBMIDI_CI_Auth_Lvl_0x6D,
	kUSBMIDI_CI_Auth_Lvl_0x6E,
	kUSBMIDI_CI_Auth_Lvl_0x6F
} usbmidi_midi_ci_authority_level_t;


/*! @brief USB host generic instance global variable */
extern usb_host_handle g_demoUSBHostHandle;
extern usb_host_cdc_instance_struct_t g_demoMidiInstance;
extern usbmidi_event_packet_t g_demoMidiEventPacket;
extern usb_host_pipe_init_t g_demoMidiEventPipeInit;
extern volatile _Bool g_demoMidiPacketRecvFlag;


usb_status_t USB_HostEvent(usb_device_handle deviceHandle,
                           usb_host_configuration_handle configurationHandle,
                           uint32_t eventCode);
void USB_HostApplicationInit(void);
void USB_HostClockInit(void);
void USB_HostIsrEnable(void);
void USB_HostTaskFn(void *param);

usb_status_t USB_HostMidiEvent(usb_device_handle deviceHandle,
                               usb_host_configuration_handle configurationHandle,
                               uint32_t eventCode);
void USB_HostMidiTask(void *param);


#endif /* USBMIDI_H_ */
