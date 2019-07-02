
#pragma once

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>


struct wvnc_uinput {
	int fd;
	bool initialized;
};

#define UINPUT_ABS_MAX INT16_MAX


int uinput_init(struct wvnc_uinput *uinput);
int uinput_move_abs(struct wvnc_uinput *uinput, int32_t x, int32_t y);
int uinput_set_buttons(struct wvnc_uinput *uinput, bool left, bool middle, bool right);
int uinput_wheel(struct wvnc_uinput *uinput, bool up);
void uinput_destroy(struct wvnc_uinput *uinput);
