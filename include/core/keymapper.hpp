#ifndef _keymapper__hpp__included__
#define _keymapper__hpp__included__

#include <string>
#include <sstream>
#include <stdexcept>
#include <list>
#include <set>
#include <map>
#include <iostream>
#include "misc.hpp"
#include "library/keyboard.hpp"
#include "library/keyboard-mapper.hpp"
#include "library/joystick2.hpp"

/**
 * Our keyboard
 */
extern keyboard::keyboard lsnes_kbd;
/**
 * Our key mapper.
 */
extern keyboard::mapper lsnes_mapper;

/**
 * Gamepad HW.
 */
extern hw_gamepad_set lsnes_gamepads;
/**
 * Initialize gamepads (need to be called before initializing joysticks).
 */
void lsnes_gamepads_init();
/**
 * Deinitialize gamepads (need to be called after deinitializing joysticks).
 */
void lsnes_gamepads_deinit();
/**
 * Cleanup the keymapper stuff.
 */
void cleanup_keymapper();

#endif
