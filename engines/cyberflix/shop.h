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

#ifndef CYBERFLIX_SHOP_H
#define CYBERFLIX_SHOP_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/ptr.h"
#include "common/rect.h"
#include "common/str.h"

#include "cyberflix/archive.h"
#include "cyberflix/image.h"
#include "cyberflix/script.h"

namespace Cyberflix {

/**
 * A CyberFlix "shop" (a @c .SHP file under DATA/): a container of PROPS —
 * screen- or world-space sprites with named "shapes" (views), each shape
 * holding pose/angle-selected cels. The inventory bar's HELP button and life
 * preserver are props of HOUSE.SHP; the 28 carryable items are INVEN.SHP.
 *
 * Reversed from TI.EXE: openshopfile FUN_00428450 (master parse FUN_00428610,
 * prop record init FUN_00428750), the prop builtins (propxy FUN_0042a370,
 * propvisible FUN_00429d00, propview FUN_004293a0, propdist FUN_004295c0,
 * propowner FUN_00428d40, countprops FUN_0042b4f0, indextoprop FUN_0042b550),
 * the shape/cell resolver FUN_0042bed0 and the display-item builder
 * FUN_0042bb90.
 *
 * Master header (engine base = record+8; located by info tag 0x40000 — NOT by
 * resource id: HOUSE.SHP has an empty placeholder with rid 0 first):
 *   +0x924 uint32 shop script res id
 *   +0x928 Pascal shop name
 *   +0x938 uint32 prop count
 *   +0x93c prop table, stride 0x10, dword0 = prop-master res id
 *
 * Prop master: +0x26 u32 script res id; Pascal name/set/scene @ +0x2a/+0x3a/
 * +0x4a; +0x5a u32 shape count; +0x5e shape table stride 0x20 {u32 shapeRes;
 * Pascal name @ +0x10}.
 *
 * Shape resource: pose-id table @ +0x2e (u16 each), pose count @ +0x70, cell
 * count @ +0x72, cell table @ +0x76 stride 0x2c {u32 frameRes; u16 cellId @
 * +0x08 (== poseId-1); i16 rect {t,l,b,r} @ +0x12; i16 regV/regH @ +0x1a/
 * +0x1c; i16 angle @ +0x28}.
 */
class Shop {
public:
	/** One selectable view of a prop (prop-master shape table entry). */
	struct Shape {
		uint32 resId = 0;
		Common::String name;
	};

	/**
	 * A prop and its runtime state, mirroring the original's 0x9e-byte record.
	 * Props start hidden in screen mode;
	 * scripts place/show them via propxy/propvisible/propview or switch them to
	 * world mode with propset/propxyz.
	 */
	struct Prop {
		Common::String name;        ///< record +0x4a (lowercased for lookup)
		Common::String setName;     ///< record +0x5e
		Common::String sceneName;   ///< record +0x6e
		uint32 masterResId = 0;     ///< record +0x0a
		uint32 scriptResId = 0;     ///< record +0x0e (own message handlers)
		Common::Array<Shape> shapes;

		bool visible = false;       ///< +0x00; FUN_00428750 inits 0
		uint16 mode = 0;            ///< +0x12; 0 = screen space (native init/propxy)
		int16 y = 0;                ///< +0x14 anchor V
		int16 x = 0;                ///< +0x16 anchor H
		int16 z = 0;                ///< propxyz world Z (world props pending)
		int16 angle = 0;            ///< +0x18 (propdeg)
		uint16 poseIndex = 0;       ///< +0x20 current pose-table index
		uint16 poseCount = 0;       ///< +0x22 pose count for current shape
		bool poseAdvancePending = true; ///< Keep pose 0 for the first compositor pass after a view change.
		int16 depth = -1;           ///< +0x26 (negative = screen-clipped item)
		int32 scale = 1000;         ///< +0x28
		int32 zClip = 0;            ///< propzclip distance (world props pending)
		int32 value = 0;            ///< +0x46 (propvalue)
		Common::String shapeName;   ///< +0x7e current view (first shape at init)
		Common::String owner;       ///< +0x8c (player == "frank")

		Common::SharedPtr<Script> script; ///< Parsed prop script, or null.
	};

	enum {
		kMasterScriptOffset = 0x924,
		kMasterNameOffset = 0x928,
		kMasterPropCountOffset = 0x938,
		kMasterPropTableOffset = 0x93c,
		kMasterPropStride = 0x10,

		kPropScriptOffset = 0x26,
		kPropNameOffset = 0x2a,
		kPropSetOffset = 0x3a,
		kPropSceneOffset = 0x4a,
		kPropShapeCountOffset = 0x5a,
		kPropShapeTableOffset = 0x5e,
		kPropShapeStride = 0x20,
		kPropShapeNameOffset = 0x10,

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

	struct WorldCamera {
		int16 heading = 0;
		int16 cameraX = 0;
		int16 cameraY = 0;
		int16 cameraZ = 0;
		int16 baseZ = 0;
		int16 nearPlane = 0;
		int16 farPlane = 0;
		int16 viewportLeft = 0;
		int16 viewportTop = 0;
		int16 viewportRight = 0;
		int16 viewportBottom = 0;
		int16 centerX = 0;
		int16 centerY = 0;
		int16 focal = 0;
	};

	struct ShapePoseResult {
		bool valid = false;
		uint16 poseCount = 0;
	};

	struct PropCellResult {
		bool valid = false;
		Common::SharedPtr<CelImage> cel;
		Common::Rect cellRect;
		int16 regV = 0;
		int16 regH = 0;
		int16 cellScale = 0;
	};

	struct PropRenderResult {
		bool valid = false;
		Common::SharedPtr<CelImage> cel;
		Common::Rect rect;
		int16 depth = 0;
		int16 depthBucket = 0;
	};

	Shop() {}

	/**
	 * Load and parse the shop file @p name (a DATA/ basename, e.g.
	 * "house.shp"). Builds the prop list and parses the shop and prop scripts.
	 * Mirrors TI.EXE FUN_00428450/FUN_00428610/FUN_00428750.
	 */
	bool open(const Common::String &name);

	bool isOpen() const { return _master >= 0; }
	const Common::String &name() const { return _name; }

	/** The shop's own script (message handlers like initprops), or null. */
	const Script *shopScript() const { return _script.get(); }

	uint32 propCount() const { return _props.size(); }
	Prop &prop(uint32 i) { return _props[i]; }
	const Prop &prop(uint32 i) const { return _props[i]; }
	void advancePropPoses();

	/** Find a prop by case-insensitive name, or nullptr. */
	Prop *findProp(const Common::String &name);
	bool addPropInstance(const Prop &source, const Common::String &newName);

	/** True if @p prop's master lists a shape named @p shape (propview's
	 *  validation, FUN_0042c0c0). Returns its pose count via the shape res. */
	ShapePoseResult shapePoseCount(const Prop &prop, const Common::String &shape) const;

	/**
	 * Resolve @p prop's current shape/pose/angle to a decoded cel and its
	 * screen rectangle (top = y - regV, left = x - regH; FUN_0042bed0 +
	 * FUN_0042bb90). Returns an invalid result if the prop has no drawable cell.
	 */
	PropRenderResult renderProp(const Prop &prop) const;
	PropRenderResult renderWorldProp(const Prop &prop, const WorldCamera &camera,
			const Common::String &setName) const;

private:
	const byte *engineBase(uint32 index) const;
	int resourceIndexById(uint32 id) const;
	/** Read the Pascal string at @p p (bounded by the file buffer). */
	Common::String pascalString(const byte *p) const;
	Common::SharedPtr<CelImage> celResource(uint32 resId) const;
	PropCellResult resolvePropCel(const Prop &prop, int angle) const;

	Common::String _name;
	Common::Array<byte> _fileData; ///< Outlives _archive (streams point in).
	Archive _archive;
	int _master = -1;
	Common::ScopedPtr<Script> _script;
	Common::Array<Prop> _props;
	mutable Common::HashMap<uint32, Common::SharedPtr<CelImage> > _celCache;
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_SHOP_H
