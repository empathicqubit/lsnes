/***************************************************************************
 *   Copyright (C) 2007 by Sindre Aam�s                                    *
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
#ifndef DUTY_UNIT_H
#define DUTY_UNIT_H

//
// Modified 2012-07-10 to 2012-07-14 by H. Ilari Liusvaara
//	- Make it rerecording-friendly.

#include "sound_unit.h"
#include "master_disabler.h"
#include "../savestate.h"
#include "../loadsave.h"

namespace gambatte {

class DutyUnit : public SoundUnit {
	unsigned nextPosUpdate;
	unsigned short period;
	unsigned char pos;
	unsigned char duty;
	bool high;
	bool enableEvents;

	void setCounter();
	void setDuty(unsigned nr1);
	void updatePos(unsigned cc);

public:
	DutyUnit();
	void event();
	bool isHighState() const { return high; }
	void nr1Change(unsigned newNr1, unsigned cc);
	void nr3Change(unsigned newNr3, unsigned cc);
	void nr4Change(unsigned newNr4, unsigned cc);
	void reset();
	void saveState(SaveState::SPU::Duty &dstate, unsigned cc);
	void loadState(const SaveState::SPU::Duty &dstate, unsigned nr1, unsigned nr4, unsigned cc);
	void resetCounters(unsigned oldCc);
	void killCounter();
	void reviveCounter(unsigned cc);

	void loadOrSave(loadsave& state);

	//intended for use by SweepUnit only.
	unsigned getFreq() const { return 2048 - (period >> 1); }
	void setFreq(unsigned newFreq, unsigned cc);
};

class DutyMasterDisabler : public MasterDisabler {
	DutyUnit &dutyUnit;
public:
	DutyMasterDisabler(bool &m, DutyUnit &dutyUnit) : MasterDisabler(m), dutyUnit(dutyUnit) {}
	void operator()() { MasterDisabler::operator()(); dutyUnit.killCounter(); }
};

}

#endif
