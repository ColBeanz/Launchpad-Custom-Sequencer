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
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURstepE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE stepSIBILITY OF SUCH DAMAGE.

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

// control buttons
#define TEMPOUP 20
#define TEMPODOWN 10
#define PAGEUP 91
#define PAGEDOWN 92
#define PAGELEFT 93
#define PAGERIGHT 94
#define DRUMCHANNEL 95
#define BASSCHANNEL 96
#define HARMONYCHANNEL 97
#define MELODYCHANNEL 98

static u8 g_tempo = 100;
static u8 g_ms_per_tick;

// instrument properties
#define LOWESTNOTE 36
#define RANGE 32
#define STEPS 8
#define PHRASES 32
#define NUMINSTRUMENTS 4
#define INSTRUMENT0 0
#define INSTRUMENT1 1
#define INSTRUMENT2 2
#define INSTRUMENT3 3

// view modes
#define ARRANGERSCREEN 0
#define NOTESCREEN 1

u8 g_Mode = NOTESCREEN;
u8 g_CurrentInstrument = INSTRUMENT0;

struct Instrument{
	u32 steps[STEPS * PHRASES];
	u8 sequence[PHRASES]; // Array representing the order of phrases to be played
	u8 phrase; // Position through sequence of phrases - index into sequence
	u8 phraseView; // Current screen we are editing (1, 32)
	u8 pitchOffset;
};

struct Instrument Instruments[4];


void MakeInstruments()
{
	u8 i;
	for(i = 0; i < NUMINSTRUMENTS; i++ )
	{
		Instruments[i].sequence[0] = 1;
		Instruments[i].sequence[1] = 2;
		Instruments[i].phrase = 0;
		Instruments[i].phraseView = 1;
	}
}

int IsNoteOn(u32 flags, u8 bit)
{
    return ((flags & (1 << bit)) != 0);
}

void SetFlag(u32 steps[], u8 note, u8 phraseView, u8 pitchOffset)
{
	u8 index = (note % 10) + ((phraseView - 1) * STEPS) - 1;

	steps[index] = steps[index] ^ 1 << (((note / 10) - 1) + pitchOffset);
}

void IncrementSequence(struct Instrument *instrument)
{
	instrument->phrase++;
	if(instrument->sequence[instrument->phrase] == 0 || instrument->phrase == 32)
	{
		instrument->phrase = 0;
	}
}

void TriggerNotes(struct Instrument *instrument, u8 step, u8 channel)
{
	if(instrument->sequence[instrument->phrase] == 0)
	{
		return;
	}

	u8 index = instrument->sequence[instrument->phrase] - 1;
	u32 flags = instrument->steps[(index * STEPS) + step];

    for (u8 bitposition = 0; bitposition < RANGE; bitposition++)
    {
        if (IsNoteOn(flags, bitposition))
        {
			hal_send_midi(DINMIDI, NOTEON | channel, bitposition + LOWESTNOTE, 127);
			hal_send_midi(USBSTANDALONE, NOTEON | channel, bitposition + LOWESTNOTE, 127);
        }
    }
};

void CalculateMsPerClock(u8 tempo)
{
	g_tempo = tempo;
    g_ms_per_tick = MS_PER_MIN / (CLOCK_RATE * g_tempo);
}

void PlotClear()
{
	for (int i = 10; i <= 80; i += 10)
	{
		for (int j = 1; j <= RANGE; j++)
		{
			hal_plot_led(TYPEPAD, i+j, 0, 0, 0);
		}
	}
}

void PlotNotes(struct Instrument *instrument)
{
	u8 phrase = instrument->phraseView - 1;
	u8 begin = phrase * STEPS;
	u8 end = begin + STEPS;
	u8 bottom = instrument->pitchOffset;
	u8 top = bottom + STEPS;
	for (u8 step = begin; step < end; step ++)
	{
		for (u8 bit = bottom; bit < top; bit++)
		{
			if(IsNoteOn(instrument->steps[step], bit))
			{
				hal_plot_led(TYPEPAD, (10 * (bit - instrument->pitchOffset)) + (step - begin) + 11, MAXLED, 0, 0);
			}
		}
	}
}

void PlotPlayhead(u8 step)
{
	if (step == 0)
	{
		for (u8 i = 1; i < 9; i++)
		{
			hal_plot_led(TYPEPAD, 8 + (i * 10), 0, 0, 0);
		}
	}
	else
	{
		for (u8 i = 1; i < 9; i++)
		{
			hal_plot_led(TYPEPAD, step + (i * 10), 0, 0, 0);
		}
	}

	for (u8 i = 1; i < 9; i++)
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
			/*
			if (value)
            {
                g_Buttons[index] = MAXLED * !g_Buttons[index];
            }
			*/
			if (value)
			{
				switch (index)
				{
					case DRUMCHANNEL:
					{
						g_CurrentInstrument = INSTRUMENT0;
					}
					break;
					case BASSCHANNEL:
					{
						g_CurrentInstrument = INSTRUMENT1;
					}
					break;
					case HARMONYCHANNEL:
					{
						g_CurrentInstrument = INSTRUMENT2;
					}
					break;
					case MELODYCHANNEL:
					{
						g_CurrentInstrument = INSTRUMENT3;
					}
					break;
					case TEMPODOWN:
					{
						CalculateMsPerClock(g_tempo - 5);
					}
					break;
					case TEMPOUP:
					{
						CalculateMsPerClock(g_tempo + 5);
					}
					break;
					case PAGELEFT:
					{
						if(Instruments[g_CurrentInstrument].phraseView != 1)
						{
							Instruments[g_CurrentInstrument].phraseView -= 1;
						}
					}
					break;
					case PAGERIGHT:
					{
						if(Instruments[g_CurrentInstrument].phraseView != 32)
						{
							Instruments[g_CurrentInstrument].phraseView += 1;
						}
					}
					break;
					case PAGEUP:
					{
						if(Instruments[g_CurrentInstrument].pitchOffset < 24)
						{
							Instruments[g_CurrentInstrument].pitchOffset += 1;
						}
					}
					break;
					case PAGEDOWN:
					{
						if(Instruments[g_CurrentInstrument].pitchOffset != 0)
						{
							Instruments[g_CurrentInstrument].pitchOffset -= 1;
						}
					}
					break;
					default:
					{
						SetFlag(Instruments[g_CurrentInstrument].steps, index, Instruments[g_CurrentInstrument].phraseView, Instruments[g_CurrentInstrument].pitchOffset);
					}
					break;
				}
			}

			/*
            // example - light / extinguish pad LEDs
            hal_plot_led(TYPEPAD, index, 0, 0, g_Buttons[index]);

            // example - send MIDI
            hal_send_midi(DINMIDI, NOTEON | 0, index, value);
			hal_send_midi(USBSTANDALONE, NOTEONCHONE, index, value);
			*/

        }
        break;

        case TYPESETUP:
        {
            if (value)
            {
                // save button states to flash (reload them by power cycling the hardware!)
                //hal_write_flash(0, g_Buttons, BUTTON_COUNT);
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


void app_timer_event()
{
    static u8 ms = 0;
    static u8 semiquaverinterval = 0;
	static u8 step = 0;

    if (++ms >= g_ms_per_tick)
    {
        ms = 0;

        // send a clock pulse up the USB
        hal_send_midi(DINMIDI, MIDITIMINGCLOCK, 0, 0);

        if (++semiquaverinterval >= 6)
        {
            semiquaverinterval = 0;
			u8 i;

		    if (step >= STEPS)
		    {
				step = 0;
				for(i = 0; i < NUMINSTRUMENTS; i++)
				{
					IncrementSequence(&Instruments[i]);
				}
		    }

			for(i = 0; i < NUMINSTRUMENTS; i++)
			{
				TriggerNotes(&Instruments[i], step, i);
			}

			PlotClear();

			if(Instruments[g_CurrentInstrument].sequence[Instruments[g_CurrentInstrument].phrase] == Instruments[g_CurrentInstrument].phraseView)
			{
				PlotPlayhead(step);
			}

			PlotNotes(&Instruments[g_CurrentInstrument]);

		    step++;
		}

    }



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
    CalculateMsPerClock(g_tempo);
    // example - load button states from flash
    //hal_read_flash(0, g_Buttons, BUTTON_COUNT);

	MakeInstruments();
	hal_send_midi(DINMIDI, MIDISTART, 0, 0);
	// store off the raw ADC frame pointer for later use
	g_ADC = adc_raw;
}
