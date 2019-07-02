
#include <assert.h>
#include <string.h>

#include <xkbcommon/xkbcommon.h>

#include "utils.h"

#include "keymap.h"


void keymap_init(struct keymap *keymap)
{
	for (int i = 0; i < 0x7f; i++) {
		keymap_push_keysym(keymap, i);
	}
}


enum keymap_push_result keymap_push_keysym(struct keymap *keymap, uint32_t keysym)
{
	if (keysym > KEYMAP_MAX_KEYSYM) {
		// This might be some unicode shit or something
		return KEYMAP_PUSH_NOT_FOUND;
	}

	if (keymap->map[keysym].name != NULL) {
		return KEYMAP_PUSH_PRESENT;
	}

	char buffer[256];
	int ret = xkb_keysym_get_name(keysym, buffer, sizeof(buffer));
	if (ret <= 0) {
		return KEYMAP_PUSH_NOT_FOUND;
	}

	keymap->map[keysym].name = strdup(buffer);
	keymap->map[keysym].keycode = keymap->loaded_count++;

	return KEYMAP_PUSH_ADDED;
}


void keymap_print_to_file(struct keymap *keymap, FILE *f)
{
	fprintf(f, "xkb_keymap {\n");

	fprintf(
		f,
		"xkb_keycodes \"(unnamed)\" {\n"
		"minimum = 8;\n"
		"maximum = %ld;\n",
		keymap->loaded_count + 8
	);
	for (size_t i = 0; i < keymap->loaded_count; i++) {
		fprintf(f, "<K%ld> = %ld;\n", i, i + 8);
	}
	fprintf(f, "};\n");

	fprintf(f, "xkb_types \"(unnamed)\" {};\n");
	fprintf(f, "xkb_compatibility \"(unnamed)\" {};\n");

	fprintf(f, "xkb_symbols \"(unnamed)\" {\n");
	int cnt = 0;
	for (size_t i = 0; i < KEYMAP_MAX_KEYSYM; i++) {
		if (keymap->map[i].name == NULL) {
			continue;
		}
		fprintf(f, "key <K%d> {[ %s ]};\n", cnt++, keymap->map[i].name);
	}
	fprintf(f, "};\n");

	fprintf(f, "};\n");
	fputc('\0', f);
	fflush(f);
}


uint32_t keymap_get_keycode(struct keymap *keymap, uint32_t keysym)
{
	assert(keysym <= KEYMAP_MAX_KEYSYM);
	return keymap->map[keysym].keycode;
}


static struct {
	const char *name; enum keymap_mod mod;
} mod_map[] = {
	{ "Shift_L", KEYMAP_MOD_SHIFT },
	{ "Shift_R", KEYMAP_MOD_SHIFT },
	{ "Control_L", KEYMAP_MOD_CTRL },
	{ "Control_R", KEYMAP_MOD_CTRL },
	{ "Super_L", KEYMAP_MOD_SUPER },
	{ "Super_R", KEYMAP_MOD_SUPER },
	{ "ISO_Level3_Shift", KEYMAP_MOD_ALTGR },
	{ "Caps Lock", KEYMAP_MOD_CAPSLOCK },
};


enum keymap_mod keymap_get_modifier(struct keymap *keymap, uint32_t keysym)
{
	if (keysym > KEYMAP_MAX_KEYSYM || keymap->map[keysym].name == NULL) {
		return KEYMAP_MOD_NONE;
	}

	for (size_t i = 0; i < ARRAY_SIZE(mod_map); i++) {
		if (!strcmp(keymap->map[keysym].name, mod_map[i].name)) {
			return mod_map[i].mod;
		}
	}

	return KEYMAP_MOD_NONE;
}
