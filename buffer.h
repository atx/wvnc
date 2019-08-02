
#pragma once

#pragma once

#include "wvnc.h"

void buffer_calculate_fb_coords(struct wvnc_output *output,
								uint32_t src_x, uint32_t src_y,
								uint32_t *fb_x, uint32_t *fb_y);

void buffer_to_fb(rgba_t *fb, struct wvnc_output *output, struct wvnc_buffer *buffer,
				  uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h);
