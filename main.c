
#include <argp.h>
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>
#include <wayland-client.h>

#include <rfb/rfb.h>

#include "wayland-virtual-keyboard-client-protocol.h"
#include "wayland-wlr-screencopy-client-protocol.h"
#include "wayland-xdg-output-client-protocol.h"

#include "keymap.h"
#include "uinput.h"
#include "utils.h"


struct wvnc;
struct wvnc_output;

struct rgba {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
} __attribute__((packed));
typedef struct rgba rgba_t;
static_assert(sizeof(struct rgba) == 4, "Invalid size of struct rgba");


struct wvnc_buffer {
	struct wvnc *wvnc;

	struct wl_buffer *wl;
	void *data;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	size_t size;
	enum wl_shm_format format;
	bool y_invert;

	bool done;
};


struct wvnc_args {
	const char *output;
	in_addr_t address;
	int port;
	int period;
	bool no_uinput;
};


struct wvnc {
	struct {
		rfbScreenInfo *screen_info;
		rgba_t *fb;
		rgba_t *fb_next;
	} rfb;
	struct {
		struct wl_display *display;
		struct wl_registry *registry;
		struct wl_shm *shm;
		struct wl_seat *seat;
		struct zxdg_output_manager_v1 *output_manager;
		struct zwlr_screencopy_manager_v1 *screencopy_manager;
		struct zwp_virtual_keyboard_manager_v1 *keyboard_manager;
		struct zwp_virtual_keyboard_v1 *keyboard;
	} wl;

	struct wvnc_args args;

	struct wvnc_uinput uinput;

	struct keymap keymap;
	unsigned int mod_counters[KEYMAP_MOD_MAX + 1];

	struct wvnc_buffer buffer;

	struct wl_list outputs;
	struct wvnc_output *selected_output;

	uint32_t logical_width;
	uint32_t logical_height;
};


struct wvnc_output {
	struct wl_output *wl;
	struct zxdg_output_v1 *xdg;
	struct wl_list link;

	int32_t x;
	int32_t y;
	uint32_t width;
	uint32_t height;
	enum wl_output_transform transform;

	const char *name;
};


// This is because we can't pass our global pointer into some of the 
// rfb callbacks. Use minimally.
thread_local struct wvnc *global_wvnc;


static void initialize_shm_buffer(struct wvnc_buffer *buffer,
								  enum wl_shm_format format,
								  uint32_t width, uint32_t height,
								  uint32_t stride)
{
	int fd = shm_create();
	size_t size = stride * height;
	int ret = ftruncate(fd, size);
	if (ret < 0) {
		fail("Failed to allocate shm buffer");
	}

	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		fail("mmap failed");
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(buffer->wvnc->wl.shm, fd, size);
	close(fd);
	struct wl_buffer *wl_buffer = wl_shm_pool_create_buffer(
		pool, 0, width, height, stride, format
	);
	wl_shm_pool_destroy(pool);

	buffer->wl = wl_buffer;
	buffer->data = data;
	buffer->width = width;
	buffer->height = height;
	buffer->size = size;
	buffer->format = format;
	buffer->stride = stride;
}


static void handle_output_geometry(void *data, struct wl_output *wl,
								   int32_t x, int32_t y,
								   int32_t p_w, int32_t p_h,
								   int32_t subp, const char *make,
								   const char *model,
								   int32_t transform)
{
	struct wvnc_output *output = data;
	output->transform = transform;
}


static void handle_output_mode(void *data, struct wl_output *wl,
							   uint32_t flags, int32_t width, int32_t height,
							   int32_t refresh)
{
}


static void handle_output_scale(void *data, struct wl_output *wl, int32_t factor)
{
	// TODO: Do we care?
}


static void handle_output_done(void *data, struct wl_output *wl)
{
}


struct wl_output_listener output_listener = {
	.geometry = handle_output_geometry,
	.mode = handle_output_mode,
	.done = handle_output_done,
	.scale = handle_output_scale,
};


static void handle_frame_buffer(void *data,
								struct zwlr_screencopy_frame_v1 *frame,
								enum wl_shm_format format, uint32_t width,
								uint32_t height, uint32_t stride)
{
	struct wvnc_buffer *buffer = data;
	// TODO: What if input size changes or something?
	if (buffer->wl == NULL) {
		initialize_shm_buffer(buffer, format, width, height, stride);
	}
	zwlr_screencopy_frame_v1_copy(frame, buffer->wl);
}


static void handle_frame_flags(void *data,
							   struct zwlr_screencopy_frame_v1 *frame,
							   uint32_t flags)
{
	// TODO: Handle Y-inversion here? (the only flag currently defined)
	struct wvnc_buffer *buffer = data;
	if (flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) {
		buffer->y_invert = true;
	}
}


static void handle_frame_ready(void *data,
							   struct zwlr_screencopy_frame_v1 *frame,
							   uint32_t tv_sec_hi, uint32_t tv_sec_lo,
							   uint32_t tv_nsec)
{
	struct wvnc_buffer *buffer = data;
	buffer->done = true;
}

static void handle_frame_failed(void *data,
								struct zwlr_screencopy_frame_v1 *frame)
{
	fail("Failed to capture output");
}


static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
	.buffer = handle_frame_buffer,
	.flags = handle_frame_flags,
	.ready = handle_frame_ready,
	.failed = handle_frame_failed
};


static void handle_xdg_output_logical_position(void *data,
											   struct zxdg_output_v1 *xdg,
											   int32_t x, int32_t y)
{
	struct wvnc_output *output = data;
	output->x = x;
	output->y = y;
}


static void handle_xdg_output_logical_size(void *data,
										   struct zxdg_output_v1 *xdg,
										   int32_t width, int32_t height)
{
	struct wvnc_output *output = data;
	// Note that this is _logical size_
	// That is, it includes rotations and scalings
	output->width = width;
	output->height = height;
}


static void handle_xdg_output_done(void *data, struct zxdg_output_v1 *xdg)
{
}


static void handle_xdg_output_name(void *data, struct zxdg_output_v1 *xdg,
								   const char *name)
{
	struct wvnc_output *output = data;
	output->name = strdup(name);
}


static void handle_xdg_output_description(void *data, struct zxdg_output_v1 *xdg,
										  const char *description)
{
}


static const struct zxdg_output_v1_listener xdg_output_listener = {
	.logical_position = handle_xdg_output_logical_position,
	.logical_size = handle_xdg_output_logical_size,
	.done = handle_xdg_output_done,
	.name = handle_xdg_output_name,
	.description = handle_xdg_output_description,
};


static void handle_wl_registry_global(void *data, struct wl_registry *registry,
									  uint32_t name, const char *interface,
									  uint32_t version)
{
#define IS_PROTOCOL(x) !strcmp(interface, x##_interface.name)
#define BIND(x, ver) wl_registry_bind(registry, name, &x##_interface, min((uint32_t)ver, version))
	struct wvnc *wvnc = data;
	if (IS_PROTOCOL(wl_output)) {
		struct wvnc_output *out = xmalloc(sizeof(struct wvnc_output));
		out->wl = BIND(wl_output, 1);
		wl_output_add_listener(out->wl, &output_listener, out);
		wl_list_insert(&wvnc->outputs, &out->link);
	} else if (IS_PROTOCOL(zxdg_output_manager_v1)) {
		// TODO: Load names
		wvnc->wl.output_manager = BIND(zxdg_output_manager_v1, 2);
	} else if (IS_PROTOCOL(zwlr_screencopy_manager_v1)) {
		wvnc->wl.screencopy_manager = BIND(zwlr_screencopy_manager_v1, 1);
	} else if (IS_PROTOCOL(wl_shm)) {
		wvnc->wl.shm = BIND(wl_shm, 1);
	} else if (IS_PROTOCOL(wl_seat)) {
		wvnc->wl.seat = BIND(wl_seat, 7);
	} else if (IS_PROTOCOL(zwp_virtual_keyboard_manager_v1)) {
		wvnc->wl.keyboard_manager = BIND(zwp_virtual_keyboard_manager_v1, 1);
	}
#undef BIND
#undef IS_PROTOCOL
}


static void handle_wl_registry_global_remove(void *data,
											 struct wl_registry *registry,
											 uint32_t name)
{
}


static const struct wl_registry_listener registry_listener = {
	.global = handle_wl_registry_global,
	.global_remove = handle_wl_registry_global_remove,
};


rgba_t *get_transformed_fb_ptr(rgba_t *fb, enum wl_output_transform transform,
							   uint32_t width, uint32_t height,
							   uint32_t ox, uint32_t oy)
{
	// TODO: This will not get inlined/vectorized well. We should
	// build multiple versions of copy_to_next_fb with different transforms
	// baked in
	uint32_t tx, ty;
	// TODO: This assumes we have y_flipped!
	// This should be corrected globally be changing the flag after we get the
	// first buffer back.
	switch (transform) {
	case WL_OUTPUT_TRANSFORM_NORMAL:
		tx = ox;
		ty = height - oy - 1;
		break;
	case WL_OUTPUT_TRANSFORM_90:
		tx = width - oy - 1;
		ty = height - ox - 1;
		break;
	case WL_OUTPUT_TRANSFORM_180:
		tx = ox;
		ty = oy;
		break;
	case WL_OUTPUT_TRANSFORM_270:
		tx = oy;
		ty = ox;
		break;
	default:
		assert(false);
		// Meh, this will break but whatever
		tx = ox;
		ty = oy;
	};

	return &fb[ty * width + tx];
}


static void update_virtual_keyboard(struct wvnc *wvnc)
{
	int fd = shm_create();
	FILE *f = fdopen(fd, "w");
	keymap_print_to_file(&wvnc->keymap, f);
	size_t size = ftell(f);

	zwp_virtual_keyboard_v1_keymap(wvnc->wl.keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
	wl_display_dispatch_pending(wvnc->wl.display);
	fclose(f);
}


static enum rfbNewClientAction rfb_new_client_hook(rfbClientPtr cl)
{
	cl->clientData = global_wvnc;
	return RFB_CLIENT_ACCEPT;
}


static void rfb_ptr_hook(int mask, int screen_x, int screen_y, rfbClientPtr cl)
{
	struct wvnc *wvnc = cl->clientData;
	if (!wvnc->uinput.initialized) {
		return; // Nothing to do here
	}
	// Way too lazy to debug fixpoing scaling
	float global_x = (float)wvnc->selected_output->x +
		clamp(screen_x, 0, (int)wvnc->selected_output->width);
	float global_y = (float)wvnc->selected_output->y +
		clamp(screen_y, 0, (int)wvnc->selected_output->height);
	int32_t touch_x = round(global_x / wvnc->logical_width * UINPUT_ABS_MAX);
	int32_t touch_y = round(global_y / wvnc->logical_height * UINPUT_ABS_MAX);

	uinput_move_abs(&wvnc->uinput, (int32_t)touch_x, (int32_t)touch_y);

	uinput_set_buttons(
		&wvnc->uinput,
		!!(mask & BIT(0)), !!(mask & BIT(1)), !!(mask & BIT(2))
	);

	if (mask & BIT(4)) {
		uinput_wheel(&wvnc->uinput, false);
	}
	if (mask & BIT(3)) {
		uinput_wheel(&wvnc->uinput, true);
	}
}


static void rfb_key_hook(rfbBool down, rfbKeySym keysym, rfbClientPtr cl)
{
	struct wvnc *wvnc = cl->clientData;
	if (wvnc->wl.keyboard == NULL) {
		return;
	}

	enum keymap_push_result result = keymap_push_keysym(&wvnc->keymap, keysym);
	if (result == KEYMAP_PUSH_NOT_FOUND) {
		log_error("Unknown keysym %04x", keysym);
		return;
	}

	if (result == KEYMAP_PUSH_ADDED) {
		log_info("Updating virtual keymap");
		update_virtual_keyboard(wvnc);
	}

	enum keymap_mod mod = keymap_get_modifier(&wvnc->keymap, keysym);
	if (mod != KEYMAP_MOD_NONE) {
		if (down) {
			wvnc->mod_counters[mod]++;
		} else if (wvnc->mod_counters[mod] > 0){
			wvnc->mod_counters[mod]--;
		} else {
			log_error("Modifier %d released without being pressed first!", mod);
		}

		uint32_t mods = 0;
		for (size_t i = 0; i < ARRAY_SIZE(wvnc->mod_counters); i++) {
			if (wvnc->mod_counters[i] > 0) {
				mods |= BIT(i);
			}
		}

		log_info("Sending mod mask %02x", mods);

		zwp_virtual_keyboard_v1_modifiers(
			wvnc->wl.keyboard, mods, 0, 0, 0
		);
	}

	zwp_virtual_keyboard_v1_key(
		wvnc->wl.keyboard, 0, keymap_get_keycode(&wvnc->keymap, keysym),
		down ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED
	);
	wl_display_dispatch_pending(wvnc->wl.display);
}


static void copy_to_next_fb(struct wvnc *wvnc, struct wvnc_buffer *buffer)
{
	// TODO: Support different formats
	assert(buffer->format == WL_SHM_FORMAT_ARGB8888 || buffer->format == WL_SHM_FORMAT_XRGB8888);
	for (uint32_t y = 0; y < buffer->height; y++) {
		for (uint32_t x = 0; x < buffer->width; x++) {
			uint32_t src = *(uint32_t *)(buffer->data + y * buffer->stride + x * 4);
			rgba_t c = {
				.r = (src >> 16) & 0xff,
				.g = (src >>  8) & 0xff,
				.b = (src >>  0) & 0xff,
				.a = 0xff,
			};
			rgba_t *tgt = get_transformed_fb_ptr(
				wvnc->rfb.fb_next, wvnc->selected_output->transform,
				wvnc->selected_output->width, wvnc->selected_output->height,
				x, y
			);
			*tgt = c;
		}
	}
}


static void update_framebuffer(struct wvnc *wvnc)
{
	// TODO: Maybe get something more sophisticated here?
	uint64_t scanline_damage_map[wvnc->selected_output->height / 64 + 1];
	memset(scanline_damage_map, 0, sizeof(scanline_damage_map));

	for (uint32_t y = 0; y < wvnc->selected_output->height; y++) {
		bool damaged = false;
		for (uint32_t x = 0; x < wvnc->selected_output->width; x++) {
			size_t off = y*wvnc->selected_output->width + x;
			rgba_t src = wvnc->rfb.fb_next[off];
			rgba_t *tgt = &wvnc->rfb.fb[off];
			if (src.r != tgt->r || src.g != tgt->g || src.b != tgt->b || src.a != tgt->a) {
				*tgt = src;
				damaged = true;
			}
		}
		if (damaged) {
			scanline_damage_map[y / 64] |= 1 << (y % 64);
		}
	}

	for (uint32_t y = 0; y < wvnc->selected_output->height; y++) {
		uint64_t bits = scanline_damage_map[y / 64];
		if (bits & (1 << (y % 64))) {
			rfbMarkRectAsModified(
					wvnc->rfb.screen_info,
					0, y, wvnc->selected_output->width, y + 1
			);
		}
	}
}


static void calculate_logical_size(struct wvnc *wvnc)
{
	int32_t min_x = INT32_MAX;
	int32_t max_x = INT32_MIN;
	int32_t min_y = INT32_MAX;
	int32_t max_y = INT32_MIN;
	struct wvnc_output *output;
	wl_list_for_each(output, &wvnc->outputs, link) {
		log_info(output->name);
		min_x = min(min_x, output->x);
		max_x = max(max_x, output->x + (int32_t)output->width);
		min_y = min(min_y, output->y);
		max_y = max(max_y, output->y + (int32_t)output->height);
	}
	wvnc->logical_width = max_x - min_x;
	wvnc->logical_height = max_y - min_y;
	log_info("Boundaries %dx%d %dx%d", min_x, min_y, max_x, max_y);
}


static void init_virtual_keyboard(struct wvnc *wvnc)
{
	if (wvnc->wl.keyboard_manager == NULL || wvnc->wl.seat == NULL) {
		log_error("Unable to create a virtual keyboard");
		return;
	}
	wvnc->wl.keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
		wvnc->wl.keyboard_manager, wvnc->wl.seat
	);

	keymap_init(&wvnc->keymap);
	update_virtual_keyboard(wvnc);
}


static void init_wayland(struct wvnc *wvnc)
{
	wvnc->wl.display = wl_display_connect(NULL);
	if (wvnc->wl.display == NULL) {
		fail("Failed to connect to the Wayland display");
	}
	wl_list_init(&wvnc->outputs);
	wvnc->buffer.wvnc = wvnc;
	wvnc->wl.registry = wl_display_get_registry(wvnc->wl.display);
	wl_registry_add_listener(wvnc->wl.registry, &registry_listener, wvnc);
	wl_display_dispatch(wvnc->wl.display);
	wl_display_roundtrip(wvnc->wl.display);
	if (wvnc->wl.screencopy_manager == NULL) {
		fail("wlr-screencopy protocol not supported!");
	}
	if (wvnc->wl.output_manager == NULL) {
		fail("xdg-output-manager protocol not supported");
	}
	struct wvnc_output *output;
	wl_list_for_each(output, &wvnc->outputs, link) {
		output->xdg = zxdg_output_manager_v1_get_xdg_output(
			wvnc->wl.output_manager, output->wl
		);
		zxdg_output_v1_add_listener(output->xdg, &xdg_output_listener, output);
	}
	wl_display_dispatch(wvnc->wl.display);
	wl_display_roundtrip(wvnc->wl.display);
	init_virtual_keyboard(wvnc);
	calculate_logical_size(wvnc);
	log_info("Wayland initialized with %d outputs", wl_list_length(&wvnc->outputs));
}


static void init_rfb(struct wvnc *wvnc)
{
	log_info("Initializing RFB");
	// 4 bytes per pixel only at the moment. Probably not worth using anything
	// else.
	wvnc->rfb.screen_info = rfbGetScreen(
		NULL, NULL,
		wvnc->selected_output->width, wvnc->selected_output->height,
		8, 3, 4
	);
	wvnc->rfb.screen_info->desktopName = "wvnc";
	wvnc->rfb.screen_info->alwaysShared = true;
	wvnc->rfb.screen_info->port = wvnc->args.port;
	wvnc->rfb.screen_info->listenInterface = wvnc->args.address;
	// TODO: Maybe enable IPv6 someday
	wvnc->rfb.screen_info->ipv6port = 0;
	wvnc->rfb.screen_info->listen6Interface = NULL;
	wvnc->rfb.screen_info->screenData = wvnc;
	wvnc->rfb.screen_info->newClientHook = rfb_new_client_hook;
	wvnc->rfb.screen_info->kbdAddEvent = rfb_key_hook;
	wvnc->rfb.screen_info->ptrAddEvent = rfb_ptr_hook;
	rfbLog = log_info;
	rfbErr = log_error;

	size_t fb_size = wvnc->selected_output->width * wvnc->selected_output->height * sizeof(rgba_t);
	wvnc->rfb.fb = xmalloc(fb_size);
	wvnc->rfb.fb_next = xmalloc(fb_size);
	wvnc->rfb.screen_info->frameBuffer = (char *)wvnc->rfb.fb;

	log_info("Starting the VNC server");
	rfbInitServer(wvnc->rfb.screen_info);
}


const char *argp_program_version = "wvnc 0.0";
const char *argp_program_bug_address = "<atx@atx.name>";


static struct argp_option argp_options[] = {
	{ "output", 'o', "OUTPUT", 0, "Select output", 0 },
	{ "bind", 'b', "ADDRESS", 0, "Select bind address", 0 },
	{ "port", 'p', "PORT", 0, "Select port", 0 },
	{ "period", 't', "PERIOD", 0, "Sampling period in ms", 0 },
	{ "no-uinput", 'U', NULL, 0, "Disable uinput tablet", 0 },
	{ NULL, 0, NULL, 0, NULL, 0 }
};


static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	struct wvnc_args *args = state->input;
	switch(key) {
	case 'o':
		args->output = arg;
		break;
	case 'b':
		args->address = inet_addr(arg);
		if (args->address == INADDR_NONE) {
			argp_failure(state, EXIT_FAILURE, 0, "Invalid bind address");
		}
		break;
	case 'p':
		args->port = atoi(arg);
		if (args->port <= 0) {
			argp_failure(state, EXIT_FAILURE, 0, "Invalid port");
		}
		break;
	case 't':
		args->period = atoi(arg);
		if (args->period <= 0) {
			argp_failure(state, EXIT_FAILURE, 0, "Invalid period");
		}
		break;
	case 'U':
		args->no_uinput = true;
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}


int main(int argc, char *argv[])
{
	struct wvnc *wvnc = xmalloc(sizeof(struct wvnc));
	global_wvnc = wvnc;
	wvnc->args.port = 5100;
	wvnc->args.address = inet_addr("127.0.0.1");
	wvnc->args.period = 30;  // 30 FPS-ish

	struct argp argp = { argp_options, parse_opt, NULL, NULL, NULL, NULL, NULL };
	argp_parse(&argp, argc, argv, 0, NULL, &wvnc->args);

	// Initialize uinput
	// For some reason, we absolutely have to initialize this
	// before initializing wayland
	if (!wvnc->args.no_uinput) {
		int ret = uinput_init(&wvnc->uinput);
		if (ret) {
			log_error("Failed to initialize uinput: %s", strerror(errno));
		}
	}
	init_wayland(wvnc);

	if (wl_list_length(&wvnc->outputs) > 1) {
		if (wvnc->args.output == NULL) {
			fail("Multiple outputs specified but none explicitly selected");
		}
		struct wvnc_output *out;
		wl_list_for_each(out, &wvnc->outputs, link) {
			if (!strcmp(out->name, wvnc->args.output)) {
				wvnc->selected_output = out;
				break;
			}
		}
	} else {
		struct wvnc_output *out;
		wl_list_for_each(out, &wvnc->outputs, link) {
			wvnc->selected_output = out;
			break;
		}
	}
	if (wvnc->selected_output == NULL) {
		fail("No output found");
	}
	// TODO: Handle size and transformations
	log_info("Starting on output %s with resolution %dx%d",
			 wvnc->selected_output->name,
			 wvnc->selected_output->width, wvnc->selected_output->height);

	// Initialize RFB
	init_rfb(wvnc);

	// Start capture
	uint64_t last_capture = 0; // Start of last capture
	const uint64_t capture_period = wvnc->args.period * 1000;
	struct zwlr_screencopy_frame_v1 *frame = NULL;
	bool capturing = false;
	while (true) {
		struct wvnc_buffer *buffer = &wvnc->buffer;
		// TODO: Should we composite the cursor or not?
		uint64_t t_now = time_monotonic();
		uint64_t t_delta = t_now - last_capture;
		if (t_delta >= capture_period && !capturing) {
			last_capture = t_now;
			capturing = true;
			buffer->done = false;
			frame = zwlr_screencopy_manager_v1_capture_output(
				wvnc->wl.screencopy_manager, 0, wvnc->selected_output->wl
			);
			zwlr_screencopy_frame_v1_add_listener(frame, &frame_listener, buffer);
			wl_display_dispatch(wvnc->wl.display);
			wl_display_flush(wvnc->wl.display);
		} else if (capturing && buffer->done) {
			capturing = false;

			copy_to_next_fb(wvnc, buffer);
			update_framebuffer(wvnc);

			zwlr_screencopy_frame_v1_destroy(frame);
		}

		// TODO: Maybe use epoll or something
		struct timeval timeout = {
			.tv_sec = 0,
			.tv_usec = t_delta < capture_period ? (capture_period - t_delta) : 0
		};
		int wl_fd = wl_display_get_fd(wvnc->wl.display);
		fd_set fds;
		memcpy(&fds, &wvnc->rfb.screen_info->allFds, sizeof(fd_set));
		FD_SET(wl_fd, &fds);
		int ret = select(wvnc->rfb.screen_info->maxFd + 2, &fds, NULL, NULL, &timeout);
		if (ret != 0) {
			bool is_wl = FD_ISSET(wl_fd, &fds);
			if (is_wl) {
				wl_display_dispatch(wvnc->wl.display);
			}
			// No way of checking directly
			if ((is_wl && ret > 1) || ret == 1) {
				rfbProcessEvents(wvnc->rfb.screen_info, 0);
			}
		}
	}


	free(wvnc);
}
