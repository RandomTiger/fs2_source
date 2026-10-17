// ObjectSnapshot.cpp
// Claude: a read-only copy of every object after each simulation step, for a host to draw from (D-050, item 3).

#include "pstypes.h"
#include "LinkList.h"
#include "Object.h"
#include "Ship.h"
#include "Weapon.h"
#include "Debris.h"
#include "Asteroid.h"
#include "ObjectSnapshot.h"

static object_snapshot Object_snapshot[MAX_OBJECTS];
static int Object_snapshot_count = 0;

// the model a host would draw for the object, or -1
static int object_snapshot_model(object *objp)
{
	switch (objp->type) {
	case OBJ_SHIP:
		return Ships[objp->instance].modelnum;
	case OBJ_WEAPON:
		return Weapon_info[Weapons[objp->instance].weapon_info_index].model_num;
	case OBJ_DEBRIS:
		return Debris[objp->instance].model_num;
	case OBJ_ASTEROID: {
		asteroid *asp = &Asteroids[objp->instance];
		return Asteroid_info[asp->type].model_num[asp->asteroid_subtype];
	}
	default:
		return -1;
	}
}

void object_snapshot_take()
{
	object *objp;

	Object_snapshot_count = 0;
	for ( objp = GET_FIRST(&obj_used_list); objp != END_OF_LIST(&obj_used_list); objp = GET_NEXT(objp) ) {
		if ( objp->type == OBJ_NONE ) {
			continue;
		}
		Assert( Object_snapshot_count < MAX_OBJECTS );
		object_snapshot *s = &Object_snapshot[Object_snapshot_count++];
		s->objnum = OBJ_INDEX(objp);
		s->signature = objp->signature;
		s->type = objp->type;
		s->instance = objp->instance;
		s->flags = objp->flags;
		s->model_num = object_snapshot_model(objp);
		s->pos = objp->pos;
		s->orient = objp->orient;
	}
}

int object_snapshot_count()
{
	return Object_snapshot_count;
}

const object_snapshot *object_snapshot_get(int n)
{
	Assert( (n >= 0) && (n < Object_snapshot_count) );
	return &Object_snapshot[n];
}
