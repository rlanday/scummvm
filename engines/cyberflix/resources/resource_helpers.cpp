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

#include "common/debug.h"
#include "common/file.h"
#include "common/memstream.h"
#include "common/path.h"

#include "cyberflix/resource_helpers.h"

namespace Cyberflix {

bool openArchiveFile(const Common::String &name, const char *kind,
		Common::Array<byte> &fileData, Archive &archive) {
	Common::File file;
	if (!file.open(Common::Path(name))) {
		warning("Cyberflix: could not open %s '%s'", kind, name.c_str());
		return false;
	}

	uint32 size = (uint32)file.size();
	fileData.resize(size);
	if (file.read(fileData.begin(), size) != size) {
		warning("Cyberflix: could not read %s '%s'", kind, name.c_str());
		fileData.clear();
		return false;
	}
	file.close();

	if (!archive.open(new Common::MemoryReadStream(fileData.begin(), size, DisposeAfterUse::NO), name)) {
		warning("Cyberflix: '%s' is not a valid %s container", name.c_str(), kind);
		return false;
	}
	return true;
}

} // End of namespace Cyberflix
