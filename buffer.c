
#include <wayland-client.h>

#include "utils.h"

#include "buffer.h"


#define FB_COORDS(name, tx, ty) \
	static void fb_coords_##name(uint32_t width, uint32_t height, \
								 uint32_t ox, uint32_t oy, \
								 uint32_t *fb_x, uint32_t *fb_y) \
	{ \
		*fb_x = (tx);\
		*fb_y = (ty); \
	}

FB_COORDS(normal, ox, height - oy - 1);
FB_COORDS(90, width - oy - 1, height - ox - 1);
FB_COORDS(180, ox, oy);
FB_COORDS(270, oy, ox);


void (*coords_fns[])(uint32_t width, uint32_t height,
					 uint32_t ox, uint32_t oy, uint32_t *fb_x, uint32_t *fb_y) = {
	[WL_OUTPUT_TRANSFORM_NORMAL] = fb_coords_normal,
	[WL_OUTPUT_TRANSFORM_90] = fb_coords_90,
	[WL_OUTPUT_TRANSFORM_180] = fb_coords_180,
	[WL_OUTPUT_TRANSFORM_270] = fb_coords_270,
};



void buffer_calculate_fb_coords(struct wvnc_output *output,
								uint32_t src_x, uint32_t src_y,
								uint32_t *fb_x, uint32_t *fb_y)
{
	coords_fns[output->transform](
		output->width, output->height,
		src_x, src_y,
		fb_x, fb_y
	);
}


// TODO: Deduplicate this with the above

#define FB_OFF(name, tx, ty) \
	static rgba_t *fb_off_##name(rgba_t *fb, uint32_t width, uint32_t height, uint32_t ox, uint32_t oy) \
	{ \
		return &fb[(ty) * width + (tx)];\
	}

FB_OFF(normal, ox, height - oy - 1);
FB_OFF(90, width - oy - 1, height - ox - 1);
FB_OFF(180, ox, oy);
FB_OFF(270, oy, ox);

#define COPY_TO_FB(name) \
static void copy_to_fb_##name(rgba_t *fb, struct wvnc_output *output, struct wvnc_buffer *buffer, \
							  uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h) \
{ \
	for (uint32_t off_y = 0; off_y < src_h; off_y++) { \
		for (uint32_t off_x = 0; off_x < src_w; off_x++) { \
			uint32_t x = src_x + off_x; \
			uint32_t y = src_y + off_y; \
			uint32_t src = *(uint32_t *)(buffer->data + y*buffer->stride + x * 4); \
			rgba_t c = { \
				.r = (src >> 16) & 0xff, \
				.g = (src >>  8) & 0xff, \
				.b = (src >>  0) & 0xff, \
				.a = 0xff, \
			}; \
			rgba_t *tgt = fb_off_##name( \
				fb, \
				output->width, output->height, \
				x, y \
			); \
			*tgt = c; \
		} \
	} \
}

COPY_TO_FB(normal);
COPY_TO_FB(90);
COPY_TO_FB(180);
COPY_TO_FB(270);


void (*copy_fns[])(rgba_t *fb, struct wvnc_output *output, struct wvnc_buffer *buffer,
				   uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h) = {
	[WL_OUTPUT_TRANSFORM_NORMAL] = copy_to_fb_normal,
	[WL_OUTPUT_TRANSFORM_90] = copy_to_fb_90,
	[WL_OUTPUT_TRANSFORM_180] = copy_to_fb_180,
	[WL_OUTPUT_TRANSFORM_270] = copy_to_fb_270,
};


void buffer_to_fb(rgba_t *fb, struct wvnc_output *output, struct wvnc_buffer *buffer,
				  uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h)
{
	if (buffer->format != WL_SHM_FORMAT_ARGB8888 && buffer->format != WL_SHM_FORMAT_XRGB8888) {
		fail("Unknown buffer format %d", buffer->format);
	}
	// We assume y_invert is true here
	// Everything will be flipped otherwise
	// TODO: Fix this
	if (output->transform >= ARRAY_SIZE(copy_fns) || copy_fns[output->transform] == NULL) {
		fail("Unknown output transform");
	}

	copy_fns[output->transform](fb, output, buffer, src_x, src_y, src_w, src_h);
}
