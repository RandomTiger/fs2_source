// ObjectSnapshot.h
// Claude: a read-only copy of every object after each simulation step, for a host to draw from (D-050, item 3).
// Taking it reads game state and changes none of it.

#ifndef _OBJECT_SNAPSHOT_H
#define _OBJECT_SNAPSHOT_H

#include "vecmat.h"

typedef struct object_snapshot {
	int		objnum;			// index into Objects
	int		signature;		// Objects[objnum].signature, unique for the life of the game
	int		type;				// OBJ_SHIP, OBJ_WEAPON, ...
	int		instance;		// index into the type's own array (Ships, Weapons, ...)
	uint		flags;			// Objects[objnum].flags (OF_SHOULD_BE_DEAD, OF_RENDERS, ...)
	int		model_num;		// the model drawn for it, or -1 if it has none (lasers, fireballs, ...)
	vector	pos;
	matrix	orient;
} object_snapshot;

// Copy every object in use, in the order of the used list. game_frame calls it after game_simulation_frame.
void object_snapshot_take();

// The copy from the last object_snapshot_take: the number of objects, and each one.
int object_snapshot_count();
const object_snapshot *object_snapshot_get(int n);

#endif
