
#include <fcntl.h>
#include <linux/uinput.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "utils.h"

#include "uinput.h"


int uinput_init(struct wvnc_uinput *uinput)
{
	uinput->fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (uinput->fd < 0) {
		return uinput->fd;
	}

	uint32_t keys[] = {
		BTN_LEFT, BTN_RIGHT, BTN_MIDDLE,
	};

	ioctl(uinput->fd, UI_SET_EVBIT, EV_KEY);
	for (size_t i = 0; i < ARRAY_SIZE(keys); i++) {
		ioctl(uinput->fd, UI_SET_KEYBIT, keys[i]);
	}

	ioctl(uinput->fd, UI_SET_EVBIT, EV_ABS);
	ioctl(uinput->fd, UI_SET_ABSBIT, ABS_X);
	ioctl(uinput->fd, UI_SET_ABSBIT, ABS_Y);

	ioctl(uinput->fd, UI_SET_EVBIT, EV_REL);
	ioctl(uinput->fd, UI_SET_RELBIT, REL_WHEEL);
	
	struct uinput_abs_setup abs;
	memset(&abs, 0, sizeof(abs));
	abs.absinfo.maximum = UINPUT_ABS_MAX;
	abs.absinfo.minimum = 0;
	abs.code = ABS_X;
	ioctl(uinput->fd, UI_ABS_SETUP, &abs);
	abs.code = ABS_Y;
	ioctl(uinput->fd, UI_ABS_SETUP, &abs);
	
	struct uinput_setup set;
	memset(&set, 0, sizeof(set));
	set.id.bustype = BUS_VIRTUAL;
	set.id.vendor = 0x0;
	set.id.product = 0x0;
	set.id.version = 1;
	strncpy(set.name, "wvnc-device", sizeof(set.name));
	ioctl(uinput->fd, UI_DEV_SETUP, &set);

	ioctl(uinput->fd, UI_DEV_CREATE, 0);

	// Not sleeping here causes userspace to just ignore the device,
	// go figure.
	// I don't think we necessarily need this, but still
	usleep(500000);
	uinput->initialized = true;

	return 0;
}


static int dispatch_events(struct wvnc_uinput *uinput,
						   struct input_event *evs, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		ssize_t ret = write(uinput->fd, &evs[i], sizeof(evs[i]));
		if (ret < 0) {
			return ret;
		}
	}
	return 0;
}


int uinput_move_abs(struct wvnc_uinput *uinput, int32_t x, int32_t y)
{
	struct input_event evs[] = {
		{
			.type = EV_ABS,
			.code = ABS_X,
			.value = x,
		},
		{
			.type = EV_ABS,
			.code = ABS_Y,
			.value = y,
		},
		{
			.type = EV_SYN,
			.code = 0,
			.value = SYN_REPORT,
		},
	};

	return dispatch_events(uinput, evs, ARRAY_SIZE(evs));
}


int uinput_set_buttons(struct wvnc_uinput *uinput, bool left, bool middle, bool right)
{
	struct input_event evs[] = {
		{
			.type = EV_KEY,
			.code = BTN_LEFT,
			.value = left
		},
		{
			.type = EV_KEY,
			.code = BTN_MIDDLE,
			.value = middle
		},
		{
			.type = EV_KEY,
			.code = BTN_RIGHT,
			.value = right
		},
		{
			.type = EV_SYN,
			.code = 0,
			.value = SYN_REPORT,
		},
	};
	return dispatch_events(uinput, evs, ARRAY_SIZE(evs));
}


int uinput_wheel(struct wvnc_uinput *uinput, bool up)
{
	struct input_event evs[] = {
		{
			.type = EV_REL,
			.code = REL_WHEEL,
			.value = up ? 1 : -1
		},
		{
			.type = EV_SYN,
			.code = 0,
			.value = SYN_REPORT,
		},
	};
	return dispatch_events(uinput, evs, ARRAY_SIZE(evs));
}


void uinput_destroy(struct wvnc_uinput *uinput)
{
	ioctl(uinput->fd, UI_DEV_DESTROY, 0);
	close(uinput->fd);
	uinput->initialized = false;
}
