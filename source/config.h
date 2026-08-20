/*
 * WiiKart editable configuration.
 *
 * The parser is platform-independent C99. main.c supplies paths on the
 * Wii's FAT filesystem; host tests can load the same files directly.
 */
#ifndef WIIKART_CONFIG_H
#define WIIKART_CONFIG_H

#include "game.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONTROL_KEYBOARD_PLAYERS 2
#define CONTROL_MAX_BINDS        3
#define CONTROL_LABEL_LEN       18

enum {
    CONTROL_STEER_LEFT = 0,
    CONTROL_STEER_RIGHT,
    CONTROL_ACCEL,
    CONTROL_BRAKE,
    CONTROL_HANDBRAKE,
    CONTROL_GEAR_UP,
    CONTROL_GEAR_DOWN,
    CONTROL_BOOST,
    CONTROL_RACE_MENU,
    CONTROL_MENU_CONFIRM,
    CONTROL_MENU_BACK,
    CONTROL_ACTION_COUNT
};

/* Normalized keyboard codes. Letters use uppercase ASCII; the named
 * navigation keys live above the byte range so main.c can translate the
 * libwiikeyboard keysyms without leaking Wii headers into this module. */
enum {
    GAME_KEY_NONE = 0,
    GAME_KEY_SPACE = 32,
    GAME_KEY_ENTER = 256,
    GAME_KEY_ESCAPE,
    GAME_KEY_LEFT,
    GAME_KEY_RIGHT,
    GAME_KEY_UP,
    GAME_KEY_DOWN
};

/* Logical GameCube inputs. Dolphin exposes an Xbox pad to the game as
 * these controls; controls.json says which logical input drives each
 * action and which physical Xbox control the user should map to it. */
enum {
    GC_INPUT_A           = 1u << 0,
    GC_INPUT_B           = 1u << 1,
    GC_INPUT_X           = 1u << 2,
    GC_INPUT_Y           = 1u << 3,
    GC_INPUT_Z           = 1u << 4,
    GC_INPUT_L           = 1u << 5,
    GC_INPUT_R           = 1u << 6,
    GC_INPUT_START       = 1u << 7,
    GC_INPUT_DPAD_LEFT   = 1u << 8,
    GC_INPUT_DPAD_RIGHT  = 1u << 9,
    GC_INPUT_DPAD_UP     = 1u << 10,
    GC_INPUT_DPAD_DOWN   = 1u << 11,
    GC_INPUT_STICK_LEFT  = 1u << 12,
    GC_INPUT_STICK_RIGHT = 1u << 13
};

typedef struct {
    int show_input_overlay;
    int keyboard[CONTROL_KEYBOARD_PLAYERS][CONTROL_ACTION_COUNT]
                [CONTROL_MAX_BINDS];
    unsigned int gamecube[CONTROL_ACTION_COUNT];
    char xbox_label[CONTROL_ACTION_COUNT][CONTROL_LABEL_LEN];
} ControlConfig;

void control_config_defaults(ControlConfig *controls);
const char *control_action_name(int action);
const char *control_key_name(int key_code);
const char *control_gamecube_name(unsigned int mask);

/* Text loaders are convenient for tests and tools. File loaders are the
 * runtime entry points. Each loader commits only after the whole document
 * has parsed and validated successfully. */
int config_load_cars_text(const char *json, char *error, int error_cap);
int config_load_settings_text(GameSettings *settings, const char *json,
                              char *error, int error_cap);
int config_load_controls_text(ControlConfig *controls, const char *json,
                              char *error, int error_cap);

int config_load_cars_file(const char *path, char *error, int error_cap);
int config_load_settings_file(GameSettings *settings, const char *path,
                              char *error, int error_cap);
int config_load_controls_file(ControlConfig *controls, const char *path,
                              char *error, int error_cap);

#ifdef __cplusplus
}
#endif

#endif /* WIIKART_CONFIG_H */
