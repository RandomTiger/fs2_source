/* tex_convert.h  (R1-2)
 * Pixel conversion for texture upload, host side (gr_host): whatever BmpMan hands over becomes what the render sink takes,
 * RGBA8 or A8 (render_sink.h). The sink never sees 16 bit or palettes, so the 32 bit default (D-20261027-192331) and the
 * 16 bit option (ColourDepth=16, -force_16bit) differ only in the layout passed here:
 *   32 bit: ARGB 8888, the format BmpMan unpacks at texture_bpp 32.
 *   16 bit: ARGB 1555 (or 4444, any layout described by masks); a 1 bit alpha is 0 or 255.
 * Plain C++17, no game type, no Godot type; header only so sink_check tests it. The layouts are masks like the game's
 * color_gun (Gr_t_red ...), so what the host's init tells BmpMan and what it converts with are one description.
 */
#ifndef FS2_TEX_CONVERT_H
#define FS2_TEX_CONVERT_H

#include <cstdint>
#include <cstring>

namespace fs2host {

struct PixLayout {
	uint32_t rmask, gmask, bmask, amask;	// amask 0 = no alpha channel, opaque
	int bytes;				// 2 or 4 bytes per pixel
};

static const PixLayout LAYOUT_ARGB8888 = { 0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u, 4 };
static const PixLayout LAYOUT_ARGB1555 = { 0x7C00u, 0x03E0u, 0x001Fu, 0x8000u, 2 };
static const PixLayout LAYOUT_ARGB4444 = { 0x0F00u, 0x00F0u, 0x000Fu, 0xF000u, 2 };
static const PixLayout LAYOUT_RGB565   = { 0xF800u, 0x07E0u, 0x001Fu, 0x0000u, 2 };

// the layout BmpMan unpacks at a given texture_bpp (32 default, 16 option)
inline const PixLayout &layout_for_texture_bpp(int texture_bpp)
{
	return texture_bpp == 16 ? LAYOUT_ARGB1555 : LAYOUT_ARGB8888;
}

// one channel to 8 bits by bit replication, so full scale is exactly 255 (0x1F -> 0xFF, 1 -> 0xFF, 0xF -> 0xFF)
inline uint8_t expand_channel(uint32_t value, int bits)
{
	if (bits <= 0) { return 255; }
	if (bits >= 8) { return (uint8_t)(value >> (bits - 8)); }
	uint32_t out = value;
	int have = bits;
	while (have < 8) {
		out = (out << bits) | value;
		have += bits;
	}
	return (uint8_t)(out >> (have - 8));
}

struct ChannelShift {
	int shift, bits;
};

inline ChannelShift channel_of(uint32_t mask)
{
	ChannelShift c = { 0, 0 };
	if (mask == 0) { return c; }
	while (!((mask >> c.shift) & 1u)) { c.shift++; }
	while ((mask >> (c.shift + c.bits)) & 1u) { c.bits++; }
	return c;
}

// Converts w*h pixels of layout `l` (rows `src_pitch` bytes apart; 0 = tight) to tightly packed RGBA8 (r g b a in memory order).
inline void to_rgba8(const uint8_t *src, int w, int h, int src_pitch, const PixLayout &l, uint8_t *dst)
{
	const ChannelShift cr = channel_of(l.rmask), cg = channel_of(l.gmask), cb = channel_of(l.bmask), ca = channel_of(l.amask);
	if (src_pitch <= 0) { src_pitch = w * l.bytes; }
	for (int y = 0; y < h; y++) {
		const uint8_t *row = src + (size_t)y * (size_t)src_pitch;
		uint8_t *out = dst + (size_t)y * (size_t)w * 4u;
		for (int x = 0; x < w; x++) {
			uint32_t p;
			if (l.bytes == 4) {
				uint32_t t;
				std::memcpy(&t, row + (size_t)x * 4u, 4);
				p = t;
			} else {
				uint16_t t;
				std::memcpy(&t, row + (size_t)x * 2u, 2);
				p = t;
			}
			out[0] = expand_channel((p & l.rmask) >> cr.shift, cr.bits);
			out[1] = expand_channel((p & l.gmask) >> cg.shift, cg.bits);
			out[2] = expand_channel((p & l.bmask) >> cb.shift, cb.bits);
			out[3] = l.amask ? expand_channel((p & l.amask) >> ca.shift, ca.bits) : 255;
			out += 4;
		}
	}
}

// 8 bit coverage (AABITMAP: font atlas, aabitmap) to tightly packed A8: a copy that drops the row padding.
inline void to_a8(const uint8_t *src, int w, int h, int src_pitch, uint8_t *dst)
{
	if (src_pitch <= 0) { src_pitch = w; }
	for (int y = 0; y < h; y++) {
		std::memcpy(dst + (size_t)y * (size_t)w, src + (size_t)y * (size_t)src_pitch, (size_t)w);
	}
}

// bytes the sink must be given for a w*h texture
inline size_t rgba8_bytes(int w, int h) { return (size_t)w * (size_t)h * 4u; }
inline size_t a8_bytes(int w, int h) { return (size_t)w * (size_t)h; }

} // namespace fs2host

#endif
