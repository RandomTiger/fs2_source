/*
 * Speech recogniser behind a small interface, so another engine (PocketSphinx, sherpa-onnx)
 * can replace it.  This one is Vosk, loaded at run time with LoadLibrary: with no DLL or no
 * model the open fails, voice stays off and nothing else changes.  No FreeSpace types, so
 * the dispatch test links it.  See docs/VOICE_CONTROL.md.
 *
 * The model loads, and speech is recognised, on a worker thread.  While push-to-talk is held
 * the audio streams to it, and the pause after each phrase ends that phrase, so a target and
 * then an order can be said in one press.
 */

#ifndef _FS2_VOICEENGINE_H
#define _FS2_VOICEENGINE_H

#define VOICE_ENGINE_OFF		0		// not opened
#define VOICE_ENGINE_LOADING	1		// DLL loaded, model loading
#define VOICE_ENGINE_READY		2
#define VOICE_ENGINE_FAILED	3		// see voice_engine_error()

#define VOICE_ENGINE_RATE		16000	// samples per second, 16-bit mono
#define VOICE_RESULT_LEN		4096

// dll_dir: folder holding libvosk.dll and its runtime DLLs, or "" for the exe's folder and
// the DLL search path.  model_dir: the model folder.  Returns 1 if the DLL loaded and the
// model has started loading, 0 on failure.
int  voice_engine_open(const char *dll_dir, const char *model_dir);
int  voice_engine_state();
const char *voice_engine_error();

// A stream is one push-to-talk press.  It may hold several phrases, each ended by a pause.
// Start one with the phrases (one per line) its first phrase is matched against.  Returns 0
// if the engine is not ready or the last stream has not given its final result.  Copies them.
int  voice_engine_stream_start(const char *phrases);

// audio while push-to-talk is held.  Copies the samples.
void voice_engine_stream_feed(const short *samples, int count);

// push-to-talk released: what is left is recognised and the final result follows
void voice_engine_stream_end();

// 1 when a phrase has a result: json holds it ("" if recognition failed).  *final is 1 for the
// stream's last result, after which the engine is free for the next stream.  After any other
// result the engine keeps the audio that follows but waits for voice_engine_stream_next to give
// the phrases for the next phrase.
int  voice_engine_stream_result(char *json, int max, int *final);
void voice_engine_stream_next(const char *phrases);

void voice_engine_close();

#endif
