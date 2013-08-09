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
#ifndef INTERRUPTER_H
#define INTERRUPTER_H

//
// Modified 2012-07-10 to 2012-07-14 by H. Ilari Liusvaara
//	- Make it rerecording-friendly.

#include <string>
#include <vector>
#include "loadsave.h"


namespace gambatte {

struct GsCode {
	unsigned short address;
	unsigned char value;
	unsigned char type;

	void loadOrSave(loadsave& state) {
		state(address);
		state(value);
		state(type);
	}
};

class Interrupter {
	unsigned short &SP;
	unsigned short &PC;
	std::vector<GsCode> gsCodes;
	
	void applyVblankCheats(unsigned cc, class Memory &mem);
public:
	Interrupter(unsigned short &SP, unsigned short &PC);
	unsigned interrupt(const unsigned address, unsigned cycleCounter, class Memory &memory);
	void setGameShark(const std::string &codes);

	void loadOrSave(loadsave& state);
};

}

#endif
