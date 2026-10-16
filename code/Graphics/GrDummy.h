// Claude: the null renderer (-nographics): every drawing call does nothing, so the game runs with no
// graphics device.  After fs2_source_ue's GR_DUMMY.

#ifndef _GRDUMMY_H
#define _GRDUMMY_H

void gr_dummy_init();
void gr_dummy_cleanup();

#endif
