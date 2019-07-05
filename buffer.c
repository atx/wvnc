
#include <wayland-client.h>

#include "utils.h"

#include "buffer.h"


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
static void copy_to_fb_##name(rgba_t *fb, struct wvnc_output *output, struct wvnc_buffer *buffer) \
{ \
	for (uint32_t y = 0; y < buffer->height; y++) { \
		for (uint32_t x = 0; x < buffer->width; x++) { \
			uint32_t src = *(uint32_t *)(buffer->data + y * buffer->stride + x * 4); \
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


void (*copy_fns[])(rgba_t *fb, struct wvnc_output *output, struct wvnc_buffer *buffer) = {
	[WL_OUTPUT_TRANSFORM_NORMAL] = copy_to_fb_normal,
	[WL_OUTPUT_TRANSFORM_90] = copy_to_fb_90,
	[WL_OUTPUT_TRANSFORM_180] = copy_to_fb_180,
	[WL_OUTPUT_TRANSFORM_270] = copy_to_fb_270,
};


void buffer_to_fb(rgba_t *fb, struct wvnc_output *output, struct wvnc_buffer *buffer)
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

	copy_fns[output->transform](fb, output, buffer);
}
