/*
 * Microphone capture for voice control.  See VoiceCapture.h.
 */

#include "pstypes.h"
#include "dscap.h"

#include "VoiceCapture.h"
#include "VoiceEngine.h"

#define VOICE_CAPTURE_BUFFER_SECONDS	2		// dscap's looping buffer; polled every frame
#define VOICE_CAPTURE_MAX_SAMPLES		(VOICE_ENGINE_RATE * VOICE_CAPTURE_MAX_SECONDS)

static int Voice_capture_buffer = 0;			// dscap buffer created
static int Voice_capture_recording = 0;
static short Voice_capture_samples[VOICE_CAPTURE_MAX_SAMPLES];
static int Voice_capture_count = 0;
static unsigned char *Voice_capture_raw = NULL;
static int Voice_capture_raw_size = 0;

int voice_capture_start()
{
	if (Voice_capture_recording) {
		return 1;
	}

	if (!Voice_capture_buffer) {
		if (!dscap_supported()) {
			return 0;
		}
		if (dscap_create_buffer(VOICE_ENGINE_RATE, 16, 1, VOICE_CAPTURE_BUFFER_SECONDS) != 0) {
			return 0;
		}
		Voice_capture_raw_size = dscap_max_buffersize();
		if (Voice_capture_raw_size <= 0) {
			dscap_release_buffer();
			return 0;
		}
		Voice_capture_raw = (unsigned char *)malloc(Voice_capture_raw_size);
		if (!Voice_capture_raw) {
			dscap_release_buffer();
			return 0;
		}
		Voice_capture_buffer = 1;
	}

	Voice_capture_count = 0;
	if (dscap_start_record() != 0) {
		return 0;
	}
	Voice_capture_recording = 1;
	return 1;
}

void voice_capture_poll()
{
	int bytes, n;

	if (!Voice_capture_recording) {
		return;
	}

	bytes = dscap_get_raw_data(Voice_capture_raw, (unsigned int)Voice_capture_raw_size);
	if (bytes <= 0) {
		return;
	}

	n = bytes / (int)sizeof(short);
	if (n > VOICE_CAPTURE_MAX_SAMPLES - Voice_capture_count) {
		n = VOICE_CAPTURE_MAX_SAMPLES - Voice_capture_count;
	}
	if (n > 0) {
		memcpy(Voice_capture_samples + Voice_capture_count, Voice_capture_raw, n * sizeof(short));
		Voice_capture_count += n;
	}
}

int voice_capture_stop(short **samples)
{
	*samples = Voice_capture_samples;

	if (!Voice_capture_recording) {
		return 0;
	}

	voice_capture_poll();
	dscap_stop_record();
	Voice_capture_recording = 0;
	return Voice_capture_count;
}

void voice_capture_close()
{
	if (Voice_capture_recording) {
		dscap_stop_record();
		Voice_capture_recording = 0;
	}
	if (Voice_capture_buffer) {
		dscap_release_buffer();
		Voice_capture_buffer = 0;
	}
	if (Voice_capture_raw) {
		free(Voice_capture_raw);
		Voice_capture_raw = NULL;
	}
	Voice_capture_raw_size = 0;
	Voice_capture_count = 0;
}
