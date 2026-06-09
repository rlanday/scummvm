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

#include "common/file.h"

#include "cyberflix/console.h"
#include "cyberflix/cyberflix.h"
#include "cyberflix/archive.h"

namespace Cyberflix {

Console::Console(CyberflixEngine *engine) : GUI::Debugger(), _engine(engine) {
	registerCmd("dumpArchive", WRAP_METHOD(Console, cmdDumpArchive));
}

bool Console::cmdDumpArchive(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Parses and lists the resources of an LPPALPPA container file.\n");
		debugPrintf("Usage: %s <filename> [count]   (e.g. BOOTFILE, CTL.STG, BEDSIT1.SET)\n", argv[0]);
		return true;
	}

	Common::File file;
	if (!file.open(argv[1])) {
		debugPrintf("Could not open '%s'\n", argv[1]);
		return true;
	}

	Archive archive;
	if (!archive.open(file.readStream(file.size()), argv[1])) {
		debugPrintf("'%s' is not a valid LPPALPPA container\n", argv[1]);
		return true;
	}

	debugPrintf("%s: %u resources, declared size %u bytes\n",
			archive.getName().c_str(), archive.getResourceCount(),
			archive.getDeclaredSize());

	uint32 count = archive.getResourceCount();
	uint32 limit = (argc >= 3) ? (uint32)atoi(argv[2]) : 16;
	debugPrintf("  idx        id      length     info       data@\n");
	for (uint32 i = 0; i < count && i < limit; ++i) {
		const Archive::Resource &res = archive.getResource(i);
		if (res.empty)
			debugPrintf("  [%4u]   <empty>\n", i);
		else
			debugPrintf("  [%4u] %6u  %#10x  %#010x  %#08x\n",
					i, res.id, res.length, res.info, res.dataOffset);
	}
	if (count > limit)
		debugPrintf("  ... %u more (pass a count to show more)\n", count - limit);
	return true;
}

} // End of namespace Cyberflix
