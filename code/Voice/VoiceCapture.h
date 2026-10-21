/*
 * Microphone capture for voice control: DirectSoundCapture (dscap) at 16 kHz, 16-bit mono,
 * while push-to-talk is held.  Never used in replay.  See docs/VOICE_CONTROL.md.
 */

#ifndef _FS2_VOICECAPTURE_H
#define _FS2_VOICECAPTURE_H

#define VOICE_CAPTURE_MAX_SECONDS	6		// longer speech is cut off

int  voice_capture_start();					// 1 if recording started
void voice_capture_poll();						// call each frame while recording
int  voice_capture_stop(short **samples);	// stops; returns the sample count, *samples valid until the next start
void voice_capture_close();

#endif
