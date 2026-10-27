/* host_stub.h  (R1-4 draft)
 * Local stand-in for what P7-2/P7-3 put in code/Graphics/2d.h, so gr_host can be read and checked before the P7 branch is
 * merged into this one. It is only used when 2d.h does not already define GR_HOST. The shapes mirror P7's 2d.h at
 * fs2_source_P7 (GR_HOST 106, gr_host_backend, gr_host_backend_register); when P7 lands this file is deleted.
 */
#ifndef FS2_HOST_STUB_H
#define FS2_HOST_STUB_H

#ifndef GR_HOST
#define GR_HOST (106)

typedef struct gr_host_backend {
	void (*init)();
	void (*cleanup)();
	void (*force_windowed)();
	void (*activate)(int active);
	void (*bitmap)(int x, int y);
	void (*bitmap_ex)(int x, int y, int w, int h, int sx, int sy);
	int no_device;
} gr_host_backend;
extern void gr_host_backend_register(const gr_host_backend *backend);
#endif

#endif
