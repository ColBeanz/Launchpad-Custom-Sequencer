/******************************************************************************

 Copyright (c) 2015, Focusrite Audio Engineering Ltd.
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.

 * Neither the name of Focusrite Audio Engineering Ltd., nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 *****************************************************************************/

//______________________________________________________________________________
//
// Headers
//______________________________________________________________________________

#include "app.h"

//______________________________________________________________________________
//
// This is where the fun is!  Add your code to the callbacks below to define how
// your app behaves.
//
// In this example, we either render the raw ADC data as LED rainbows or store
// and recall the pad state from flash.
//______________________________________________________________________________

// timing defs
#define MS_PER_MIN 60000
#define CLOCK_RATE 24

// store ADC frame pointer
static const u16 *g_ADC = 0;

// buffer to store pad states for flash save
#define BUTTON_COUNT 100

u8 g_Buttons[BUTTON_COUNT] = {0};

#define RANGE 8
#define STEPS 8

u8 DRUM_NOTES[RANGE] =
{
	36, 37, 38, 39, 40, 41, 42, 43
};

struct PadState{
	u8 steps[STEPS];
	u8 pos;
};

struct PadState Channel1 = { .pos = 0};

int IsNoteOn(u8 flags, u8 bit)
{
    return ((flags & (1 << bit)) != 0);
}

void SetFlag(u8* steps, u8 note)
{
	if (note > 80)
	{
		steps[note-80] = 128;
	}
	if (note > 70)
	{
		steps[note-70] = 64;
	}
	if (note > 60)
	{
		steps[note-60] = 32;
	}
	if (note > 50)
	{
		steps[note-50] = 16;
	}
	if (note > 40)
	{
		steps[note-40] = 8;
	}
	if (note > 30)
	{
		steps[note-30] = 4;
	}
	if (note > 20)
	{
		steps[note-20] = 2;
	}
	if (note > 10)
	{
		steps[note-10] = 1;
	}
}

void TriggerNotes(u8 step) // step is the 8 bit binary flag from which the midi notes will be derived
{
    for (int bitPosition = 0; bitPosition < RANGE; bitPosition++)
    {
        if (IsNoteOn(step, bitPosition))
        {
			hal_send_midi(DINMIDI, NOTEON | 0, DRUM_NOTES[bitPosition], MAXLED);
			hal_send_midi(USBSTANDALONE, NOTEON | 0, DRUM_NOTES[bitPosition], MAXLED);
        }
    }
};

int CalculateMsPerClock(int tempo)
{
    return MS_PER_MIN / (CLOCK_RATE * tempo);
}

void PlotPlayhead(int step)
{
	if (step == 0)
	{
		for (int i = 1; i < 9; i++)
		{
			hal_plot_led(TYPEPAD, 8 + (i * 10), 0, 0, 0);
		}
	}
	else
	{
		for (int i = 1; i < 9; i++)
		{
			hal_plot_led(TYPEPAD, step + (i * 10), 0, 0, 0);
		}
	}

	for (int i = 1; i < 9; i++)
	{
		hal_plot_led(TYPEPAD, (step + 1) + (i * 10), MAXLED, MAXLED, MAXLED);
	}
}

//______________________________________________________________________________

void app_surface_event(u8 type, u8 index, u8 value)
{
    switch (type)
    {
        case  TYPEPAD:
        {
            // toggle it and store it off, so we can save to flash if we want to
            if (value)
            {
                g_Buttons[index] = MAXLED * !g_Buttons[index];
            }

			SetFlag(Channel1.steps, index);

            // example - light / extinguish pad LEDs
            hal_plot_led(TYPEPAD, index, 0, 0, g_Buttons[index]);

            // example - send MIDI
            hal_send_midi(DINMIDI, NOTEON | 0, index, value);
			hal_send_midi(USBSTANDALONE, NOTEONCHONE, index, value);

        }
        break;

        case TYPESETUP:
        {
            if (value)
            {
                // save button states to flash (reload them by power cycling the hardware!)
                hal_write_flash(0, g_Buttons, BUTTON_COUNT);
            }
        }
        break;
    }
}

//______________________________________________________________________________

void app_midi_event(u8 port, u8 status, u8 d1, u8 d2)
{
    // example - MIDI interface functionality for USB "MIDI" port -> DIN port
    if (port == USBMIDI)
    {
        hal_send_midi(DINMIDI, status, d1, d2);
    }

    // // example -MIDI interface functionality for DIN -> USB "MIDI" port port
    if (port == DINMIDI)
    {
        hal_send_midi(USBMIDI, status, d1, d2);
    }
}

//______________________________________________________________________________

void app_sysex_event(u8 port, u8 * data, u16 count)
{
    // example - respond to UDI messages?
}

//______________________________________________________________________________

void app_aftertouch_event(u8 index, u8 value)
{
    // example - send poly aftertouch to MIDI ports
    hal_send_midi(USBMIDI, POLYAFTERTOUCH | 0, index, value);


}

//______________________________________________________________________________

void app_cable_event(u8 type, u8 value)
{
    // example - light the Setup LED to indicate cable connections
    if (type == MIDI_IN_CABLE)
    {
        hal_plot_led(TYPESETUP, 0, 0, value, 0); // green
    }
    else if (type == MIDI_OUT_CABLE)
    {
        hal_plot_led(TYPESETUP, 0, value, 0, 0); // red
    }
}

//______________________________________________________________________________


static int g_tempo = 120;
static int g_ms_per_tick;


void app_timer_event()
{
    static int ms = 0;
    static int semiquaver = 0;

    if (++ms >= g_ms_per_tick)
    {
        ms = 0;

        // send a clock pulse up the USB
        hal_send_midi(USBSTANDALONE, MIDITIMINGCLOCK, 0, 0);

        if (++semiquaver >= 6)
        {
            semiquaver = 0;

		    if (Channel1.pos >= STEPS)
		    {
				Channel1.pos = 0;
		    }

			PlotPlayhead(Channel1.pos);

			TriggerNotes(Channel1.steps[Channel1.pos]);

		    Channel1.pos++;
		}

    }

    static int position = 0;



/*
	// alternative example - show raw ADC data as LEDs
	for (int i=0; i < PAD_COUNT; ++i)
	{
		// raw adc values are 12 bit, but LEDs are 6 bit.
		// Let's saturate into r;g;b for a rainbow effect to show pressure
		u16 r = 0;
		u16 g = 0;
		u16 b = 0;

		u16 x = (3 * MAXLED * g_ADC[i]) >> 12;

		if (x < MAXLED)
		{
			r = x;
		}
		else if (x >= MAXLED && x < (2*MAXLED))
		{
			r = MAXLED - x;
			g = x - MAXLED;
		}
		else
		{
			g = MAXLED - x;
			b = x - MAXLED;
		}

		hal_plot_led(TYPEPAD, ADC_MAP[i], r, g, b);
	}
 */
}

//______________________________________________________________________________

void app_init(const u16 *adc_raw)
{
    // calculate how many milliseconds per tick for midi clock
    g_ms_per_tick = CalculateMsPerClock(g_tempo);
    // example - load button states from flash
    hal_read_flash(0, g_Buttons, BUTTON_COUNT);

    // example - light the LEDs to say hello!
    for (int i=0; i < 10; ++i)
    {
        for (int j=0; j < 10; ++j)
        {
            u8 b = g_Buttons[j*10 + i];

            hal_plot_led(TYPEPAD, j*10 + i, 0, 0, b);
        }
    }

	// store off the raw ADC frame pointer for later use
	g_ADC = adc_raw;
}
