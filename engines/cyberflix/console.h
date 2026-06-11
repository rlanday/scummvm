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

#ifndef CYBERFLIX_CONSOLE_H
#define CYBERFLIX_CONSOLE_H

#include "gui/debugger.h"

namespace Cyberflix {

class CyberflixEngine;

class Console : public GUI::Debugger {
public:
	explicit Console(CyberflixEngine *engine);
	~Console() override = default;

private:
	// Dumps the LPPALPPA container header of a given asset file.
	bool cmdDumpArchive(int argc, const char **argv);

	// Disassembles a script resource (info tag 0x0FA1).
	bool cmdDisasm(int argc, const char **argv);

	// Executes a script resource on the VM harness with tracing.
	bool cmdVmTrace(int argc, const char **argv);
	bool cmdVmRun(int argc, const char **argv);

	// Decodes a SHP/SET cel resource and blits it to the screen for inspection.
	bool cmdShowShape(int argc, const char **argv);

	// Decodes a full-screen frame (MOV keyframe / SET background) at a byte
	// offset within a file and blits it to the screen for inspection.
	bool cmdShowFrame(int argc, const char **argv);

	// Plays a MOV's frame sequence up to a given index into a persistent
	// framebuffer and blits the result (exercises inter-frame decoding).
	bool cmdShowMovie(int argc, const char **argv);

	// Opens a STG deck and renders one of its nodes (exercises the Stage
	// parser + node renderer used by openstagefile/sendtostage).
	bool cmdShowNode(int argc, const char **argv);

	// Opens a SET room and renders one scene's panorama background (exercises
	// the Set parser + scene renderer used by sendtoscene/changeset).
	bool cmdShowSet(int argc, const char **argv);
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_CONSOLE_H
