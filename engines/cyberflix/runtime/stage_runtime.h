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

#ifndef CYBERFLIX_RUNTIME_STAGE_RUNTIME_H
#define CYBERFLIX_RUNTIME_STAGE_RUNTIME_H

#include "common/ptr.h"

#include "cyberflix/image.h"
#include "cyberflix/stage.h"

namespace Cyberflix {

class StageRuntime {
public:
	Common::SharedPtr<Stage> &stage() { return _stage; }
	const Common::SharedPtr<Stage> &stage() const { return _stage; }
	bool &visible() { return _visible; }
	bool visible() const { return _visible; }
	int &node() { return _node; }
	int node() const { return _node; }
	FrameImage &shellFrameData() { return _shellFrame; }
	const FrameImage &shellFrameData() const { return _shellFrame; }
	bool &shellFrameValid() { return _shellFrameValid; }
	bool shellFrameValid() const { return _shellFrameValid; }

	void clearShellFrame() {
		_shellFrame.width = 0;
		_shellFrame.height = 0;
		_shellFrame.pixels.clear();
		_shellFrameValid = false;
	}

	void reset() {
		_stage.reset();
		_visible = false;
		_node = 0;
		clearShellFrame();
	}

private:
	Common::SharedPtr<Stage> _stage;
	bool _visible = false;
	int _node = 0;
	FrameImage _shellFrame;
	bool _shellFrameValid = false;
};

} // End of namespace Cyberflix

#endif
