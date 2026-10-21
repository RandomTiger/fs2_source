/*
 * Speech recogniser behind a small interface, so another engine (PocketSphinx, sherpa-onnx)
 * can replace it.  This one is Vosk, loaded at run time with LoadLibrary: with no DLL or no
 * model the open fails, voice stays off and nothing else changes.  No FreeSpace types, so
 * the dispatch test links it.  See docs/VOICE_CONTROL.md.
 *
 * The model loads, and each utterance is recognised, on a worker thread.
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

// Recognise one utterance against phrases (one per line).  Copies both.  Returns 1 if the job
// started, 0 if the engine is not ready or still busy.
int  voice_engine_recognize(const char *phrases, const short *samples, int count);

// 1 when the last job has finished: json holds the result ("" if recognition failed) and the
// engine is free again.  0 while it runs, or when no job is waiting.
int  voice_engine_result(char *json, int max);

void voice_engine_close();

#endif
