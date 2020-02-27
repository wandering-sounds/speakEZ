/*
 * Copyright 2016-2020 NXP
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * o Redistributions of source code must retain the above copyright notice, this list
 *   of conditions and the following disclaimer.
 *
 * o Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 *
 * o Neither the name of NXP Semiconductor, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file    speakEZ.c
 * @brief   Application entry point
 */
#include <stdio.h>
#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "fsl_debug_console.h"



/* * * * * * * * * * * * * * * * * * * * * * *
 * ADDITIONAL INCLUDES:
 * * * * * * * * * * * * * * * * * * * * * * */
#include "fsl_wm8960.h"
#include "speakEZ.h"
#include "arm_math.h"
#include <math.h>



/* * * * * * * * * * * * * * * * * * * * * * *
 * GLOBAL VARIABLE DEFINITIONS:
 * * * * * * * * * * * * * * * * * * * * * * */

/*! @brief SAI IRQ done flag to trigger audio updates */
volatile _Bool SAI_RequestSynthUpdate = 0;

/*! @brief SW4 debouncing global variables for PIT interrupt */
volatile uint32_t g_sw4Debounce = 0;
volatile _Bool g_sw4Pressed = 0;

/*! @brief USB MIDI global variables */
usb_host_handle g_demoUSBHostHandle;
usb_host_cdc_instance_struct_t g_demoMidiInstance;
usbmidi_event_packet_t g_demoMidiEventPacket;
usb_host_pipe_init_t g_demoMidiEventPipeInit;
volatile _Bool g_demoMidiPacketRecvFlag = 0;



/* * * * * * * * * * * * * * * * * * * * * * *
 * FUNCTION DEFINITIONS:
 * * * * * * * * * * * * * * * * * * * * * * */

/*
 * writeToWM8960
 *
 * Writes a control word to the specified register in the WM8960.
 * Attempts the requested transfer 3 times, after which it sends an
 * error message over serial with a reason code.
 * THIS FUNCTION REQUIRES THAT YOUR I2C_PERIPHERAL INSTANCE HAS BEEN
 * PROPERLY INITIALIZED BY MCUXpresso. (for MIMXRT1010-EVK, LPI2C1)
 * In the case of the dev board above, make sure BOTH I2C pins:
 *  - Are OPEN-DRAIN
 *  - Have Software Input On ENABLED
 *  - Have 22kOhm pull-ups ENABLED
 *  - Have Pull/Keeper as Pull
 * This function can be ignored and taken out of your codebase if
 * you are initializing your own CODEC elsewhere.
 *
 * Below is the intended I2C bitstream per the datasheet:
 * |ST|SA6 SA5 SA4 SA3 SA2 SA1 SA0 Rd/Wr|Ack|A6 A5 A4 A3 A2 A1 A0 D8|Ack|D7 D6 D5 D4 D3 D2 D1 D0|Ack|SP|
 *
 * Return values:
 * kStatus_Success Data was received successfully.
 * kStatus_LPI2C_Busy Another master is currently utilizing the bus.
 * kStatus_LPI2C_Nak The slave device sent a NAK in response to a byte.
 * kStatus_LPI2C_FifoError FIFO underrun or overrun.
 * kStatus_LPI2C_ArbitrationLost Arbitration lost error.
 * kStatus_LPI2C_PinLowTimeout SCL or SDA were held low too long.
 */
status_t writeToWM8960(uint8_t controlReg, uint16_t controlWord) {

	I2C_masterBuffer[0] = (controlReg << 1) | ((controlWord >> 8) & 0b1); // {A6..0, D8}
	I2C_masterBuffer[1] = 0b11111111 & controlWord; // D7..0
	uint8_t slaveAddr = 0b0011010; // per the WM8960 datasheet

	lpi2c_master_transfer_t transferConfig;
	transferConfig.flags = kLPI2C_TransferDefaultFlag;
	transferConfig.slaveAddress = slaveAddr;
	transferConfig.direction = kLPI2C_Write;
	transferConfig.subaddress = 0;
	transferConfig.subaddressSize = 0;
	transferConfig.data = I2C_masterBuffer;
	transferConfig.dataSize = 2;

	status_t currentStatus = kStatus_Success;
	for(uint8_t ii = 0; ii < 3; ii++) {

		currentStatus = LPI2C_MasterTransferBlocking(I2C_PERIPHERAL, &transferConfig);

		if(currentStatus != kStatus_Success) PRINTF("ERROR!!! WM8960 write failed! Retrying...\n");
		else break;
	}

	if(currentStatus != kStatus_Success) {
		PRINTF("\nERROR!!! writeToWM8960 failed on reg (0x%x)\n", controlReg);
		PRINTF("Reason:  ");
		switch(currentStatus) {
		case kStatus_LPI2C_Busy:
			PRINTF("I2C Bus Busy.\n\n");
			break;
		case kStatus_LPI2C_Nak:
			PRINTF("I2C Slave Nak.\n\n");
			break;
		case kStatus_LPI2C_FifoError:
			PRINTF("I2C Fifo Error.\n\n");
			break;
		case kStatus_LPI2C_ArbitrationLost:
			PRINTF("I2C Master Arbitration Loss.\n\n");
			break;
		case kStatus_LPI2C_PinLowTimeout:
			PRINTF("I2C Pin Held Low Too Long.\n\n");
			break;
		default:
			PRINTF("Reason Unknown.\n\n");
			break;
		}
	}

	return currentStatus;
}
/*
 * configureWM8960
 *
 * Transfers data to the appropriate WM8960 registers so the
 * CODEC has the correct settings and features enabled.
 * This function can be ignored and taken out of your codebase
 * if you are initializing your own CODEC elsewhere.
 *
 * Every effort is made to make the purpose of each transfer
 * clear.
 *
 * Please read the WM8960 datasheet for details on control bits.
 * There are lots of things you can do that I do not!
 */
void configureWM8960() {

	/*
	 * Power gating 1 (0x19):
	 *
	 * Enable 50kOhm Vmid divider (VMIDSEL = 01) for playback/record
	 * Enable VREF (VREF = 1)
	 * Enable Analogue Input PGA and Boost Right (AINR = 1) for on-board mic
	 * Enable ADC Right (ADCR = 1)
	 * Enable MICBIAS (MICB = 1)
	 *
	 * | VMIDSEL[1:0] | VREF | AINL | AINR | ADCL | ADCR | MICB | DIGEN |
	 */
	writeToWM8960(WM8960_POWER1, 0b011010110);

	/*
	 * Power gating 2 (0x1A):
	 *
	 * Enable left DAC (DACL = 1)
	 * Enable right DAC (DACR = 1)
	 * Enable LOUT1 buffer (LOUT1 = 1) for headphone left
	 * Enable ROUT1 buffer (ROUT1 = 1) for headphone right
	 *
	 * | DACL | DACR | LOUT1 | ROUT1 | SPKL | SPKR | reserved | OUT3 | PLL_EN |
	 */
	writeToWM8960(WM8960_POWER2, 0b111100000);

	/*
	 * Power gating 3 (0x2F):
	 *
	 * Enable right input PGA (RMIC = 1) for on-board mic
	 * Enable left output mixer (LOMIX = 1)
	 * Enable right output mixer (ROMIX = 1)
	 *
	 * | reserved | reserved | reserved | LMIC | RMIC | LOMIX | ROMIX | reserved | reserved |
	 */
	writeToWM8960(WM8960_POWER3, 0b000011100);

	/*
	 * Right input volume (0x01):
	 *
	 * Update the volume with this change (IPVU = 1)
	 * Disable right mute (RINMUTE = 0), we will update with IPVU later
	 * To avoid clicks/distortion, change gain only on zero crossings (LIZC = 1)
	 * Leave right volume at default (RINVOL[5:0] = 0b010111)
	 *
	 * | IPVU | RINMUTE | RIZC | RINVOL[5:0] |
	 */
	writeToWM8960(WM8960_RINVOL, 0b101010111);

	/*
	 * LOUT1 volume (0x02):
	 *
	 * Change gain only on zero crossings (LO1ZC = 1)
	 * Set left output volume at +0dB (LOUT1VOL[6:0] = 0b1111001)
	 *
	 * | OUT1VU | LO1ZC | LOUT1VOL[6:0] |
	 */
	writeToWM8960(WM8960_LOUT1, 0b011111001);

	/*
	 * ROUT1 volume (0x03):
	 *
	 * Update the volume with this change (OUT1VU = 1)
	 * Change gain only on zero crossings (RO1ZC = 1)
	 * Set right output volume at +0dB (ROUT1VOL[6:0] = 0b1111001)
	 *
	 * | OUT1VU | RO1ZC | ROUT1VOL[6:0] |
	 */
	writeToWM8960(WM8960_ROUT1, 0b111111001);

	/*
	 * ADC and DAC control 1 (0x05):
	 *
	 * Remove DAC digital mute (DACMU = 0)
	 *
	 * | reserved | DACDIV2 | ADCPOL[1:0] | reserved | DACMU | DEEMPH[1:0] | ADCHPD |
	 */
	writeToWM8960(WM8960_DACCTL1, 0b000000000);

	/*
	 * ADC and DAC control 2 (0x06):
	 *
	 * Use a ~10ms ramp when digital DAC mute is toggled (DACSMM = 1)
	 * The sloping DAC filter stopband has slightly more aggressive behavior with minimal drawbacks (DACSLOPE = 1)
	 *
	 * | reserved | reserved | DACPOL[1:0] | reserved | DACSMM | DACMR | DACSLOPE | reserved |
	 */
	writeToWM8960(WM8960_DACCTL2, 0b000001010);

	/*
	 * Audio interface 1 (0x07):
	 *
	 * Audio data word length = 24 bits (WL[1:0] = 0b10) (despite SAI setting -- it works OK with proper shifting)
	 * Format is I2S (FORMAT[1:0] = 0b10)
	 *
	 * | ALRSWAP | BCLKINV | MS | DLRSWAP | LRP | WL[1:0] | FORMAT[1:0] |
	 */
	writeToWM8960(WM8960_IFACE1, 0b000001010);

	/*
	 * Audio interface 2 (0x09):
	 *
	 * Use a GPIO function on the ADCLRC/GPIO1 pin (ALRCGPIO = 1)
	 *
	 * | reserved | reserved | ALRCGPIO | WL8 | DACCOMP[1:0] | ADCCOMP[1:0] | LOOPBACK |
	 */
	writeToWM8960(WM8960_IFACE2, 0b001000000);

	/*
	 * Additional control (0x17):
	 *
	 * We want both ADC data outputs to refer to the right side (on-board mic) for convenience (DATSEL[1:0] = 0b10)
	 *
	 * | TSDEN | VSEL[1:0] | reserved | DMONOMIX | DATSEL[1:0] | TOCLKSEL | TOEN |
	 */
	writeToWM8960(WM8960_ADDCTL1, 0b111001000);

	/*
	 * Anti-pop 1 (0x1C):
	 *
	 * Enable the VMID soft start (SOFT_ST = 1)
	 *
	 * | reserved | POBCTRL | reserved | reserved | BUFDCOPEN | BUFIOEN | SOFT_ST | reserved | HPSTBY |
	 */
	writeToWM8960(WM8960_APOP1, 0b000000100);

	/*
	 * ADCL signal path (0x20):
	 *
	 * LMP3 and LMN1 are used for the headphone mic. Unused for this demo application.
	 *
	 * | LMN1 | LMP3 | LMP2 | LMICBOOST[1:0] | LMIC2B | reserved | reserved | reserved |
	 */
	writeToWM8960(WM8960_LINPATH, 0b000000000);

	/*
	 * ADCR signal path (0x21):
	 *
	 * RMP2 and RMN1 are used for the on-board mic. (RMN1 = 1, RMP2 = 1)
	 * We also need to connect the right PGA to the boost mixer (RMIC2B = 1)
	 * We need to hear the on-board microphone loud, which happens around +20 dB (RMICBOOST = 10)
	 *
	 * | RMN1 | RMP3 | RMP2 | RMICBOOST[1:0] | RMIC2B | reserved | reserved | reserved |
	 */
	writeToWM8960(WM8960_RINPATH, 0b101101000);

	/*
	 * Left out mix (0x22):
	 *
	 * Left DAC only to the left output mixer. (LD2LO = 1)
	 *
	 * | LD2LO | LI2LO | LI2LOVOL[2:0] | reserved | reserved | reserved | reserved |
	 */
	writeToWM8960(WM8960_LOUTMIX, 0b100000000);

	/*
	 * Right out mix (0x25):
	 *
	 * Right DAC only to the right output mixer. (RD2RO = 1)
	 *
	 * | RD2RO | RI2RO | RI2ROVOL[2:0] | reserved | reserved | reserved | reserved|
	 */
	writeToWM8960(WM8960_ROUTMIX, 0b100000000);

	/*
	 * Additional control 4 (0x30):
	 *
	 * GPIO1 should be the debounced jack detect signal (GPIOSEL[2:0] = 0b011)
	 * Headphone jack detect is on RIN3/JD3 (HPSEL[1:0] = 0b11)
	 * Temperature sensor enable (TSENSEN = 1)
	 *
	 * | reserved | GPIOPOL | GPIOSEL[2:0] | HPSEL[1:0] | TSENSEN | MBSEL |
	 */
	writeToWM8960(WM8960_ADDCTL4, 0b000111110);

	/*
	 * I found the microphone pickup quality was improved through the use
	 * of the auto level control. I make some settings changes here to get it
	 * turned on and functioning.
	 */

	/*
	 * Noise Gate (0x14):
	 *
	 * Noise gate threshold of -40.5dBfs (NGTH[4:0] = 0b11000)
	 * Enable the noise gate (prevent static) (NGAT = 1)
	 *
	 * | reserved | NGTH[4:0] | reserved | reserved | NGAT |
	 */
	writeToWM8960(WM8960_NOISEG, 0b011000001);


	/*
	 * Automatic Level Control 1 (0x11):
	 *
	 * Turn the ALC on for the right channel (ALCSEL[1:0] = 0b01)
	 * Set the maximum gain for the PGA to +6dB (MAXGAIN[2:0] = 0b011)
	 * Set the ALC target level to -6.0dB (ALCL[3:0] = 0b1011)
	 *
	 * | ALCSEL[1:0] | MAXGAIN[2:0] | ALCL[3:0] |
	 */
	writeToWM8960(WM8960_ALC1, 0b010111011);

	/*
	 * Automatic Level Control 2 (0x12):
	 *
	 * Set the minimum gain of the PGA to -17.25dB (MINGAIN[2:0] = 0b000)
	 * Set the hold time before gain increases to 5.33ms (HLD[3:0] = 0b0010)
	 *
	 * | reserved | reserved | MINGAIN[2:0] | HLD[3:0] |
	 */
	writeToWM8960(WM8960_ALC2, 0b100000010);

	/*
	 * Automatic Level Control 3 (0x13):
	 *
	 * Set the ALC decay (ramp-up time) to 192ms (DCY[3:0] = 0b0011)
	 * Set the ALC attack (ramp-down time) to 24ms (ATK[3:0] = 0b0010)
	 *
	 * | ALCMODE | DCY[3:0] | ATK[3:0] |
	 */
	writeToWM8960(WM8960_ALC3, 0b000110010);

}


/*
 * setTxAudio
 *
 * Thread-safe audio Tx copy after program calculations.
 *
 * The input argument should be "outputAudioBuffer".
 */
void setTxAudio(int32_t *audioBuffer) {
	NVIC_DisableIRQ(SAI1_IRQn);

	for(int ii = 0; ii < kAudio_Buffer_Words; ii++) {
		SAI1_txAudio[ii] = audioBuffer[ii] * 256; // sign-agnostic left-shift
	}

	NVIC_EnableIRQ(SAI1_IRQn);
}
/*
 * getRxAudio
 *
 * Thread-safe audio Rx copy for further manipulation.
 *
 * The input argument should be "inputAudioBuffer".
 */
void getRxAudio(int32_t *audioBuffer) {
	NVIC_DisableIRQ(SAI1_IRQn);

	for(int ii = 0; ii < kAudio_Buffer_Words; ii++) {
		audioBuffer[ii] = SAI1_rxAudio[ii] / 256; // sign-agnostic right-shift
	}

	NVIC_EnableIRQ(SAI1_IRQn);
}


/*
 * setWavetableSine
 *
 * Initializes the values of inputWavetable as a 24-bit sine
 * wave of length tableLen
 */
void setWavetableSine(int32_t *inputWavetable, uint32_t tableLen) {
	for(uint32_t i = 0; i < tableLen; i++) {
		inputWavetable[i] = (int32_t)(arm_sin_f32((float)(2*PI * i / tableLen)) * kSynth_Max_Audio_Level);
	}
}
/*
 * setWavetableTri
 *
 * Initializes the values of inputWavetable as a 24-bit triangle
 * wave of length tableLen
 */
void setWavetableTri(int32_t *inputWavetable, uint32_t tableLen) {
	for(uint32_t i = 0; i < tableLen; i++) {

		if((2*i) < tableLen) {
			inputWavetable[i] = (int32_t)( (4.0 * i / tableLen) * kSynth_Max_Audio_Level + kSynth_Min_Audio_Level );
		}
		else {
			inputWavetable[i] = inputWavetable[tableLen - i - 1];
		}

	}
}
/*
 * setWavetableSaw
 *
 * Initializes the values of inputWavetable as a 24-bit saw
 * wave of length tableLen
 */
void setWavetableSaw(int32_t *inputWavetable, uint32_t tableLen) {
	for(uint32_t i = 0; i < tableLen; i++) {
		inputWavetable[i] = (int32_t)( (2.0 * i / tableLen ) * kSynth_Max_Audio_Level + kSynth_Min_Audio_Level);
	}
}
/*
 * setWavetableNovel
 *
 * Initializes the values of inputWavetable as a 24-bit unique
 * waveform of length tableLen
 */
void setWavetableNovel(int32_t *inputWavetable, uint32_t tableLen) {
	for(uint32_t i = 0; i < tableLen; i++) {

		if((6*i) < tableLen) {
			inputWavetable[i] = (int32_t)kSynth_Max_Audio_Level;
		}
		else if((2*i) < tableLen) {
			inputWavetable[i] = (int32_t)kSynth_Min_Audio_Level;
		}
		else {
			inputWavetable[i] = (int32_t)( (4.0 * ( i / tableLen - 0.5)) * kSynth_Max_Audio_Level + kSynth_Min_Audio_Level );
		}

	}
}


/*
 * initSynth
 *
 * Sets the values of freq and phaseIncrement for a wavetable synth,
 * and initializes the keyActive status and phase of all keys to 0.
 * Sets the channel number and initial pitchbends.
 *
 * Playing a note without initializing the synth generates undefined behavior.
 */
void initSynth(wavetableSynth *synth, uint32_t numKeys, uint32_t indexA3, float freqA3, usbmidi_channel_number_t chNum) {

	assert(indexA3 <= numKeys);

	int i;

	for(i = 0; i < indexA3; ++i) {
		synth->freq[i] = freqA3 / powf(TWELFTH_ROOT_OF_TWO, (float)(indexA3 - i));
		synth->phaseIncrement[i] = kSynth_Table_Length * synth->freq[i] / kAudio_Frame_Hz;
		synth->phase[i] = 0;
		synth->velocity[i] = 0;
	}
	for(; i < numKeys; ++i) {
		synth->freq[i] = freqA3 * powf(TWELFTH_ROOT_OF_TWO, (float)(i - indexA3));
		synth->phaseIncrement[i] = kSynth_Table_Length * synth->freq[i] / kAudio_Frame_Hz;
		synth->phase[i] = 0;
		synth->velocity[i] = 0;
	}

	synth->midiChannel = chNum;
	synth->pbendFactor = 1.0;

}
/*
 * playSynth
 *
 * Outputs a wavetable audio sample using the active notes and their phases
 * for the specified synth. Increments the note phases.
 *
 * Returns a signed value fenced within 24 significant bits.
 */
int32_t playSynth(wavetableSynth *synth) {

	int32_t audioOut = 0;
	uint32_t startIndex = 0;
	float interpDist = 0;
	float interpBegin = 0;
	float interpEnd = 0;

	for(int i = 0; i < kSynth_Num_Keys; i++) {

		if(synth->velocity[i] == 0) continue;

		/*
		 * We must perform a linear interpolation to extract an approximate
		 * waveform amplitude for fractional indices
		 */
		startIndex = (uint32_t)synth->phase[i];
		interpDist = synth->phase[i] - startIndex;
		interpBegin = (float)synth->wavetable[startIndex] * synth->velocity[i] / kSynth_Max_Velocity;
		interpEnd = (float)synth->wavetable[(startIndex + 1) % kSynth_Table_Length] * synth->velocity[i] / kSynth_Max_Velocity;

		audioOut += (int32_t)( interpBegin + interpDist * (interpEnd - interpBegin) );

		if(audioOut > kSynth_Max_Audio_Level) audioOut = kSynth_Max_Audio_Level;
		if(audioOut < kSynth_Min_Audio_Level) audioOut = kSynth_Min_Audio_Level;

		synth->phase[i] += synth->phaseIncrement[i] * synth->pbendFactor;
		if(synth->phase[i] >= kSynth_Table_Length) {
			synth->phase[i] = synth->phase[i] - kSynth_Table_Length;
		}
	}

	return audioOut;
}
/*
 * pressKey
 *
 * Sets the specified key index to the desired velocity for the wavetableSynth
 */
void pressKey(wavetableSynth *synth, uint32_t keyIndex, uint32_t keyVelocity) {

	if(keyVelocity > kSynth_Max_Velocity) keyVelocity = kSynth_Max_Velocity;

	if(keyIndex < kSynth_Num_Keys) {
		synth->velocity[keyIndex] = keyVelocity;
	}

}
/*
 * releaseKey
 *
 * Resets the specified key index for the wavetableSynth so it is no longer active.
 * Keys have memory, so they retain the phase they left off on.
 */
void releaseKey(wavetableSynth *synth, uint32_t keyIndex) {

	if(keyIndex < kSynth_Num_Keys) {
		synth->velocity[keyIndex] = 0;
	}

}
/*
 * updatePitchbend
 *
 * Updates the pbendFactor for the specified synth
 */
void updatePitchbend(wavetableSynth *synth, uint32_t pbLSB, uint32_t pbMSB) {

	uint32_t pbVal = (pbMSB << 7) | (pbLSB);

	float scaledPbVal = ((float)pbVal / 8192.0f - 1.0f);

	synth->pbendFactor = powf(2.0f, scaledPbVal * (float)kSynth_Pbend_Semitones / 12.0f);

}

/*
 * toggleActiveWavetable
 *
 * Demo function to change the carrier wavetable from waveform to waveform.
 * Alters the sound of the output audio.
 */
void toggleActiveWavetable(wavetableSynth *synth) {

	if(++g_activeWavetable >= NUM_WAVETABLES) g_activeWavetable = 0;

	switch(g_activeWavetable) {

	case kSynth_Wavetable_Sine:
		synth->wavetable = &wavetableSine[0];
		break;

	case kSynth_Wavetable_Tri:
		synth->wavetable = &wavetableTri[0];
		break;

	case kSynth_Wavetable_Saw:
		synth->wavetable = &wavetableSaw[0];
		break;

	case kSynth_Wavetable_Novel:
		synth->wavetable = &wavetableNovel[0];
		break;

	default:
		break;

	}
}
/*
 * playDemoChord
 *
 * Presses all the constituent keys in a demo chord.
 */
void playDemoChord(wavetableSynth *synth, uint32_t chordNum) {
	for(int i = 0; i < NUM_DEMO_NOTES; i++) {

		pressKey(synth, demoChords[chordNum][i], 20);

	}
}
/*
 * toggleDemoChord
 *
 * Releases the old keys, indexes the demo chord number, and presses the new keys.
 */
void toggleDemoChord(wavetableSynth *synth) {

	for(int i = 0; i < NUM_DEMO_NOTES; i++) {
		releaseKey(synth, demoChords[g_activeDemoChord][i]);
	}

	if(++g_activeDemoChord >= NUM_DEMO_CHORDS) g_activeDemoChord = 0;

	playDemoChord(synth, g_activeDemoChord);

}


/*
 * getSAI_RequestSynthUpdate
 *
 * Safely reads SAI_RequestSynthUpdate
 *
 * Returns the read value
 */
_Bool getSAI_RequestSynthUpdate() {

	NVIC_DisableIRQ(SAI1_IRQn);
	_Bool temp = SAI_RequestSynthUpdate;
	NVIC_EnableIRQ(SAI1_IRQn);

	return temp;
}
/*
 * clearSAI_RequestSynthUpdate
 *
 * Safely resets SAI_RequestSynthUpdate to 0
 */
void clearSAI_RequestSynthUpdate() {

	NVIC_DisableIRQ(SAI1_IRQn);
	SAI_RequestSynthUpdate = 0;
	NVIC_EnableIRQ(SAI1_IRQn);

}

/*
 * The calculation method used here was
 * learned from "Cookbook formulae for audio EQ
 * biquad filter coefficients", linked in the submission
 * and the Github page. As of Feb 19, 2020, this can be
 * found at
 * "web.archive.org/web/20160301104613/http://www.musicdsp.org/files/Audio-EQ-Cookbook.txt".
 * (by the real og, Robert Bristow-Johnson)
 */
/*
 * calculateBiquadCoeff
 *
 * Calculate 2nd Order IIR biquad filter coefficients
 *
 * @param coeffs -- target coefficient array address,
 * 					expects five coefficients: {b0/a0, b1/a0, b2/a0, a1/a0, a2/a0}
 * @param fC -- the center/cutoff frequency in Hz
 * @param fS -- the sampling frequency in Hz
 * @param filterType -- select from filter_type_t
 * @param Q_OR_BW -- for bandpass filters, the bandwidth in octaves; otherwise, the Q value
 */
void calculateBiquadCoeffs(float *coeffs, float fC, float fS, filter_type_t filterType, float Q_OR_BW) {
	float omega 	= 0;
	float invHlfQ	= 1;	//  1/(2*Q)
	float alpha 	= 0;
	float sinOmega 	= 0;
	float cosOmega 	= 0;
	float a0 		= 1;
	float a1 		= 0;
	float a2 		= 0;
	float b0 		= 0;
	float b1		= 0;
	float b2 		= 0;

	omega = 2.0 * PI * fC / fS;
	sinOmega = arm_sin_f32(omega);
	cosOmega = arm_cos_f32(omega);

	if(Q_OR_BW < 0) Q_OR_BW = 0.001;

	switch(filterType) {

	case kFilter_Low_Pass:
		alpha = sinOmega / (2.0 * Q_OR_BW);
		b0 = (1 - cosOmega) / 2.0;
		b1 = 1 - cosOmega;
		b2 = b0;
		a0 = 1 + alpha;
		a1 = -2.0 * cosOmega;
		a2 = 1 - alpha;
		break;

	case kFilter_High_Pass:
		alpha = sinOmega / (2.0 * Q_OR_BW);
		b0 = (1 + cosOmega) / 2.0;
		b1 = -(1 + cosOmega); // TI disagrees in their app note SLAA447, but it sure sounds like TI is wrong
		b2 = b0;
		a0 = 1 + alpha;
		a1 = -2.0 * cosOmega;
		a2 = 1 - alpha;
		break;

	case kFilter_Band_Pass:
		invHlfQ = sinhf(logf(2.0) / 2.0 * Q_OR_BW * omega / sinOmega);
		alpha = invHlfQ * sinOmega;
		b0 = alpha;
		b1 = 0;
		b2 = -alpha;
		a0 = 1 + alpha;
		a1 = -2.0 * cosOmega;
		a2 = 1 - alpha;
		break;

	default:
		break;
	}

		coeffs[0] = b0 / a0;
		coeffs[1] = b1 / a0;
		coeffs[2] = b2 / a0;
		coeffs[3] = a1 / a0;
		coeffs[4] = a2 / a0;
}


/*
 * runLowpassBiquad
 *
 * Performs the antialiasing filter on the input.
 * Returns the next filtered output.
 *
 * Uses the input float[5] array of coefficients.
 *
 * This introduces a delay of two samples to the vocoder output.
 */
float runLowpassBiquad(float newInput, float *coeffs) {

	float newOutput = coeffs[0] * newInput
					+ coeffs[1] * lowpassBiquadInputs[0]
					+ coeffs[2] * lowpassBiquadInputs[1]
					- coeffs[3] * lowpassBiquadOutputs[0]
					- coeffs[4] * lowpassBiquadOutputs[1];

	lowpassBiquadInputs[1] = lowpassBiquadInputs[0];
	lowpassBiquadInputs[0] = newInput;

	lowpassBiquadOutputs[1] = lowpassBiquadOutputs[0];
	lowpassBiquadOutputs[0] = newOutput;

	return newOutput;
}
/*
 * runSibilanceBiquad
 *
 * Performs the high-frequency bypass filter on the input.
 * Returns the next filtered output.
 *
 * Uses the input float[5] array of coefficients.
 */
float runSibilanceBiquad(float newInput, float *coeffs) {

	float newOutput = coeffs[0] * newInput
					+ coeffs[1] * sibilanceBiquadInputs[0]
					+ coeffs[2] * sibilanceBiquadInputs[1]
					- coeffs[3] * sibilanceBiquadOutputs[0]
					- coeffs[4] * sibilanceBiquadOutputs[1];

	sibilanceBiquadInputs[1] = sibilanceBiquadInputs[0];
	sibilanceBiquadInputs[0] = newInput;

	sibilanceBiquadOutputs[1] = sibilanceBiquadOutputs[0];
	sibilanceBiquadOutputs[0] = newOutput;

	return newOutput;
}
/*
 * runAnalysisBiquad
 *
 * Performs a series of analysis bandpass captures
 * on the filtered voice input; done every six CODEC samples.
 *
 * After running this function, the new analysis results
 * are available for further computation by calling
 * analysisBiquadOutputs[n][0] for the desired band.
 *
 * Uses the input float[NUM_VOCODER_BANDS * 5] array of coefficients.
 *
 * This introduces a delay of 12 samples to the vocoder output.
 */
void runAnalysisBiquad(float newInput, float *coeffs) {

	for(uint32_t i = 0; i < NUM_VOCODER_BANDS; ++i) {

		analysisBiquadOutputs[i][2] = analysisBiquadOutputs[i][1];
		analysisBiquadOutputs[i][1] = analysisBiquadOutputs[i][0];

		analysisBiquadOutputs[i][0] = coeffs[5 * i] * newInput
									+ coeffs[5 * i + 2] * analysisBiquadInputs[1]
									- coeffs[5 * i + 3] * analysisBiquadOutputs[i][1]
									- coeffs[5 * i + 4] * analysisBiquadOutputs[i][2];

		analysisBiquadAbs[i] = fabsf(analysisBiquadOutputs[i][0]);
	}

	analysisBiquadInputs[1] = analysisBiquadInputs[0];
	analysisBiquadInputs[0] = newInput;
}
/*
 * runEnvelopeFollower
 *
 * Performs a series of lowpass filters on the absolute
 * value of the analysis filter results (inputArray).
 * This operation must be performed once the analysis
 * filter runs.
 *
 * Uses the input float[5] array of coefficients.
 *
 * After running this function, the new envelope results
 * are available in envelopeFollowerOutputs[n][0] for the desired band.
 *
 * Introduces one sample of delay. This is only
 * the case for performance reasons (lots of float operations
 * if this is done on the same sample as everything else).
 */
void runEnvelopeFollower(float *inputArray, float *coeffs) {

	for(uint32_t i = 0; i < NUM_VOCODER_BANDS; ++i) {

		envelopeFollowerOutputs[i][2] = envelopeFollowerOutputs[i][1];
		envelopeFollowerOutputs[i][1] = envelopeFollowerOutputs[i][0];

		envelopeFollowerOutputs[i][0] = coeffs[0] * inputArray[i]
									  + coeffs[1] * envelopeFollowerInputs[i][0]
									  + coeffs[2] * envelopeFollowerInputs[i][1]
									  - coeffs[3] * envelopeFollowerOutputs[i][1]
									  - coeffs[4] * envelopeFollowerOutputs[i][2];

		envelopeFollowerInputs[i][1] = envelopeFollowerInputs[i][0];
		envelopeFollowerInputs[i][0] = inputArray[i];

	}
}
/*
 * runShapingBiquad
 *
 * Performs a series of shaping bandpass captures
 * on the synthesizer output; done once each CODEC sample.
 *
 * After running this function, the new shaping results
 * are available for multiplication by calling
 * shapingBiquadOutputs[n][0] for the desired band.
 *
 * Uses the input float[NUM_VOCODER_BANDS * 5] array of coefficients.
 *
 * Introduces a delay of 2 samples to the vocoder output.
 * This also introduces a delay of 2 samples to the synth output.
 */
void runShapingBiquad(float newInput, float *coeffs) {

	for(uint32_t i = 0; i < NUM_VOCODER_BANDS; ++i) {

		shapingBiquadOutputs[i][2] = shapingBiquadOutputs[i][1];
		shapingBiquadOutputs[i][1] = shapingBiquadOutputs[i][0];

		shapingBiquadOutputs[i][0] = coeffs[5 * i] * newInput
								   + coeffs[5 * i + 2] * shapingBiquadInputs[1]
								   - coeffs[5 * i + 3] * shapingBiquadOutputs[i][1]
								   - coeffs[5 * i + 4] * shapingBiquadOutputs[i][2];
	}

	shapingBiquadInputs[1] = shapingBiquadInputs[0];
	shapingBiquadInputs[0] = newInput;
}


/*
 * SAI1_IRQHandler
 *
 * Our CODEC simultaneous send/receive interrupt.
 * This interrupt is scheduled to occur whenever the Tx
 * FIFO hits its watermark at 16 words remaining.
 *
 * In the interrupt, we only update from/to the FIFOs
 * using our protected global audio arrays.
 *
 * These global arrays are updated using thread guarding
 * within our program loop.
 *
 * This ticks the audio sampling heartbeat of the application
 * with SAI_RequestSynthUpdate.
 */
void SAI1_IRQHandler(void) {

	// Read from FIFO into SAI1_rxAudio[2]
	SAI1_rxAudio[0] = SAI_ReadData(SAI_1_PERIPHERAL, 0);
	SAI1_rxAudio[1] = SAI_ReadData(SAI_1_PERIPHERAL, 0);

	// Write to FIFO from SAI1_txAudio[2]
	SAI_WriteData(SAI_1_PERIPHERAL, 0, SAI1_txAudio[0]);
	SAI_WriteData(SAI_1_PERIPHERAL, 0, SAI1_txAudio[1]);

	// Lastly, clear any halting status flags
	SAI_RxClearStatusFlags(SAI_1_PERIPHERAL, kSAI_WordStartFlag | kSAI_FIFOErrorFlag);
	SAI_TxClearStatusFlags(SAI_1_PERIPHERAL, kSAI_WordStartFlag | kSAI_FIFOErrorFlag);

	SAI_RequestSynthUpdate = 1;

}


/*
 * PIT_IRQHandler
 *
 * Used to digitally debounce the SW4 input.
 * Necessary to ensure each press and release results
 * in only ONE press event.
 *
 * Somewhat less expensive than constantly checking in the main while().
 */
void PIT_IRQHandler(void) {

	if(!GPIO_PinRead(BOARD_USER_BUTTON_GPIO, BOARD_USER_BUTTON_GPIO_PIN)) {
		g_sw4Debounce++;
    }
	else g_sw4Debounce = 0;

	if(g_sw4Debounce > 10) g_sw4Pressed = 1;
	else g_sw4Pressed = 0;

	PIT_ClearStatusFlags(PIT_1_PERIPHERAL, kPIT_Chnl_0, kPIT_TimerFlag);

}
/*
 * getSW4Pressed
 *
 * Safe read for the value of g_sw4Pressed.
 */
_Bool getSW4Pressed(void) {

	NVIC_DisableIRQ(PIT_IRQn);
	_Bool temp = g_sw4Pressed;
	NVIC_EnableIRQ(PIT_IRQn);

	return temp;

}


/*!
 * @brief host callback function.
 *
 * device attach/detach callback function.
 *
 * @param deviceHandle         device handle.
 * @param configurationHandle  attached device's configuration descriptor information.
 * @param eventCode            callback event code, please reference to enumeration host_event_t.
 *
 * @retval kStatus_USB_Success              The host is initialized successfully.
 * @retval kStatus_USB_NotSupported         The application don't support the configuration.
 */
usb_status_t USB_HostEvent(usb_device_handle deviceHandle,
                           usb_host_configuration_handle configurationHandle,
                           uint32_t eventCode) {

	usb_status_t status = kStatus_USB_Success;

    switch (eventCode & 0x0000FFFFU)
    {
        case kUSB_HostEventAttach:
        	PRINTF("\n\nEvent attach...\n");
            status = USB_HostMidiEvent(deviceHandle, configurationHandle, eventCode);
            break;

        case kUSB_HostEventNotSupported:
            break;

        case kUSB_HostEventEnumerationDone:
            status = USB_HostMidiEvent(deviceHandle, configurationHandle, eventCode);
            break;

        case kUSB_HostEventDetach:
        	PRINTF("\nEvent detach...\n");
            status = USB_HostMidiEvent(deviceHandle, configurationHandle, eventCode);
            break;

        case kUSB_HostEventEnumerationFail:
            PRINTF("Enumeration failed...\n");
            break;

        default:
            break;
    }
    return status;
}

void USB_HostApplicationInit(void)
{
    usb_status_t status = kStatus_USB_Success;

    /*
     * USB_HostClockInit Initializes:
     *  - The PHY PLL clock
     *  - The USB HS 0 clock
     *  - The PHY
     */
    USB_HostClockInit();

#if ((defined FSL_FEATURE_SOC_SYSMPU_COUNT) && (FSL_FEATURE_SOC_SYSMPU_COUNT))
    SYSMPU_Enable(SYSMPU, 0);
#endif /* FSL_FEATURE_SOC_SYSMPU_COUNT */

    status = USB_HostInit(CONTROLLER_ID, &g_demoUSBHostHandle, USB_HostEvent);
    if (status != kStatus_USB_Success)
    {
        PRINTF("Host init error!!\r\n");
        return;
    }
    USB_HostIsrEnable();

    PRINTF("...Host init done.\r\n");
}

/*
 * USB_OTG1_IRQHandler
 *
 * Activates the EHCI IRQ Handler on USB OTG interrupts.
 * REQUIRES A GLOBAL usb_host_handle OBJECT.
 */
void USB_OTG1_IRQHandler(void) {
    USB_HostEhciIsrFunction(g_demoUSBHostHandle);
}

/*
 * handleMidiEventPacket
 *
 * Must be called for each synthesizer. Should only be called when there is
 * valid data in event. Otherwise, we risk redundant event handling.
 *
 * Confirms the associated channel number.
 * Presses or releases keys, using MIDI commands "Note_On" and "Note_Off".
 * Updates channel pitchbend.
 * Future functionality pending...
 */
void handleMidiEventPacket(wavetableSynth *synth, usbmidi_event_packet_t event) {

	usbmidi_code_index_number_t eventCIN = event.CCIN & 0x0F;
	//usbmidi_cable_number_t cable = (event.CCIN & 0xF0) >> 4;

	uint8_t eventByte0 = event.MIDI_0;
	usbmidi_channel_number_t chNum = eventByte0 & 0x0F;

	uint8_t eventByte1 = event.MIDI_1;
	uint8_t eventByte2 = event.MIDI_2;

	if(synth->midiChannel != chNum) return;


	switch(eventCIN) {
	case kUSBMIDI_CIN_Misc:
		break;
	case kUSBMIDI_CIN_Cable_Event:
		break;
	case kUSBMIDI_CIN_Two_Byte_Common_Msg:
		break;
	case kUSBMIDI_CIN_Three_Byte_Common_Msg:
		break;
	case kUSBMIDI_CIN_SysEx_Start_Continue:
		break;
	case kUSBMIDI_CIN_Single_Byte_Common_Msg:
		break;
	case kUSBMIDI_CIN_SysEx_Ends_Two_Bytes:
		break;
	case kUSBMIDI_CIN_SysEx_Ends_Three_Bytes:
		break;
	case kUSBMIDI_CIN_Note_Off:
		releaseKey(synth, eventByte1);
		break;
	case kUSBMIDI_CIN_Note_On:
		pressKey(synth, eventByte1, eventByte2);
		break;
	case kUSBMIDI_CIN_Poly_Keypress:
		break;
	case kUSBMIDI_CIN_Control_Change:
		break;
	case kUSBMIDI_CIN_Program_Change:
		break;
	case kUSBMIDI_CIN_Channel_Pressure:
		break;
	case kUSBMIDI_CIN_Pitchbend_Change:
		updatePitchbend(synth, eventByte1, eventByte2);
		break;
	case kUSBMIDI_CIN_System_Message:
		break;
	default:
		break;
	}

}



/* * * * * * * * * * * * * * * * * * * * * * *
 * APPLICATION ENTRY:
 * * * * * * * * * * * * * * * * * * * * * * */
int main(void) {

  	/* Init board hardware */
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitBootPeripherals();
  	/* Init FSL debug console */
    BOARD_InitDebugConsole();


    PRINTF("Initializing wavetables...\n");
    setWavetableSine(wavetableSine, kSynth_Table_Length);
    setWavetableTri(wavetableTri, kSynth_Table_Length);
    setWavetableSaw(wavetableSaw, kSynth_Table_Length);
    setWavetableNovel(wavetableNovel, kSynth_Table_Length);

    wavetableSynth demoSynth;
    initSynth(&demoSynth, kSynth_Num_Keys, kSynth_A3_Index, TONE_A3_HZ, kUSBMIDI_Channel_1);
    demoSynth.wavetable = &wavetableSaw[0];
    g_activeWavetable = 2;

    /*
     * If the user holds the USER BUTTON (SW4 on the RT1010-EVK) while
     * toggling the RESET switch (SW9 on the EVK), the program will enter
     * a mode that ignores MIDI commands sent over USB.
     *
     * Instead, a series of pre-defined chords will play with a default Saw tone.
     * These chords can be toggled by the user by pressing the USER BUTTON.
     * This allows some musical experimentation without a MIDI controller!
     */
    _Bool noMidiDemo = 0;
    _Bool funcToggled = 0;
    if(!GPIO_PinRead(BOARD_USER_BUTTON_GPIO, BOARD_USER_BUTTON_GPIO_PIN)) {
    	noMidiDemo = 1;
    	playDemoChord(&demoSynth, g_activeDemoChord);
    }


    PRINTF("Initializing vocoder...\n");

    float lowpassBiquadCoeffs[5] 						= {0};
    float sibilanceBiquadCoeffs[5] 						= {0};
    float analysisBiquadCoeffs[NUM_VOCODER_BANDS * 5]	= {0};
    float envelopeFollowerCoeffs[5]						= {0};
    float shapingBiquadCoeffs[NUM_VOCODER_BANDS * 5]	= {0};

    /* calculate antialiasing filter coefficients */
    calculateBiquadCoeffs(lowpassBiquadCoeffs, (float)kResample_Phoneme_LP,
    		(float)kAudio_Frame_Hz, kFilter_Low_Pass, lowpassBiquadQ);

    /* calculate sibilance filter coefficients */
    calculateBiquadCoeffs(sibilanceBiquadCoeffs, (float)kResample_Sibilance_HP,
    		(float)kAudio_Frame_Hz, kFilter_High_Pass, sibilanceBiquadQ);

    /* calculate envelope follower filter coefficients */
    calculateBiquadCoeffs(envelopeFollowerCoeffs, (float)kResample_Envelope_Freq,
    		(float)kAudio_Frame_Hz / 6, kFilter_Low_Pass, envelopeFollowerQ);

    for(int band = 0; band < NUM_VOCODER_BANDS; band++) {

    	/* calculate voice analysis filter coefficients per band */
    	calculateBiquadCoeffs(&analysisBiquadCoeffs[band * 5], bandpassBiquadF0[band],
    			(float)kAudio_Frame_Hz / 6, kFilter_Band_Pass, analysisBiquadBWs[band]);

    	/* calculate synth shaping filter coefficients per band */
    	calculateBiquadCoeffs(&shapingBiquadCoeffs[band * 5], bandpassBiquadF0[band],
    	    	(float)kAudio_Frame_Hz, kFilter_Band_Pass, shapingBiquadBWs[band]);

    }

    uint32_t voxDownsampleCount = 0;
    float aaVoice = 0;
    float sibilanceBypass = 0;
    float summedAudio = 0;


    /*
     * For the demo speakEZ applications, I will not be using
     * the built-in WM8960 driver from NXP. Instead, I wrote
     * a minimal driver for getting the CODEC running that
     * uses some fsl_wm8960.h definitions. Many settings are
     * set in stone, according to SPF-45852 rev C.
     *
     * If you are using speakEZ with your own board & CODEC,
     * it will of course be necessary to re-write these drivers.
	 */

    PRINTF("Initializing WM8960 codec...\n");
    LPI2C1->MCR |= 1U << 3; // Enable master control in debug mode
    configureWM8960();


    PRINTF("Initializing SAI1...\n");
    SAI1->TCSR |= 0b1U << 29; // Enable debug SAI transfers
    SAI1->RCSR |= 0b1U << 29; // Enable debug SAI reads
    SAI_TxEnable(SAI_1_PERIPHERAL, 1);
    SAI_RxEnable(SAI_1_PERIPHERAL, 1);

    if(!noMidiDemo) {
    	PRINTF("Initializing USB Host...\n");
    	USB_HostApplicationInit();
    }



    /* * * * * * * * * * * * * * * * * * * * * * *
     * INFINITE LOOP:
     * * * * * * * * * * * * * * * * * * * * * * */
    while(1) {


    	/* Play the synth, listen to the voice, and run the vocoder filters */
        if(getSAI_RequestSynthUpdate()) {

        	getRxAudio(inputAudioBuffer);

        	aaVoice = runLowpassBiquad((float)inputAudioBuffer[1], lowpassBiquadCoeffs);				// Save the low-passed voice
        	sibilanceBypass = runSibilanceBiquad((float)inputAudioBuffer[1], sibilanceBiquadCoeffs);	// Save the high-passed voice

        	if(voxDownsampleCount == 0) {
        		runAnalysisBiquad(aaVoice, analysisBiquadCoeffs);		// Capture the filtered amplitude from each downsampled voice band
        	}
        	if(voxDownsampleCount == 1) {
        		runEnvelopeFollower(analysisBiquadAbs, envelopeFollowerCoeffs); // Run the follower one sample delayed for performance reasons
        	}

        	runShapingBiquad((float)playSynth(&demoSynth), shapingBiquadCoeffs);	// Capture the filtered amplitude from each synth band

        	summedAudio = 0;
        	for(int i = 0; i < NUM_VOCODER_BANDS; ++i) {
        		summedAudio += shapingBiquadOutputs[i][0] * envelopeFollowerOutputs[i][0] * 0.00005; 	// Modulate the synth data
        	}
        	summedAudio += sibilanceBypass;															// Add in consonants from speech

        	outputAudioBuffer[0] = (int32_t)summedAudio;
        	outputAudioBuffer[1] = outputAudioBuffer[0];

        	setTxAudio(outputAudioBuffer);

        	if(++voxDownsampleCount >= kResample_Downsample_Rate) {
        		voxDownsampleCount = 0;
        	}

        	clearSAI_RequestSynthUpdate();
        }


        /* Handle USB events and parse received packets/data */
        if(!noMidiDemo){
        	USB_HostTaskFn(g_demoUSBHostHandle);
        	USB_HostMidiTask(&g_demoMidiInstance);

        	if(g_demoMidiPacketRecvFlag) {
        		handleMidiEventPacket(&demoSynth, g_demoMidiEventPacket);
        		g_demoMidiPacketRecvFlag = 0;
        	}
        }


        /* Handle User Button functions and LED feedback */
        if(!funcToggled && getSW4Pressed()) {

        	USER_LED_ON();

        	if(noMidiDemo) toggleDemoChord(&demoSynth);
        	else toggleActiveWavetable(&demoSynth);

        	funcToggled = 1;
        }
        else {
        	if(funcToggled && !getSW4Pressed()) {

        		USER_LED_OFF();

        		funcToggled = 0;
        	}
        }


    }
    return 0;
}
