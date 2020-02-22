/*
 * usbmidi.c
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

#include "usbmidi.h"


void USB_HostClockInit(void) {
    usb_phy_config_struct_t phyConfig = {
        BOARD_USB_PHY_D_CAL,
        BOARD_USB_PHY_TXCAL45DP,
        BOARD_USB_PHY_TXCAL45DM,
    };

    CLOCK_EnableUsbhs0PhyPllClock(kCLOCK_Usbphy480M, 480000000U);
    CLOCK_EnableUsbhs0Clock(kCLOCK_Usb480M, 480000000U);
    USB_EhciPhyInit(CONTROLLER_ID, BOARD_XTAL0_CLK_HZ, &phyConfig);
}

void USB_HostIsrEnable(void) {
    uint8_t irqNumber;

    uint8_t usbHOSTEhciIrq[] = USBHS_IRQS;
    irqNumber                = usbHOSTEhciIrq[CONTROLLER_ID - kUSB_ControllerEhci0];
/* USB_HOST_CONFIG_EHCI */

/* Install isr, set priority, and enable IRQ. */
#if defined(__GIC_PRIO_BITS)
    GIC_SetPriority((IRQn_Type)irqNumber, USB_HOST_INTERRUPT_PRIORITY);
#else
    NVIC_SetPriority((IRQn_Type)irqNumber, USB_HOST_INTERRUPT_PRIORITY);
#endif
    EnableIRQ((IRQn_Type)irqNumber);
}

void USB_HostTaskFn(void *param) {
    USB_HostEhciTaskFunction(param);
}


/*!
 * @brief Audio setup and interface callback.
 *
 * This function is used as callback function for control set stages.
 *
 * @param param      the usb host instance pointer.
 * @param data       data buffer pointer.
 * @param dataLength data length.
 * @status         transfer result status.
 */
static void midiControlCallback(void *param, uint8_t *data, uint32_t dataLength, usb_status_t status)
{
	usb_host_cdc_instance_struct_t *callbackInstance = (usb_host_cdc_instance_struct_t *)param;

    if (kStatus_USB_TransferStall == status)
    {
        PRINTF("Transfer stalled, assumed unsupported!!\n");
    }
    else if (kStatus_USB_Success != status)
    {
        PRINTF("Control callback status NOT success. Transfer failed!!\n");
    }


    if (callbackInstance->runWaitState == kUSBMIDIRunState_WaitSetInterfaces)
    {
        callbackInstance->runState = kUSBMIDIRunState_SetPacketInfo;
    }
    else if (callbackInstance->runWaitState == kUSBMIDIRunState_WaitSetPacketInfo)
    {
        callbackInstance->runState = kUSBMIDIRunState_SetProtocol;
    }
    else if (callbackInstance->runWaitState == kUSBMIDIRunState_WaitSetProtocol)
    {
        callbackInstance->runState = kUSBMIDIRunState_Listening;
    }
    else
    {
    	PRINTF("%s: Unhandled runWaitState!!\n", __func__);
    }

}

/*!
 * @brief midi interrupt receive callback (adapted from host_hid_generic_bm example)
 *
 * This function is used as callback function for interrupt transfer. Interrupt transfer is used to implement
 * asynchronous MIDI requests and reads, allowing the rest of our program flow.
 * @param param    the host cdc instance pointer.
 * @param data     data buffer pointer.
 * @param dataLength data length.
 * @status         transfer result status.
 */
static void midiInterruptRecvCallback(void *param, uint8_t *data, uint32_t dataLength, usb_status_t status)
{
	usb_host_cdc_instance_struct_t *callbackInstance = (usb_host_cdc_instance_struct_t *)param;
	//usbmidi_event_packet_t *state = (usbmidi_event_packet_t *)data;

	if(status)
	{
	    if(status == kStatus_USB_TransferCancel)
	    {
	        PRINTF("!! ERROR: Data transfer cancelled !!\n");
	    }
	    else
	    {
	        //PRINTF("!! MIDI data transfer error !!\n"); //Comment out to avoid clicks on timeouts between presses/releases
	    }
	}
	else
	{
		g_demoMidiPacketRecvFlag = 1;
	    //PRINTF("\nCable Number and CIN = 0x%x\n", state->cableAndCIN);
	    //PRINTF("First MIDI Event Byte = 0x%x\n", state->MIDI_0);
	    //PRINTF("Second MIDI Event Byte = 0x%x\n", state->MIDI_1);
	    //PRINTF("Third MIDI Event Byte = 0x%x\n", state->MIDI_2);
	}

    if(callbackInstance->runWaitState == kUSBMIDIRunState_WaitListening)
    {
        if(status == kStatus_USB_Success)
        {
            callbackInstance->runState = kUSBMIDIRunState_Listening;
        }
        else
        {
            if(callbackInstance->deviceState == kStatus_DEV_Attached)
            {
                callbackInstance->runState = kUSBMIDIRunState_PrimeListening;
            }
        }
    }
}


/*
 * USB_HostMidiTask
 *
 * This state machine implements the MIDI control reads for setting notes.
 * It also manages state changes for the USB MIDI driver more generally.
 *
 * This was adapted from the host HID example provided for the RT1010-EVK.
 *
 */
void USB_HostMidiTask(void *param)
{
    usb_status_t status = kStatus_USB_Success;
    usb_host_cdc_instance_struct_t *midiInstance = (usb_host_cdc_instance_struct_t *)param;

    /* device state changes */
    if(midiInstance->deviceState != midiInstance->prevState)
    {
        midiInstance->prevState = midiInstance->deviceState;
        switch(midiInstance->deviceState)
        {
            case kStatus_DEV_Idle:
                break;
            case kStatus_DEV_Attached:
                midiInstance->runState = kUSBMIDIRunState_SetInterfaces;
                status = USB_HostCdcInit(midiInstance->deviceHandle, &midiInstance->classHandle);
                PRINTF("Audio device attached...status code (0x%x)\n", status);
                break;
            case kStatus_DEV_Detached:
                midiInstance->deviceState = kStatus_DEV_Idle;
                midiInstance->runState = kUSBMIDIRunState_Idle;
                status = USB_HostCdcDeinit(midiInstance->deviceHandle, midiInstance->classHandle);
                midiInstance->controlInterfaceHandle = NULL;
                midiInstance->dataInterfaceHandle = NULL;
                midiInstance->classHandle = NULL;
                midiInstance->deviceHandle = NULL;
                PRINTF("Audio device detached...status code (0x%x)\n\n", status);
                break;
            default:
                break;
        }
    }

    /* midi application run state */
    switch(midiInstance->runState)
    {
        case kUSBMIDIRunState_Idle:
            break;
        case kUSBMIDIRunState_SetInterfaces:
            midiInstance->runWaitState = kUSBMIDIRunState_WaitSetInterfaces;
            midiInstance->runState = kUSBMIDIRunState_Idle;


            PRINTF("Setting interfaces...\n");
            if(USB_HostCdcSetControlInterface(midiInstance->classHandle, midiInstance->controlInterfaceHandle, 0,
                    midiControlCallback, midiInstance))
            {
                PRINTF("\n!! Error setting control interface !!\n");
            }
            if(USB_HostCdcSetDataInterface(midiInstance->classHandle, midiInstance->dataInterfaceHandle, 0,
            		midiControlCallback, midiInstance))
            {
                PRINTF("\n!! Error setting data interface !!\n");
            }
            break;
        case kUSBMIDIRunState_SetPacketInfo:
        	midiInstance->runWaitState = kUSBMIDIRunState_WaitListening;
        	midiInstance->runState = kUSBMIDIRunState_Listening;

        	PRINTF("Setting up packet info...\n");
        	midiInstance->bulkInPacketSize =
        			USB_HostCdcGetPacketsize(midiInstance->classHandle, USB_ENDPOINT_BULK, USB_IN);
        	midiInstance->bulkOutPacketSize =
        			USB_HostCdcGetPacketsize(midiInstance->classHandle, USB_ENDPOINT_BULK, USB_OUT);
        	break;
        case kUSBMIDIRunState_SetProtocol:
        	midiInstance->runWaitState = kUSBMIDIRunState_WaitListening;
        	midiInstance->runState = kUSBMIDIRunState_Idle;
        	/*
        	 * This state is reserved for future use with deeper MIDI functionality.
        	 */
        	break;
        case kUSBMIDIRunState_Listening:
            midiInstance->runWaitState = kUSBMIDIRunState_WaitListening;
            midiInstance->runState = kUSBMIDIRunState_Idle;

            status = USB_HostCdcDataRecv(midiInstance->classHandle, (uint8_t *)&g_demoMidiEventPacket,
            					sizeof(g_demoMidiEventPacket), midiInterruptRecvCallback,
								midiInstance);
            if(status) PRINTF("Error in data receive, status code (0x%x)\n", status);
            break;
        case kUSBMIDIRunState_PrimeListening:
        	midiInstance->runWaitState = kUSBMIDIRunState_WaitListening;
        	midiInstance->runState = kUSBMIDIRunState_Listening; // Go right on back in
        	break;
        default:
            break;
    }
}


usb_status_t USB_HostMidiEvent(usb_device_handle deviceHandle,
                               usb_host_configuration_handle configurationHandle,
                               uint32_t eventCode) {

    usb_host_configuration_t *configuration;
    usb_host_interface_t *interface;
    uint32_t infoValue;

    usb_status_t status = kStatus_USB_Success;

    uint8_t id;

    switch (eventCode)
    {
        case kUSB_HostEventAttach:

            configuration = (usb_host_configuration_t *)configurationHandle;

            for(int8_t interfaceIndex = 0; interfaceIndex < configuration->interfaceCount; ++interfaceIndex)
            {
                interface = &(configuration->interfaceList[interfaceIndex]);


                id = interface->interfaceDesc->bInterfaceClass;
                PRINTF("Interface class is 0x%x", id);

                if(id != USB_AUDIO_CLASS_CODE) continue;
                else PRINTF("...Audio Class device detected.\n");

                id = interface->interfaceDesc->bInterfaceSubClass;
                PRINTF("Interface subclass is 0x%x", id);
                if(id == USB_AUDIO_SUBCLASS_CONTROL) PRINTF("...AUDIOCONTROL Subclass detected.\n");
                else if (id == USB_AUDIO_SUBCLASS_MIDISTREAMING) PRINTF("...MIDISTREAMING Subclass detected.\n");
                id = interface->interfaceDesc->bInterfaceProtocol;
                PRINTF("...Interface Protocol is version 0x%x\n", id);

                if((interface->interfaceDesc->bInterfaceClass == USB_AUDIO_CLASS_CODE) &&
                    (interface->interfaceDesc->bInterfaceSubClass == USB_AUDIO_SUBCLASS_CONTROL))
                {
                    PRINTF("Interface (0x%x) is an Audio Control interface.\n\n", interface->interfaceIndex);
                    g_demoMidiInstance.controlInterfaceHandle = interface;
                }
                else if((interface->interfaceDesc->bInterfaceClass == USB_AUDIO_CLASS_CODE) &&
                		 (interface->interfaceDesc->bInterfaceSubClass == USB_AUDIO_SUBCLASS_MIDISTREAMING))
                {
                	PRINTF("Interface (0x%x) is a MIDI Streaming interface.\n\n", interface->interfaceIndex);
                	g_demoMidiInstance.dataInterfaceHandle = interface;
                }
                else {
                	PRINTF("!! Attached USB device is not supported !!\n\n");
                	return kStatus_USB_NotSupported;
                }

            }

            g_demoMidiInstance.deviceHandle = deviceHandle;
            g_demoMidiInstance.configHandle = configurationHandle;

            if((NULL != g_demoMidiInstance.dataInterfaceHandle) &&
               (NULL != g_demoMidiInstance.controlInterfaceHandle) &&
			   (NULL != g_demoMidiInstance.deviceHandle))
            {
                status = kStatus_USB_Success;
            }
            else status = kStatus_USB_NotSupported;

            return status;
            break;

        case kUSB_HostEventNotSupported:
            break;

        case kUSB_HostEventEnumerationDone:

        	if (g_demoMidiInstance.configHandle == configurationHandle) {

        	    if ((g_demoMidiInstance.deviceHandle != NULL) &&
        	        (g_demoMidiInstance.dataInterfaceHandle != NULL) &&
					(g_demoMidiInstance.controlInterfaceHandle != NULL)) {

        	        if (g_demoMidiInstance.deviceState == kStatus_DEV_Idle) {

        	        	g_demoMidiInstance.deviceState = kStatus_DEV_Attached; // This is critical to proceed with MidiTask & later to detach properly!

        	        	USB_HostHelperGetPeripheralInformation(deviceHandle, kUSB_HostGetDevicePID, &infoValue);
        	        	PRINTF("Enumeration complete: pid=0x%x ", infoValue);
        	        	USB_HostHelperGetPeripheralInformation(deviceHandle, kUSB_HostGetDeviceVID, &infoValue);
        	        	PRINTF("vid=0x%x ", infoValue);
        	        	USB_HostHelperGetPeripheralInformation(deviceHandle, kUSB_HostGetDeviceAddress, &infoValue);
        	        	PRINTF("address=%d\r\n", infoValue);

        	        	g_demoMidiEventPipeInit.endpointAddress = infoValue;

        	        }
        	        else {
        	            PRINTF("The device instance is not idle...\n");
        	            status = kStatus_USB_Error;
        	        }
        	    }
        	}
            break;

        case kUSB_HostEventDetach:
        	if(g_demoMidiInstance.configHandle == configurationHandle) {

        		g_demoMidiInstance.configHandle = NULL;
        		if(g_demoMidiInstance.deviceState != kStatus_DEV_Idle) g_demoMidiInstance.deviceState = kStatus_DEV_Detached;

        	}
            break;

        default:
            break;
    }
    return status;
}
