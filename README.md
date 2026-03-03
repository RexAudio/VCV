# FM Tool

## Overview
FM Tool allows you to do DX-style FM with any audio inputs. 
Connect 2 sources to the IN and MOD IN ports, turn up the FM depth, and connect the OUT port.

![FMtool Module Mockup](images/FMtool.svg)

## FM Depth
Controls the amount of FM applied. Higher FM depth introduces more high frequency harmonics.

## IN
The carrier or main audio input. 

## MOD IN
The modulator input. The carrier will be modulated by this input at an amount set by the FM depth knob.

## Depth CV
The Depth CV in port allows you to connect modulation sources such as LFOs and ADSRs to dynamically change the FM depth.
The Depth CV knob controls the amount of this modulation of FM depth.

# Out
An audio output

## Notes
This DX-style FM is actually phase modulation (PM) using short modulated delays
but it is often called FM (frequency modulation) as it produces the same harmonics.
Using a carrier and modulator at the same frequency or harmonically-related (in-tune) frequencies produces a harmonic spectrum. 
Carriers and modulators at inharmonic (out-of-tune) frequencies relative to each other produces a dissonant spectrum.
