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
#define ARRANGERSCREEN 1
#define NOTESCREEN 2
#define TEMPODOWN 10
#define TEMPOUP 20
#define RELEASEDOWN 30
#define RELEASEUP 40
#define REMOVEPHRASE 60
#define APPENDPHRASE 70
#define MUTECHANNEL 80
#define PAGEUP 91
#define PAGEDOWN 92
#define PAGELEFT 93
#define PAGERIGHT 94
#define DRUMCHANNEL 95
#define BASSCHANNEL 96
#define HARMONYCHANNEL 97
#define MELODYCHANNEL 98
#define MUTEVOICE0 19
#define MUTEVOICE1 29
#define MUTEVOICE2 39
#define MUTEVOICE3 49
#define MUTEVOICE4 59
#define MUTEVOICE5 69
#define MUTEVOICE6 79
#define MUTEVOICE7 89

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
#define INSTRUMENT3 3 // index into Instruments[]

u8 g_Mode = ARRANGERSCREEN;
u8 g_CurrentInstrument = INSTRUMENT0;

struct Instrument{
	u32 steps[STEPS * PHRASES];
	u8 sequence[PHRASES]; // Array representing the order of phrases to be played
	u8 phrase; // Position through sequence of phrases - index into sequence
	u8 phraseView; // Current screen we are editing (1, 32)
	u8 pitchOffset;
	u8 sustainStates[RANGE]; // Stores number of semiquavers left of each note duration
	u8 sustain; // note sustain
	u8 colour[3]; // LED colour
	u8 channelButton; // Midi number corresponding to button to light up
	u8 isMuted; // Channel mute
	u32 mutedVoices; // Bit flags for voices that are muted
};

struct Instrument Instruments[4];


void MakeInstruments()
{
	u8 i;
	for(i = 0; i < NUMINSTRUMENTS; i++ )
	{
		Instruments[i].phrase = 0;
		Instruments[i].phraseView = 1;
		Instruments[i].sustain = 4;
		switch(i)
		{
			case INSTRUMENT0:
			{
				Instruments[i].colour[0] = 21;
				Instruments[i].colour[1] = 42;
				Instruments[i].colour[2] = 63;
				Instruments[i].channelButton = DRUMCHANNEL;
			}
			break;
			case INSTRUMENT1:
			{
				Instruments[i].colour[0] = 63;
				Instruments[i].colour[1] = 21;
				Instruments[i].colour[2] = 42;
				Instruments[i].channelButton = BASSCHANNEL;
			}
			break;
			case INSTRUMENT2:
			{
				Instruments[i].colour[0] = 21;
				Instruments[i].colour[1] = 63;
				Instruments[i].colour[2] = 42;
				Instruments[i].channelButton = HARMONYCHANNEL;
			}
			break;
			case INSTRUMENT3:
			{
				Instruments[i].colour[0] = 42;
				Instruments[i].colour[1] = 21;
				Instruments[i].colour[2] = 63;
				Instruments[i].channelButton = MELODYCHANNEL;
			}
			break;
		}

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

    for (u8 note = 0; note < RANGE; note++)
    {
		// Send note off Messages
		if(instrument->sustainStates[note] == 1)
		{
			hal_send_midi(DINMIDI, NOTEOFF | channel, note + LOWESTNOTE, 0);
			hal_send_midi(USBSTANDALONE, NOTEOFF | channel, note + LOWESTNOTE, 0);
		}
		if(instrument->sustainStates[note] > 0)
		{
			instrument->sustainStates[note] -= 1;
		}

		if(!instrument->isMuted)
		{
			if(IsNoteOn(instrument->mutedVoices, note))
			{
				continue;
			}
	        if (IsNoteOn(flags, note))
	        {
				hal_send_midi(DINMIDI, NOTEON | channel, note + LOWESTNOTE, 127);
				hal_send_midi(USBSTANDALONE, NOTEON | channel, note + LOWESTNOTE, 127);

				instrument->sustainStates[note] = instrument->sustain;
	        }
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
	for (int i = 0; i <= 90; i += 10)
	{
		for (int j = 0; j < 10; j++)
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
				hal_plot_led(TYPEPAD, (10 * (bit - instrument->pitchOffset)) + (step - begin) + 11, instrument->colour[0], instrument->colour[1], instrument->colour[2]);
			}
		}
	}
}

int Pow(u32 base, u32 exponent)
{
	u32 result = 1;
	for(;;)
	{
		if (exponent & 1)
		{
			result *= base;
		}
		exponent >>= 1;
		if(!exponent)
		{
			break;
		}
		base *= base;
	}

	return result;
}

void PlotPlayhead(u8 step)
{
	// if (step == 0)
	// {
	// 	for (u8 i = 1; i < 9; i++)
	// 	{
	// 		hal_plot_led(TYPEPAD, 8 + (i * 10), 0, 0, 0);
	// 	}
	// }
	// else
	// {
	// 	for (u8 i = 1; i < 9; i++)
	// 	{
	// 		hal_plot_led(TYPEPAD, step + (i * 10), 0, 0, 0);
	// 	}
	// }

	for (u8 i = 1; i < 9; i++)
	{
		hal_plot_led(TYPEPAD, (step + 1) + (i * 10), MAXLED, MAXLED, MAXLED);
	}
}

void PlotButtons(struct Instrument *instrument, u8 isCurrent)
{
	if(isCurrent)
	{
		if(instrument->isMuted)
		{
			hal_plot_led(TYPEPAD, instrument->channelButton, MAXLED, 0, 0);
		}
		else
		{
			hal_plot_led(TYPEPAD, instrument->channelButton, instrument->colour[0], instrument->colour[1], instrument->colour[2]);
		}

		hal_plot_led(TYPEPAD, ARRANGERSCREEN, instrument->colour[0], instrument->colour[1], instrument->colour[2]);
	}
	else
	{
		if(instrument->isMuted)
		{
			hal_plot_led(TYPEPAD, instrument->channelButton, MAXLED, 0, 0);
		}
		else
		{
			hal_plot_led(TYPEPAD, instrument->channelButton, instrument->colour[0] / 4, instrument->colour[1] / 4, instrument->colour[2] / 4);
		}
	}

	for(u8 i = 0; i < 8; i++)
	{
		if(IsNoteOn(instrument->mutedVoices, i + instrument->pitchOffset))
		{
			hal_plot_led(TYPEPAD, (10 * (i + 1)) + 9, MAXLED, 0, 0);
		}
	}

	// Plot individual staticly coloured buttons.
	hal_plot_led(TYPEPAD, PAGEUP, MAXLED, MAXLED, MAXLED);
	hal_plot_led(TYPEPAD, PAGEDOWN, MAXLED, MAXLED, MAXLED);
	hal_plot_led(TYPEPAD, PAGELEFT, MAXLED, MAXLED, MAXLED);
	hal_plot_led(TYPEPAD, PAGERIGHT, MAXLED, MAXLED, MAXLED);

	hal_plot_led(TYPEPAD, MUTECHANNEL, 20, 0, 0);
	hal_plot_led(TYPEPAD, APPENDPHRASE, 0, 0, MAXLED);
	hal_plot_led(TYPEPAD, REMOVEPHRASE, 10, 0, MAXLED);

	hal_plot_led(TYPEPAD, RELEASEUP, 0, MAXLED, 10);
	hal_plot_led(TYPEPAD, RELEASEDOWN, 0, 10, 5);

	hal_plot_led(TYPEPAD, TEMPOUP, 0, MAXLED, 0);
	hal_plot_led(TYPEPAD, TEMPODOWN, 0, 10, 0);

	hal_plot_led(TYPEPAD, NOTESCREEN, MAXLED, MAXLED, MAXLED);
}


void PlotPhrases(struct Instrument *instrument)
{
    for(u8 i = 0; i < PHRASES; i++)
    {
        if(i == instrument->phraseView - 1)
        {
            hal_plot_led(TYPEPAD, PHRASES_MAP[i], MAXLED, MAXLED, MAXLED);
			continue;
        }

        hal_plot_led(TYPEPAD, PHRASES_MAP[i], instrument->colour[0], instrument->colour[1], instrument->colour[2]);
    }
}

void PlotSequence(struct Instrument *instrument)
{
	static u8 flash = 0;

    flash = flash ^ 1;

	for(u8 i = 0; i < PHRASES; i++)
	{
		if(instrument->sequence[i] == 0)
		{
			break;
		}

		if(instrument->sequence[i] == instrument->phraseView)
		{
			if(i == instrument->phrase)
			{
				hal_plot_led(TYPEPAD, ARRANGER_MAP[i], MAXLED * flash, MAXLED * flash, MAXLED * flash);
			}
			else
			{
				hal_plot_led(TYPEPAD, ARRANGER_MAP[i], MAXLED, MAXLED, MAXLED);
			}
		}
		else
		{
			if(i == instrument->phrase)
			{
				hal_plot_led(TYPEPAD, ARRANGER_MAP[i], instrument->colour[0] * flash, instrument->colour[1] * flash, instrument->colour[2] * flash);
			}
			else
			{
				hal_plot_led(TYPEPAD, ARRANGER_MAP[i], instrument->colour[0], instrument->colour[1], instrument->colour[2]);
			}
		}
	}
}

//______________________________________________________________________________

void app_surface_event(u8 type, u8 index, u8 value)
{
	static u8 muteChannelHeld = 0;
	static u8 appendPhraseHeld = 0;
    switch (type)
    {
        case  TYPEPAD:
        {
			if (value)
			{
				switch (index)
				{
					case NOTESCREEN:
					{
						g_Mode = NOTESCREEN;
					}
					break;
					case ARRANGERSCREEN:
					{
						g_Mode = ARRANGERSCREEN;
					}
					break;
					case MUTECHANNEL:
					{
						muteChannelHeld = 1;
					}
					break;
					case APPENDPHRASE:
					{
						appendPhraseHeld = 1;
					}
					break;
					case REMOVEPHRASE:
					{
						for(u8 i = 1; i < PHRASES; i++)
						{
							if(Instruments[g_CurrentInstrument].sequence[i] == 0)
							{
								Instruments[g_CurrentInstrument].sequence[i - 1] = 0;
							}
						}
					}
					break;
					case DRUMCHANNEL:
					{
						if(muteChannelHeld)
						{
							Instruments[INSTRUMENT0].isMuted = Instruments[INSTRUMENT0].isMuted ^ 1;
						}
						else
						{
							g_CurrentInstrument = INSTRUMENT0;
						}
					}
					break;
					case BASSCHANNEL:
					{
						if(muteChannelHeld)
						{
							Instruments[INSTRUMENT1].isMuted = Instruments[INSTRUMENT1].isMuted ^ 1;
						}
						else
						{
							g_CurrentInstrument = INSTRUMENT1;
						}
					}
					break;
					case HARMONYCHANNEL:
					{
						if(muteChannelHeld)
						{
							Instruments[INSTRUMENT2].isMuted = Instruments[INSTRUMENT2].isMuted ^ 1;
						}
						else
						{
							g_CurrentInstrument = INSTRUMENT2;
						}
					}
					break;
					case MELODYCHANNEL:
					{
						if(muteChannelHeld)
						{
							Instruments[INSTRUMENT3].isMuted = Instruments[INSTRUMENT3].isMuted ^ 1;
						}
						else
						{
							g_CurrentInstrument = INSTRUMENT3;
						}
					}
					break;
					case RELEASEUP:
					{
						Instruments[g_CurrentInstrument].sustain += 1;
					}
					break;
					case RELEASEDOWN:
					{
						if(Instruments[g_CurrentInstrument].sustain > 1)
						{
							Instruments[g_CurrentInstrument].sustain -= 1;
						}
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
					case MUTEVOICE0:
					{
						Instruments[g_CurrentInstrument].mutedVoices = Instruments[g_CurrentInstrument].mutedVoices ^ Pow(2, Instruments[g_CurrentInstrument].pitchOffset + 0);
					}
					break;
					case MUTEVOICE1:
					{
						Instruments[g_CurrentInstrument].mutedVoices = Instruments[g_CurrentInstrument].mutedVoices ^ Pow(2, Instruments[g_CurrentInstrument].pitchOffset + 1);
					}
					break;
					case MUTEVOICE2:
					{
						Instruments[g_CurrentInstrument].mutedVoices = Instruments[g_CurrentInstrument].mutedVoices ^ Pow(2, Instruments[g_CurrentInstrument].pitchOffset + 2);
					}
					break;
					case MUTEVOICE3:
					{
						Instruments[g_CurrentInstrument].mutedVoices = Instruments[g_CurrentInstrument].mutedVoices ^ Pow(2, Instruments[g_CurrentInstrument].pitchOffset + 3);
					}
					break;
					case MUTEVOICE4:
					{
						Instruments[g_CurrentInstrument].mutedVoices = Instruments[g_CurrentInstrument].mutedVoices ^ Pow(2, Instruments[g_CurrentInstrument].pitchOffset + 4);
					}
					break;
					case MUTEVOICE5:
					{
						Instruments[g_CurrentInstrument].mutedVoices = Instruments[g_CurrentInstrument].mutedVoices ^ Pow(2, Instruments[g_CurrentInstrument].pitchOffset + 5);
					}
					break;
					case MUTEVOICE6:
					{
						Instruments[g_CurrentInstrument].mutedVoices = Instruments[g_CurrentInstrument].mutedVoices ^ Pow(2, Instruments[g_CurrentInstrument].pitchOffset + 6);
					}
					break;
					case MUTEVOICE7:
					{
						Instruments[g_CurrentInstrument].mutedVoices = Instruments[g_CurrentInstrument].mutedVoices ^ Pow(2, Instruments[g_CurrentInstrument].pitchOffset + 7);
					}
					break;
					default:
					{
						if(g_Mode == ARRANGERSCREEN)
						{
							if(appendPhraseHeld)
							{
								if(index < 49) // Check that the pad we hit is in the bottom half of the pads
								{
									for(u8 i = 0; i < PHRASES; i++)
									{
										if(Instruments[g_CurrentInstrument].sequence[i] == 0)
										{
											Instruments[g_CurrentInstrument].sequence[i] = PAD_TO_INDEX_MAP[index];
											break;
										}
									}
								}
							}
							else
							{
								if(index > 49)
								{
									if(Instruments[g_CurrentInstrument].sequence[PAD_TO_INDEX_MAP[index] - 1] != 0)
									{
										if(PAD_TO_INDEX_MAP[index] != 0)
										{
											Instruments[g_CurrentInstrument].phraseView = Instruments[g_CurrentInstrument].sequence[PAD_TO_INDEX_MAP[index] - 1];
										}
									}
								}
								else
								{
									Instruments[g_CurrentInstrument].phraseView = PAD_TO_INDEX_MAP[index];
								}
							}
						}
						if(g_Mode == NOTESCREEN)
						{
							SetFlag(Instruments[g_CurrentInstrument].steps, index, Instruments[g_CurrentInstrument].phraseView, Instruments[g_CurrentInstrument].pitchOffset);
							break;
						}
					}
					break;
				}
			}
			if(!value)
			{
				switch(index)
				{
					case MUTECHANNEL:
					{
						muteChannelHeld = 0;
					}
					case APPENDPHRASE:
					{
						appendPhraseHeld = 0;
					}
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

			PlotClear();

			for(i = 0; i < NUMINSTRUMENTS; i++)
			{
				TriggerNotes(&Instruments[i], step, i);
				PlotButtons(&Instruments[i], g_CurrentInstrument == i);
			}

			switch(g_Mode)
			{
				case ARRANGERSCREEN:
				{
					PlotPhrases(&Instruments[g_CurrentInstrument]);

					PlotSequence(&Instruments[g_CurrentInstrument]);
				}
				break;
				case NOTESCREEN:
				{
					if(Instruments[g_CurrentInstrument].sequence[Instruments[g_CurrentInstrument].phrase] == Instruments[g_CurrentInstrument].phraseView)
					{
						PlotPlayhead(step);
					}

					PlotNotes(&Instruments[g_CurrentInstrument]);
				}
				break;
			}

		    step++;
		}
    }
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
