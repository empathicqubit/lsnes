/***************************************************************************
 *   Copyright (C) 2007 by Sindre Aamås                                    *
 *   sinamas@users.sourceforge.net                                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License version 2 as     *
 *   published by the Free Software Foundation.                            *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License version 2 for more details.                *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   version 2 along with this program; if not, write to the               *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "joysticklock.h"
#include <map>

static int pollJsEvent(SDL_Event *const ev, int const insensitivity) {
	typedef std::map<unsigned, int> Map;
	static Map axisState;

	int evValid;

	do {
		evValid = SDL_PollEvent(ev);
		if (evValid && ev->type == SDL_JOYAXISMOTION) {
			enum { threshold = 8192 };
			Map::iterator const at =
				axisState.insert(Map::value_type(ev->id, SdlJoystick::axis_centered)).first;
			switch (at->second) {
			case SdlJoystick::axis_centered:
				if (ev->value >= threshold + insensitivity)
					ev->value = SdlJoystick::axis_positive;
				else if (ev->value <= -(threshold + insensitivity))
					ev->value = SdlJoystick::axis_negative;
				else
					continue;

				break;
			case SdlJoystick::axis_positive:
				if (ev->value >= threshold - insensitivity)
					continue;

				ev->value = ev->value <= -(threshold + insensitivity)
				          ? SdlJoystick::axis_negative
				          : SdlJoystick::axis_centered;
				break;
			case SdlJoystick::axis_negative:
				if (ev->value <= -(threshold - insensitivity))
					continue;

				ev->value = ev->value >= threshold + insensitivity
				          ? SdlJoystick::axis_positive
				          : SdlJoystick::axis_centered;
				break;
			}

			at->second = ev->value;
		}
	} while (false);

	return evValid;
}

int SdlJoystick::Locked::pollEvent(SDL_Event *ev, int insensitivity) {
	return pollJsEvent(ev, insensitivity);
}

QMutex SdlJoystick::mutex_;
