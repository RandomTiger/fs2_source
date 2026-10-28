// HostHooks.cpp
// Claude: the platform seam (D-051), see HostHooks.h.

#include "HostHooks.h"
#include "../Pad/PadXInput.h"

host_hooks Host_hooks = host_hooks_windows;

void host_hooks_set(const host_hooks *h)
{
	if ( (h->time_counter == NULL) || (h->time_frequency == NULL) || (h->poll_events == NULL) || (h->is_foreground == NULL)
		|| (h->mouse_get_pos == NULL) || (h->mouse_set_pos == NULL)
		|| (h->joy_count == NULL) || (h->joy_caps == NULL) || (h->joy_read == NULL) || (h->pad_poll == NULL) ) {
		Error( LOCATION, "host_hooks_set: the host left an entry empty" );
	}
	Host_hooks = *h;
}

// Claude: the name the pad game code (PadGame.cpp) has always called; the read now goes through the host
int pad_backend_poll(pad_state *out)
{
	return Host_hooks.pad_poll(out);
}
