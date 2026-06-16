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

#ifndef CYBERFLIX_CAST_H
#define CYBERFLIX_CAST_H

#include "common/array.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/ptr.h"
#include "common/str.h"

#include "cyberflix/archive.h"
#include "cyberflix/script.h"
#include "cyberflix/shop.h"

namespace Cyberflix {

/**
 * A CyberFlix cast file (.CST under DATA/): the global actor list and shared
 * actor script. Reversed from TI.EXE FUN_0041f1c0/FUN_0041f370/FUN_0041f4b0:
 * the master header uses the same record+8 offsets as SHOP, with a script
 * resource at +0x924, a Pascal file name at +0x928, an actor count at +0x938,
 * and a 0x10-byte table whose first dword is the actor-master resource id.
 */
class Cast {
public:
	struct Actor {
		struct Shape {
			uint32 resId = 0;
			Common::String name;
		};

		Common::String name;      ///< Runtime lookup name, master +0x2a.
		Common::String setName;   ///< Current SET/location string, record +0x60.
		Common::String sceneName; ///< Current star/scene string, record +0x70.
		Common::String shapeName; ///< Current pose/shape string, record +0x80.
		Common::String owner;     ///< Actor owner string, record +0x90.

		uint32 masterResId = 0;
		uint32 scriptResId = 0;
		Common::SharedPtr<Script> script;

		bool visible = false;
		int16 x = 0;
		int16 y = 0;
		int16 z = 0;
		int16 angle = 0;
		int32 scale = 1000;
		int32 zClip = 0;
		int32 speed = 0;
		int32 turn = 0;
		int32 value = 0;
		Common::Array<Shape> shapes;
	};

	enum {
		kMasterScriptOffset = 0x924,
		kMasterNameOffset = 0x928,
		kMasterActorCountOffset = 0x938,
		kMasterActorTableOffset = 0x93c,
		kMasterActorStride = 0x10,

		kActorScriptOffset = 0x26,
		kActorNameOffset = 0x2a,
		kActorSetOffset = 0x3a,
		kActorSceneOffset = 0x4a,
		kActorShapeCountOffset = 0x5a,
		kActorShapeTableOffset = 0x5e,
		kActorShapeStride = 0x20,
		kActorShapeNameOffset = 0x10,

		kShapePoseTableOffset = 0x2e,
		kShapePoseCountOffset = 0x70,
		kShapeCellCountOffset = 0x72,
		kShapeCellTableOffset = 0x76,
		kShapeCellStride = 0x2c,
		kCellFrameResOffset = 0x00,
		kCellIdOffset = 0x08,
		kCellRectOffset = 0x12,
		kCellRegVOffset = 0x1a,
		kCellRegHOffset = 0x1c,
		kCellAngleOffset = 0x28,
		kCellScaleOffset = 0x2a
	};

	Cast() {}

	/** Load and parse @p name (a DATA/ basename, e.g. "gang.cst"). */
	bool open(const Common::String &name);

	bool isOpen() const { return _master >= 0; }
	const Common::String &name() const { return _name; }
	const Script *castScript() const { return _script.get(); }

	uint32 actorCount() const { return _actors.size(); }
	Actor &actor(uint32 i) { return *_actors[i]; }
	const Actor &actor(uint32 i) const { return *_actors[i]; }

	/** Find an actor by case-insensitive runtime name, or null. */
	Common::SharedPtr<Actor> findActor(const Common::String &name);

	bool renderWorldActor(const Actor &actor, const Shop::WorldCamera &camera,
			const Common::String &setName, CelImage &cel, Common::Rect &rect,
			int16 &depth) const;

private:
	const byte *engineBase(uint32 index) const;
	int resourceIndexById(uint32 id) const;
	Common::String pascalString(const byte *p) const;
	bool resolveActorCel(const Actor &actor, int angle, CelImage &cel,
			Common::Rect &cellRect, int16 &regV, int16 &regH,
			int16 &cellScale) const;

	Common::String _name;
	Common::Array<byte> _fileData;
	Archive _archive;
	int _master = -1;
	Common::ScopedPtr<Script> _script;
	Common::Array<Common::SharedPtr<Actor> > _actors;
	// Actor records are immutable while a CST is open; this keeps hot
	// sendtoactor/property lookups from rescanning the actor table each time.
	Common::HashMap<Common::String, uint32> _actorIndexByName;
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_CAST_H
