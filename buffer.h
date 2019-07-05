
#pragma once

#pragma once

#include "wvnc.h"

void buffer_to_fb(rgba_t *fb, struct wvnc_output *output,
				  struct wvnc_buffer *buffer);
