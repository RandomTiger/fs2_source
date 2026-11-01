/*
 * Vosk recogniser, loaded at run time.  See VoiceEngine.h.
 *
 * The Vosk C API is called through our own typedefs, so there is no link dependency and no
 * Vosk header.  Each phrase gets a new recogniser with that step's grammar, which avoids
 * vosk_recognizer_set_grm (not in every release).  Grammar phrases with a word the model does
 * not know are left out, since Vosk would otherwise drop the word and match the rest.
 *
 * Vosk's own endpointing ends a phrase: vosk_recognizer_accept_waveform returns 1 after about
 * half a second of silence that follows speech.  The worker then stops reading the audio queue
 * until the game, having acted on the phrase, gives the next step's grammar.  Audio is fed in
 * chunks of a tenth of a second, so at most that much after the pause goes to the old
 * recogniser.
 */

#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "VoiceEngine.h"

typedef struct VoskModel VoskModel;
typedef struct VoskRecognizer VoskRecognizer;

typedef VoskModel *			(__cdecl *vosk_model_new_t)(const char *model_path);
typedef void					(__cdecl *vosk_model_free_t)(VoskModel *model);
typedef int						(__cdecl *vosk_model_find_word_t)(VoskModel *model, const char *word);
typedef VoskRecognizer *	(__cdecl *vosk_recognizer_new_grm_t)(VoskModel *model, float sample_rate, const char *grammar);
typedef void					(__cdecl *vosk_recognizer_set_words_t)(VoskRecognizer *recognizer, int words);
typedef int						(__cdecl *vosk_recognizer_accept_waveform_s_t)(VoskRecognizer *recognizer, const short *data, int length);
typedef const char *			(__cdecl *vosk_recognizer_result_t)(VoskRecognizer *recognizer);
typedef const char *			(__cdecl *vosk_recognizer_final_result_t)(VoskRecognizer *recognizer);
typedef void					(__cdecl *vosk_recognizer_free_t)(VoskRecognizer *recognizer);
typedef void					(__cdecl *vosk_set_log_level_t)(int log_level);

static HMODULE Vosk_dll = NULL;
static vosk_model_new_t							p_vosk_model_new;
static vosk_model_free_t						p_vosk_model_free;
static vosk_model_find_word_t					p_vosk_model_find_word;
static vosk_recognizer_new_grm_t				p_vosk_recognizer_new_grm;
static vosk_recognizer_set_words_t			p_vosk_recognizer_set_words;
static vosk_recognizer_accept_waveform_s_t	p_vosk_recognizer_accept_waveform_s;
static vosk_recognizer_result_t				p_vosk_recognizer_result;
static vosk_recognizer_final_result_t		p_vosk_recognizer_final_result;
static vosk_recognizer_free_t					p_vosk_recognizer_free;
static vosk_set_log_level_t					p_vosk_set_log_level;

#define VOICE_PATH_LEN		MAX_PATH
#define VOICE_ERROR_LEN		256
#define VOICE_PHRASES_LEN	16384
#define VOICE_CHUNK			(VOICE_ENGINE_RATE / 10)	// samples fed to Vosk at a time
#define VOICE_QUEUE_MAX		(VOICE_ENGINE_RATE * 30)	// audio waiting beyond this is dropped

static volatile LONG Voice_engine_state = VOICE_ENGINE_OFF;
static char Voice_engine_err[VOICE_ERROR_LEN];
static char Voice_model_dir[VOICE_PATH_LEN];
static VoskModel *Voice_model = NULL;

static HANDLE Voice_thread = NULL;
static HANDLE Voice_job_event = NULL;
static volatile LONG Voice_quit = 0;

// game thread only
static int Voice_stream_open = 0;			// started, final result not yet taken

// shared with the worker, under Voice_lock
static CRITICAL_SECTION Voice_lock;
static int Voice_lock_made = 0;
static int Voice_streaming = 0;				// worker has a stream to recognise
static int Voice_stream_ended = 0;			// no more audio is coming
static int Voice_phrases_new = 0;			// Voice_job_phrases not yet taken by the worker
static int Voice_paused = 0;					// a phrase result was given; waiting for the next phrases
static int Voice_result_ready = 0;
static int Voice_result_final = 0;
static char Voice_job_phrases[VOICE_PHRASES_LEN];
static char Voice_job_result[VOICE_RESULT_LEN];
static short *Voice_queue = NULL;			// audio not yet fed: Voice_queue_len samples from Voice_queue_pos
static int Voice_queue_pos = 0;
static int Voice_queue_len = 0;

// worker only
static VoskRecognizer *Voice_rec = NULL;

static void voice_engine_fail(const char *fmt, const char *arg)
{
	_snprintf(Voice_engine_err, VOICE_ERROR_LEN - 1, fmt, arg);
	Voice_engine_err[VOICE_ERROR_LEN - 1] = 0;
	InterlockedExchange(&Voice_engine_state, VOICE_ENGINE_FAILED);
}

// Latin-1 -> UTF-8 (Vosk models and grammars are UTF-8)
static void voice_latin1_to_utf8(const char *in, char *out, int max)
{
	int len = 0;

	while (*in && (len < max - 2)) {
		unsigned char c = (unsigned char)*in++;

		if (c < 0x80) {
			out[len++] = (char)c;
		} else {
			out[len++] = (char)(0xC0 | (c >> 6));
			out[len++] = (char)(0x80 | (c & 0x3F));
		}
	}
	out[len] = 0;
}

// UTF-8 -> Latin-1 in place; anything outside Latin-1 becomes '?'
static void voice_utf8_to_latin1(char *s)
{
	unsigned char *in = (unsigned char *)s;
	char *out = s;

	while (*in) {
		unsigned char c = *in;

		if (c < 0x80) {
			*out++ = (char)c;
			in++;
		} else if (((c & 0xE0) == 0xC0) && ((in[1] & 0xC0) == 0x80)) {
			int cp = ((c & 0x1F) << 6) | (in[1] & 0x3F);

			*out++ = (cp < 0x100) ? (char)cp : '?';
			in += 2;
		} else {
			// longer sequence, or a stray byte: skip its continuation bytes
			*out++ = '?';
			in++;
			while ((*in & 0xC0) == 0x80) {
				in++;
			}
		}
	}
	*out = 0;
}

// 1 if the model knows every word of the UTF-8 phrase
static int voice_phrase_known(const char *phrase)
{
	char word[128];
	const char *p = phrase;

	while (*p) {
		int len = 0;

		while (*p == ' ') {
			p++;
		}
		while (*p && (*p != ' ')) {
			if (len < (int)sizeof(word) - 1) {
				word[len++] = *p;
			}
			p++;
		}
		word[len] = 0;
		if (len && (p_vosk_model_find_word(Voice_model, word) < 0)) {
			return 0;
		}
	}
	return 1;
}

// newline-separated Latin-1 phrases -> JSON array of the known ones, plus "[unk]"
static int voice_build_grammar(const char *phrases, char *out, int max)
{
	char line[256];
	char utf8[512];
	int len = 0, num = 0, n;
	const char *p = phrases;

	out[len++] = '[';
	while (*p) {
		const char *e = strchr(p, '\n');
		int llen = e ? (int)(e - p) : (int)strlen(p);

		if (llen > (int)sizeof(line) - 1) {
			llen = (int)sizeof(line) - 1;
		}
		memcpy(line, p, llen);
		line[llen] = 0;
		p = e ? (e + 1) : (p + strlen(p));

		if (!line[0] || strchr(line, '"') || strchr(line, '\\')) {
			continue;
		}
		voice_latin1_to_utf8(line, utf8, (int)sizeof(utf8));
		if (!voice_phrase_known(utf8)) {
			continue;
		}
		n = (int)strlen(utf8);
		if (len + n + 16 >= max) {
			break;
		}
		len += sprintf(out + len, "\"%s\",", utf8);
		num++;
	}
	len += sprintf(out + len, "\"[unk]\"]");
	return num;
}

static void voice_engine_free_rec()
{
	if (Voice_rec) {
		p_vosk_recognizer_free(Voice_rec);
		Voice_rec = NULL;
	}
}

// hand a result to the game.  After the final one the stream is over; after any other the
// worker waits for the next phrases.
static void voice_engine_post(const char *res, int final)
{
	EnterCriticalSection(&Voice_lock);
	Voice_job_result[0] = 0;
	if (res) {
		strncpy(Voice_job_result, res, VOICE_RESULT_LEN - 1);
		Voice_job_result[VOICE_RESULT_LEN - 1] = 0;
		voice_utf8_to_latin1(Voice_job_result);
	}
	Voice_result_final = final;
	Voice_result_ready = 1;
	if (final) {
		Voice_streaming = 0;
	} else {
		Voice_paused = 1;
	}
	LeaveCriticalSection(&Voice_lock);
}

// feed the queued audio to the recogniser until it runs out, a phrase ends or the stream does
static void voice_engine_run_stream()
{
	static char phrases[VOICE_PHRASES_LEN];
	static char grammar[VOICE_PHRASES_LEN * 2 + 64];
	static short chunk[VOICE_CHUNK];
	int n, last, new_phrases;

	for (;;) {
		if (Voice_quit) {
			return;
		}

		EnterCriticalSection(&Voice_lock);
		new_phrases = Voice_phrases_new;
		if (new_phrases) {
			strcpy(phrases, Voice_job_phrases);
			Voice_phrases_new = 0;
			Voice_paused = 0;
		}
		if (!Voice_streaming || Voice_paused) {
			LeaveCriticalSection(&Voice_lock);
			return;
		}
		n = (Voice_queue_len < VOICE_CHUNK) ? Voice_queue_len : VOICE_CHUNK;
		memcpy(chunk, Voice_queue + Voice_queue_pos, n * sizeof(short));
		Voice_queue_pos += n;
		Voice_queue_len -= n;
		last = Voice_stream_ended && (Voice_queue_len == 0);
		LeaveCriticalSection(&Voice_lock);

		if (new_phrases) {
			voice_engine_free_rec();
			if (voice_build_grammar(phrases, grammar, (int)sizeof(grammar)) > 0) {
				Voice_rec = p_vosk_recognizer_new_grm(Voice_model, (float)VOICE_ENGINE_RATE, grammar);
				if (Voice_rec) {
					p_vosk_recognizer_set_words(Voice_rec, 1);
				}
			}
		}

		// with no recogniser (no phrase the model knows) the audio is dropped
		if ((n > 0) && Voice_rec && (p_vosk_recognizer_accept_waveform_s(Voice_rec, chunk, n) == 1)) {
			// a pause ended the phrase
			voice_engine_post(p_vosk_recognizer_result(Voice_rec), last);
			voice_engine_free_rec();
			if (last) {
				return;
			}
			continue;
		}

		if (last) {
			voice_engine_post(Voice_rec ? p_vosk_recognizer_final_result(Voice_rec) : NULL, 1);
			voice_engine_free_rec();
			return;
		}

		if (n == 0) {
			return;
		}
	}
}

static DWORD WINAPI voice_engine_thread(LPVOID)
{
	char path[VOICE_PATH_LEN * 2];

	voice_latin1_to_utf8(Voice_model_dir, path, (int)sizeof(path));
	Voice_model = p_vosk_model_new(path);
	if (!Voice_model) {
		voice_engine_fail("Vosk could not load the model in '%s'", Voice_model_dir);
		return 0;
	}
	InterlockedExchange(&Voice_engine_state, VOICE_ENGINE_READY);

	for (;;) {
		WaitForSingleObject(Voice_job_event, INFINITE);
		if (Voice_quit) {
			break;
		}
		voice_engine_run_stream();
	}

	voice_engine_free_rec();
	return 0;
}

int voice_engine_open(const char *dll_dir, const char *model_dir)
{
	char path[VOICE_PATH_LEN];
	DWORD attr;

	if (Voice_engine_state != VOICE_ENGINE_OFF) {
		return Voice_engine_state != VOICE_ENGINE_FAILED;
	}
	Voice_engine_err[0] = 0;

	if (!model_dir || !model_dir[0]) {
		voice_engine_fail("no model folder set%s", "");
		return 0;
	}
	attr = GetFileAttributesA(model_dir);
	if ((attr == INVALID_FILE_ATTRIBUTES) || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
		voice_engine_fail("model folder '%s' not found", model_dir);
		return 0;
	}
	strncpy(Voice_model_dir, model_dir, VOICE_PATH_LEN - 1);
	Voice_model_dir[VOICE_PATH_LEN - 1] = 0;

	// a full path lets Windows find the MinGW runtime DLLs beside it
	if (dll_dir && dll_dir[0]) {
		_snprintf(path, VOICE_PATH_LEN - 1, "%s\\libvosk.dll", dll_dir);
		path[VOICE_PATH_LEN - 1] = 0;
		Vosk_dll = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	} else {
		strcpy(path, "libvosk.dll");
		Vosk_dll = LoadLibraryA(path);
	}
	if (!Vosk_dll) {
		voice_engine_fail("could not load '%s'", path);
		return 0;
	}

	p_vosk_model_new = (vosk_model_new_t)GetProcAddress(Vosk_dll, "vosk_model_new");
	p_vosk_model_free = (vosk_model_free_t)GetProcAddress(Vosk_dll, "vosk_model_free");
	p_vosk_model_find_word = (vosk_model_find_word_t)GetProcAddress(Vosk_dll, "vosk_model_find_word");
	p_vosk_recognizer_new_grm = (vosk_recognizer_new_grm_t)GetProcAddress(Vosk_dll, "vosk_recognizer_new_grm");
	p_vosk_recognizer_set_words = (vosk_recognizer_set_words_t)GetProcAddress(Vosk_dll, "vosk_recognizer_set_words");
	p_vosk_recognizer_accept_waveform_s = (vosk_recognizer_accept_waveform_s_t)GetProcAddress(Vosk_dll, "vosk_recognizer_accept_waveform_s");
	p_vosk_recognizer_result = (vosk_recognizer_result_t)GetProcAddress(Vosk_dll, "vosk_recognizer_result");
	p_vosk_recognizer_final_result = (vosk_recognizer_final_result_t)GetProcAddress(Vosk_dll, "vosk_recognizer_final_result");
	p_vosk_recognizer_free = (vosk_recognizer_free_t)GetProcAddress(Vosk_dll, "vosk_recognizer_free");
	p_vosk_set_log_level = (vosk_set_log_level_t)GetProcAddress(Vosk_dll, "vosk_set_log_level");

	if (!p_vosk_model_new || !p_vosk_model_free || !p_vosk_model_find_word || !p_vosk_recognizer_new_grm
		|| !p_vosk_recognizer_set_words || !p_vosk_recognizer_accept_waveform_s || !p_vosk_recognizer_result || !p_vosk_recognizer_final_result
		|| !p_vosk_recognizer_free || !p_vosk_set_log_level) {
		FreeLibrary(Vosk_dll);
		Vosk_dll = NULL;
		voice_engine_fail("'%s' is missing a Vosk function", path);
		return 0;
	}

	p_vosk_set_log_level(-1);

	Voice_quit = 0;
	Voice_stream_open = 0;
	Voice_streaming = 0;
	Voice_result_ready = 0;
	Voice_queue = (short *)malloc(VOICE_QUEUE_MAX * sizeof(short));
	Voice_job_event = Voice_queue ? CreateEventA(NULL, FALSE, FALSE, NULL) : NULL;
	if (Voice_job_event && !Voice_lock_made) {
		InitializeCriticalSection(&Voice_lock);
		Voice_lock_made = 1;
	}
	InterlockedExchange(&Voice_engine_state, VOICE_ENGINE_LOADING);
	Voice_thread = Voice_job_event ? CreateThread(NULL, 0, voice_engine_thread, NULL, 0, NULL) : NULL;
	if (!Voice_thread) {
		if (Voice_job_event) {
			CloseHandle(Voice_job_event);
			Voice_job_event = NULL;
		}
		if (Voice_queue) {
			free(Voice_queue);
			Voice_queue = NULL;
		}
		FreeLibrary(Vosk_dll);
		Vosk_dll = NULL;
		voice_engine_fail("could not start the recogniser thread%s", "");
		return 0;
	}

	return 1;
}

int voice_engine_state()
{
	return Voice_engine_state;
}

const char *voice_engine_error()
{
	return Voice_engine_err;
}

int voice_engine_stream_start(const char *phrases)
{
	if ((Voice_engine_state != VOICE_ENGINE_READY) || Voice_stream_open) {
		return 0;
	}

	EnterCriticalSection(&Voice_lock);
	strncpy(Voice_job_phrases, phrases, VOICE_PHRASES_LEN - 1);
	Voice_job_phrases[VOICE_PHRASES_LEN - 1] = 0;
	Voice_phrases_new = 1;
	Voice_paused = 0;
	Voice_stream_ended = 0;
	Voice_result_ready = 0;
	Voice_queue_pos = 0;
	Voice_queue_len = 0;
	Voice_streaming = 1;
	LeaveCriticalSection(&Voice_lock);

	Voice_stream_open = 1;
	SetEvent(Voice_job_event);
	return 1;
}

void voice_engine_stream_feed(const short *samples, int count)
{
	if (!Voice_stream_open || (count <= 0)) {
		return;
	}

	EnterCriticalSection(&Voice_lock);
	if (!Voice_stream_ended) {
		if (Voice_queue_pos + Voice_queue_len + count > VOICE_QUEUE_MAX) {
			memmove(Voice_queue, Voice_queue + Voice_queue_pos, Voice_queue_len * sizeof(short));
			Voice_queue_pos = 0;
		}
		if (count > VOICE_QUEUE_MAX - Voice_queue_len) {
			count = VOICE_QUEUE_MAX - Voice_queue_len;
		}
		memcpy(Voice_queue + Voice_queue_pos + Voice_queue_len, samples, count * sizeof(short));
		Voice_queue_len += count;
	}
	LeaveCriticalSection(&Voice_lock);

	SetEvent(Voice_job_event);
}

void voice_engine_stream_end()
{
	if (!Voice_stream_open) {
		return;
	}

	EnterCriticalSection(&Voice_lock);
	Voice_stream_ended = 1;
	LeaveCriticalSection(&Voice_lock);

	SetEvent(Voice_job_event);
}

int voice_engine_stream_result(char *json, int max, int *final)
{
	int ready;

	*final = 0;
	if (!Voice_stream_open) {
		return 0;
	}

	EnterCriticalSection(&Voice_lock);
	ready = Voice_result_ready;
	if (ready) {
		if (max > 0) {
			strncpy(json, Voice_job_result, max - 1);
			json[max - 1] = 0;
		}
		*final = Voice_result_final;
		Voice_result_ready = 0;
	}
	LeaveCriticalSection(&Voice_lock);

	if (ready && *final) {
		Voice_stream_open = 0;
	}
	return ready;
}

void voice_engine_stream_next(const char *phrases)
{
	if (!Voice_stream_open) {
		return;
	}

	EnterCriticalSection(&Voice_lock);
	strncpy(Voice_job_phrases, phrases, VOICE_PHRASES_LEN - 1);
	Voice_job_phrases[VOICE_PHRASES_LEN - 1] = 0;
	Voice_phrases_new = 1;
	LeaveCriticalSection(&Voice_lock);

	SetEvent(Voice_job_event);
}

void voice_engine_close()
{
	int stopped = 1;

	if (Voice_thread) {
		InterlockedExchange(&Voice_quit, 1);
		SetEvent(Voice_job_event);
		// a model still loading cannot be interrupted; give it time, then leave it
		if (WaitForSingleObject(Voice_thread, 10000) != WAIT_OBJECT_0) {
			stopped = 0;
		}
		CloseHandle(Voice_thread);
		Voice_thread = NULL;
	}

	if (!stopped) {
		return;
	}

	if (Voice_job_event) {
		CloseHandle(Voice_job_event);
		Voice_job_event = NULL;
	}
	if (Voice_model) {
		p_vosk_model_free(Voice_model);
		Voice_model = NULL;
	}
	if (Voice_queue) {
		free(Voice_queue);
		Voice_queue = NULL;
	}
	if (Voice_lock_made) {
		DeleteCriticalSection(&Voice_lock);
		Voice_lock_made = 0;
	}
	Voice_stream_open = 0;
	if (Vosk_dll) {
		FreeLibrary(Vosk_dll);
		Vosk_dll = NULL;
	}
	InterlockedExchange(&Voice_engine_state, VOICE_ENGINE_OFF);
}
