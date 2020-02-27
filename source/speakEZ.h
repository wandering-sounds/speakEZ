/*
 * speakEZ.h
 *
 *  Revision 1
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

#ifndef SPEAKEZ_H_
#define SPEAKEZ_H_

#include "arm_math.h"
#include "usbmidi.h"

#define TWELFTH_ROOT_OF_TWO 	1.05946309436f
#define THIRD_ROOT_OF_TWO		1.25992104989f
#define TONE_A3_HZ				220.0f

enum _speakEZ_synth_constants {
	kSynth_Table_Length 	= 512U,
	kSynth_Max_Audio_Level 	= 3000000, // To protect the ears. 8388607 is true max
	kSynth_Min_Audio_Level	= -3000000, // To protect the ears. -8388608 is true min
	kSynth_Num_Keys			= 128U,
	kSynth_Max_Velocity		= 127U,
	kSynth_A3_Index			= 57U, 	// freq index for A3 = 220 Hz
	kSynth_Pbend_Semitones	= 2U	// number of semitones that can be bent up, or down
};

enum _speakEZ_audio_constants {
	kAudio_Frame_Hz = 46880U, // Measured with logic analyzer on LRCK, despite 48000 Hz MCUXpresso setting
	kAudio_Buffer_Words = 2U
};


status_t writeToWM8960(uint8_t controlReg, uint16_t controlWord);
void configureWM8960();


int32_t inputAudioBuffer[kAudio_Buffer_Words] = 			{0}; // Rx buffer used in program calculations
volatile int32_t SAI1_rxAudio[kAudio_Buffer_Words] = 		{0}; // Protected Rx buffer used by SAI1_IRQHandler
int32_t outputAudioBuffer[kAudio_Buffer_Words] = 			{0}; // Tx buffer used in program calculations
volatile int32_t SAI1_txAudio[kAudio_Buffer_Words] = 		{0}; // Protected Tx buffer used by SAI1_IRQHandler


void setTxAudio(int32_t *audioBuffer);
void getRxAudio(int32_t *audioBuffer);


/*
 * wavetableSynth Structure
 *
 * Designed to contain all the information necessary
 * to keep track of the current state of the audio generator.
 *
 * Essentially creates a digital keyboard.
 */
typedef struct wavetableSynth {

	float freq[kSynth_Num_Keys];
	float phase[kSynth_Num_Keys];
	float phaseIncrement[kSynth_Num_Keys];
	float pbendFactor;
	uint32_t velocity[kSynth_Num_Keys];
	int32_t *wavetable;

	usbmidi_channel_number_t midiChannel;

} wavetableSynth;

/*
 * Below are some wavetables with assigned, predefined
 * generating functions.
 *
 * I encourage you to try making your own!
 *
 * There are so many unique periodic sounds you can
 * make with interesting mathematical statements.
 */
int32_t wavetableSine[kSynth_Table_Length] 		= {0};
int32_t wavetableTri[kSynth_Table_Length] 		= {0};
int32_t wavetableSaw[kSynth_Table_Length] 		= {0};
int32_t wavetableNovel[kSynth_Table_Length] 	= {0};
enum _speakEZ_wavetable_library {
	kSynth_Wavetable_Sine						= 0,
	kSynth_Wavetable_Tri,
	kSynth_Wavetable_Saw,
	kSynth_Wavetable_Novel
};
#define  NUM_WAVETABLES							  4U
uint32_t g_activeWavetable						= 0;


#define NUM_DEMO_CHORDS							  8U
#define NUM_DEMO_NOTES							  7U
const uint32_t demoChords[NUM_DEMO_CHORDS][NUM_DEMO_NOTES] = {
		{33, 45, 52, 57, 60, 64, 69},
		{36, 43, 48, 55, 60, 64, 67},
		{40, 47, 52, 56, 59, 64, 68},
		{41, 48, 53, 57, 60, 65, 69},
		{43, 50, 55, 59, 62, 67, 71},
		{41, 50, 57, 60, 65, 69, 72},
		{43, 50, 57, 62, 65, 69, 72},
		{36, 48, 55, 60, 67, 72, 76}
};
uint32_t g_activeDemoChord						= 0;


void setWavetableSine(int32_t *inputWavetable, uint32_t tableLen);
void setWavetableTri(int32_t *inputWavetable, uint32_t tableLen);
void setWavetableSaw(int32_t *inputWavetable, uint32_t tableLen);
void setWavetableNovel(int32_t *inputWavetable, uint32_t tableLen);

void toggleActiveWavetable(wavetableSynth *synth);
void playDemoChord(wavetableSynth *synth, uint32_t chordNum);
void toggleDemoChord(wavetableSynth *synth);

void initSynth(wavetableSynth *synth, uint32_t numKeys, uint32_t indexA3, float freqA3, usbmidi_channel_number_t chNum);
int32_t playSynth(wavetableSynth *synth);
void pressKey(wavetableSynth *synth, uint32_t keyIndex, uint32_t keyVelocity);
void releaseKey(wavetableSynth *synth, uint32_t keyIndex);
void updatePitchbend(wavetableSynth *synth, uint32_t pbLSB, uint32_t pbMSB);


_Bool getSAI_RequestSynthUpdate();
void clearSAI_RequestSynthUpdate();

_Bool getSW4Pressed(void);

void handleMidiEventPacket(wavetableSynth *synth, usbmidi_event_packet_t event);


/*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*
 *~*~* V O C O D E R   S T U F F *~*~*
 *~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*~*/

enum _speakEZ_resampling_constants {
	kResample_Downsample_Rate = 6, // CODEC LRCK cycles per downsample
	kResample_Phoneme_LP = 3400, // Hz
	kResample_Sibilance_HP = 3500, // Hz
	kResample_Envelope_Freq = 100 // Hz
};

typedef enum _speakEZ_filter_types {
	kFilter_Low_Pass = 0,
	kFilter_High_Pass,
	kFilter_Band_Pass
} filter_type_t;


void calculateBiquadCoeffs(float *coeffs, float fC, float fS, uint8_t filterType, float Q_OR_BW);

float lowpassBiquadQ				= 0.9;
float lowpassBiquadInputs[2]		= {0};
float lowpassBiquadOutputs[2]		= {0};
float runLowpassBiquad(float newInput, float *coeffs);


float sibilanceBiquadQ				= 0.9;
float sibilanceBiquadInputs[2]		= {0};
float sibilanceBiquadOutputs[2]		= {0};
float runSibilanceBiquad(float newInput, float *coeffs);

/*
 * I calculated the center frequencies at about four
 * bands per octave, from 3100 Hz down to around 150 Hz.
 *
 * Feel free to play around with these frequencies!
 */
#define NUM_VOCODER_BANDS			18
const float bandpassBiquadF0[NUM_VOCODER_BANDS] = {
		3100.0,
		2605.0,
		2190.0,
		1845.0,
		1550.0,
		1305.0,
		1095.0,
		 920.0,
		 775.0,
		 650.0,
		 550.0,
		 460.0,
		 390.0,
		 325.0,
		 275.0,
		 230.0,
		 195.0,
		 165.0
};

float analysisBiquadBWs[NUM_VOCODER_BANDS] 			= {0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1};
float analysisBiquadInputs[2] 						= {0};
float analysisBiquadOutputs[NUM_VOCODER_BANDS][3] 	= {0};
float analysisBiquadAbs[NUM_VOCODER_BANDS]	 		= {0};
void runAnalysisBiquad(float newInput, float *coeffs);


float envelopeFollowerQ								= 0.9;
float envelopeFollowerInputs[NUM_VOCODER_BANDS][2] 	= {0};
float envelopeFollowerOutputs[NUM_VOCODER_BANDS][3] = {0};
void runEnvelopeFollower(float *inputArray, float *coeffs);


float shapingBiquadBWs[NUM_VOCODER_BANDS] 			= {0.2,0.2,0.2,0.2,0.2,0.2,0.2,0.2,0.2,0.2,0.2,0.2,0.2,0.2,0.2,0.2,0.2,0.2};
float shapingBiquadInputs[2] 						= {0};
float shapingBiquadOutputs[NUM_VOCODER_BANDS][3] 	= {0};
void runShapingBiquad(float newInput, float *coeffs);

#endif /* SPEAKEZ_H_ */
