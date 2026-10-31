/*
 * Microphone capture for voice control: DirectSoundCapture (dscap) at 16 kHz, 16-bit mono,
 * while push-to-talk is held.  Never used in replay.  See docs/VOICE_CONTROL.md.
 */

#ifndef _FS2_VOICECAPTURE_H
#define _FS2_VOICECAPTURE_H

int  voice_capture_start();					// 1 if recording started
int  voice_capture_poll(short **samples);	// each frame while recording: the samples since the last call, *samples valid until the next call
int  voice_capture_stop(short **samples);	// stops; returns the last samples, as voice_capture_poll
void voice_capture_close();

#endif
