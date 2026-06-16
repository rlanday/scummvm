/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef CYBERFLIX_DETECTION_H
#define CYBERFLIX_DETECTION_H

#include "engines/advancedDetector.h"

#define GAMEOPTION_FONT_ANTIALIASING GUIO_GAMEOPTIONS1
#define CYBERFLIX_OPTION_FONT_ANTIALIASING "enable_font_antialiasing"

namespace Cyberflix {

enum CyberflixGameType {
	GType_Titanic = 0
};

enum CyberflixGameFileTypes {
	GAME_BOOTFILE   = 1 << 0,    // DATA/BOOTFILE: boot script + globals
	GAME_EXECUTABLE = 1 << 1     // TITANIC.EXE
};

struct CyberflixGameDescription {
	AD_GAME_DESCRIPTION_HELPERS(desc);

	ADGameDescription desc;

	int gameType;
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_DETECTION_H
