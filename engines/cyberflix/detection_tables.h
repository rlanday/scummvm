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

#ifndef CYBERFLIX_DETECTION_TABLES_H
#define CYBERFLIX_DETECTION_TABLES_H

namespace Cyberflix {

static const CyberflixGameDescription gameDescriptions[] = {
	// Titanic: Adventure Out of Time - English Windows (2-CD retail)
	// Detected via DATA/BOOTFILE; the engine validates the extracted
	// TITANIC1/TITANIC2 sibling-disc layout at startup.
	{
		{
			"titanicaoot",
			"",
			{
				{"BOOTFILE", GAME_BOOTFILE, "9a3eb0be9eb91d5c03d8c6a793808b9e", 95488},
				AD_LISTEND
			},
			Common::EN_ANY,
			Common::kPlatformWindows,
			ADGF_UNSTABLE,
			GUIO2(GUIO_NOMIDI, GAMEOPTION_ENHANCED_PANORAMA_SETTLING)
		},
		GType_Titanic
	},

	{ AD_TABLE_END_MARKER, 0 }
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_DETECTION_TABLES_H
