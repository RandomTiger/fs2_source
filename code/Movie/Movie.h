/*
 * Cutscene playback.
 *
 * The original game played Interplay MVE files through a library that was not part of the
 * source release, so an .mve cannot be played.  movie_play() tries the .mve first, and when
 * that fails plays a Theora video with the same name and the extension .ogv instead.
 *
 * See Movie.cpp.
 */

#ifndef _MOVIE_H
#define _MOVIE_H

// Plays a movie, for example "intro.mve".  Returns 1 if it was played, 0 if not.
// The second argument is in the original calls; what it meant is not known, and it is unused.
int movie_play(char *filename, int unused = 0);

// Plays two movies one after the other.  The second is skipped if the player stopped the first.
// Returns 1 if either was played.
int movie_play_two(char *filename1, char *filename2, int unused = 0);

#endif
