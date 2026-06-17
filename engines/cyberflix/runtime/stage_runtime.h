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

#include "common/array.h"
#include "common/ptr.h"
#include "common/str.h"

#include "cyberflix/image.h"
#include "cyberflix/stage.h"

namespace Cyberflix {

class CyberflixEngine;
struct Value;

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

	void openStageFile(CyberflixEngine &engine, const Common::String &name);
	void closeStageFile(CyberflixEngine &engine);
	void gotoFlat(CyberflixEngine &engine, const Value &flat);
	Common::String currentStage() const;
	bool stageVisible(const bool *newVisible);
	Common::String currentFlat() const;
	const FrameImage *stageShellFrame();

	void sendToStage(CyberflixEngine &engine, const Common::String &message, const Common::Array<Value> &args);
	Value sendToStageFx(CyberflixEngine &engine, const Common::String &message, const Common::Array<Value> &args);
	void sendToFlat(CyberflixEngine &engine, const Common::String &flat, const Common::String &message,
			const Common::Array<Value> &args);
	Value sendToFlatFx(CyberflixEngine &engine, const Common::String &flat, const Common::String &message,
			const Common::Array<Value> &args);
	void sendToButton(CyberflixEngine &engine, const Common::String &flat, const Common::String &button,
			const Common::String &message, const Common::Array<Value> &args);
	Value sendToButtonFx(CyberflixEngine &engine, const Common::String &flat, const Common::String &button,
			const Common::String &message, const Common::Array<Value> &args);

	void renderStageNode(CyberflixEngine &engine, int node, bool resetCursor = true);
	void repaintDirtyStageRects(CyberflixEngine &engine);
	bool pointInButton(const Common::String &flat, const Common::String &button, int32 packedPoint) const;

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
