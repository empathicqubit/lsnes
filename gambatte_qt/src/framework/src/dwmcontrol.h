/***************************************************************************
 *   Copyright (C) 2011 by Sindre Aamås                                    *
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
#ifndef DWMCONTROL_H_
#define DWMCONTROL_H_

#include <QWidget>
#include <vector>

class BlitterWidget;

class DwmControl {
public:
	explicit DwmControl(std::vector<BlitterWidget *> const &blitters);
	void setDwmTripleBuffer(bool enable);
	void hideEvent();
	void showEvent();
	/** @return compositionChange */
	bool winEvent(void const *msg);
	void tick();
	void hwndChange(BlitterWidget *blitter);

	static bool hasDwmCapability();
	static bool isCompositingEnabled();

private:
	std::vector<BlitterWidget *> const blitters_;
	int refreshCnt_;
	bool tripleBuffer_;
};

class DwmControlHwndChange {
public:
	explicit DwmControlHwndChange(DwmControl &dwmc) : dwmc_(dwmc) {}
	void operator()(BlitterWidget *blitter) const { dwmc_.hwndChange(blitter); }

private:
	DwmControl &dwmc_;
};

#endif /* DWMCONTROL_H_ */
