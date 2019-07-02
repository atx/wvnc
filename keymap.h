
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Mapping between VNC keycodes and our keymap
// As everything sucks, we have to do it this way instead of just passing
// the keycodes along to the compositor

// No idea if this is correct, maybe...
#define KEYMAP_MAX_KEYSYM 0xffff


// This is from virtboard by purism libre
// apparently those are some magical undocumented constants...
enum keymap_mod {
	KEYMAP_MOD_SHIFT = 0,
	KEYMAP_MOD_CAPSLOCK = 1,
	KEYMAP_MOD_CTRL = 2,
	KEYMAP_MOD_SUPER = 6,
	KEYMAP_MOD_ALTGR = 7,

	KEYMAP_MOD_NONE = 0xffff
};
#define KEYMAP_MOD_MAX 7


struct keymap_mapping {
	uint32_t keycode;  // Incremental numbering
	const char *name;  // This is what we use to make an XKB keymap from
};

struct keymap {
	struct keymap_mapping map[KEYMAP_MAX_KEYSYM + 1];
	size_t loaded_count;
};

enum keymap_push_result {
	KEYMAP_PUSH_NOT_FOUND,
	KEYMAP_PUSH_ADDED,
	KEYMAP_PUSH_PRESENT
};


void keymap_init(struct keymap *keymap);
enum keymap_push_result keymap_push_keysym(struct keymap *keymap, uint32_t keysym);
void keymap_print_to_file(struct keymap *keymap, FILE *f);
uint32_t keymap_get_keycode(struct keymap *keymap, uint32_t keysym);
enum keymap_mod keymap_get_modifier(struct keymap *keymap, uint32_t keysym);
