/*
 * Gameplay state trace.  Format in TraceFormat.h.
 */

#ifndef _FS2_STATE_TRACE_H
#define _FS2_STATE_TRACE_H

#define TRACE_FULL_INTERVAL	60		// frames between full state records when not tracing every frame

void trace_open(char *filename, int full_every_frame);
void trace_frame(int frame);
void trace_close();

#endif
