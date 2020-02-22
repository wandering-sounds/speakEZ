# speakEZ
Vocoder and USB MIDI wavetable synth, built for the MIMXRT1010-EVK.

# Requirements
* MIMXRT1010-EVK module
* TRS Headphones (generic)
* MIDI controller/sequencer/keyboard
* USB OTG Micro-B Male to USB Type-A Female adapter

# Flashing Instructions
1. Download this repo:

        https://github.com/wandering-sounds/speakEZ/archive/master.zip

2. Plug your EVK into your PC using the debug USB port (J41).

3. Open MCUXpresso IDE. In the Quickstart Panel, select "Import project(s) from file system...".

4. Under "Project archive (zip)", select the "Browse..." button. Locate and select the archive you downloaded.

5. Click "Next >" and then "Finish". The speakEZ project should appear in your "Project Explorer" tab.

6. Click on the project folder. Click on the "GUI Flash Tool" icon in your top bar.

7. A window should pop up displaying the debug probe for the EVK. Select "OK".

8. Under "Target Operations" > "Program" > "Options", find "File to program". Click on the button "Workspace...".

9. Find and double-click on /Release/speakEZ.axf. Click "Run".

10. Make sure your dip switches are set to 1-[ OFF OFF ON OFF ]-4. Press the POR Reset Switch (SW9).

# Using speakEZ with the EVK
Once you've pressed SW9, speakEZ is ready. Carefully insert your headphones.

You can try the no-MIDI demo by holding the User Button (SW4) while you press the POR button. 
Demo chords will begin to play with a saw synth. 
To change the demo chord, press the User Button again. It will progress through a sequence of eight demo chords. 
Speaking or singing anywhere near the microphone will produce vocoded synth tones. 
To return to the regular mode, press the POR button without holding the User Button.

In the regular mode, plug any MIDI keyboard with a USB cable into the USB OTG connector (J9). 
You will likely need an adapter to convert from USB Type-A (mating with the keyboard cable) to a USB OTG (mating with the EVK).

Pressing and releasing keys on the keyboard will play and release notes on the synth. 
Like in the no-MIDI demo, singing or speaking near the mic on the EVK will transmit the phonemes onto the synth. 
Pressing the User Button in this mode will change the wavetable the synth uses on the fly. The order progresses:

        1. - - - Saw (default, standard vocoder sound)
        2. - - - Novel Waveform (harsh, glitchy, good for single notes)
        3. - - - Sine (nearly silent, for demonstration purposes)
        4. - - - Triangle (slightly thicker than Sine)
        
The wavetable selected will loop back to Saw once you have progressed through each of these.

# License
Code: 3-Clause BSD

Copyright 2020 Brady Etz

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

