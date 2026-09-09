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

#ifndef DREAMFACTORY_SHOP_H
#define DREAMFACTORY_SHOP_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/ptr.h"
#include "common/rect.h"
#include "common/str.h"

#include "dreamfactory/archive.h"
#include "dreamfactory/image.h"
#include "dreamfactory/resource_helpers.h"
#include "dreamfactory/script.h"

namespace DreamFactory {

/**
 * Loads a DreamFactory "shop": a .SHP resource file containing a collection
 * of named graphical objects called "props", their images and optional script
 * handlers. "Shop" is an engine term, not necessarily a store in the story.
 * In Titanic, HOUSE.SHP supplies interface props such as the inventory bar's
 * HELP button and life preserver; INVEN.SHP supplies the 28 carryable items.
 *
 * A prop combines its available artwork with runtime state: its position,
 * visibility, owner and selected appearance. Screen-space props use screen
 * coordinates, as interface elements do. World-space props use coordinates
 * in a SET room and are projected onto the screen using the room's camera.
 * Loading a shop does not show every prop: props start hidden, and scripts
 * place them and select their appearance with propxy/propvisible/propview.
 * The shop and individual props can each have a script; prop messages search
 * the prop's script, then the shop's script, then the GLOBAL library.
 *
 * Artwork is selected in three steps:
 * - A "shape" is a named appearance or animation belonging to a prop, not a
 *   geometric outline. propview() selects a shape by name; HOUSE.SHP's bag
 *   animation is an example of an appearance with multiple animation steps.
 * - A "pose" is one step in that shape's sequence. The prop's poseIndex
 *   selects an entry in the shape's pose-ID table; advancing the index plays
 *   the animation, wrapping to the start after the last entry.
 * - A "cell" is a record describing artwork for a pose at a particular angle.
 *   Among cells for the selected pose, choose the closest angle. That cell
 *   references a "cel": the actual bitmap to decode and draw. Its bounds and
 *   registration point describe how to position the image at the prop's anchor.
 *
 * open() builds the Prop/Shape arrays and parses scripts. Shape resources are
 * consulted when selecting artwork; cel bitmaps are decoded on demand and
 * cached. The .SHP file is normally under DATA/ on retail CDs, but may be in
 * the merged asset directory of the Steam release.
 *
 * File-format scope:
 * The following describes the shared Macintosh/Windows Titanic .SHP format,
 * from the outer container down to scripts and image data. Verified by
 * comparing the HFS data forks against the Windows files on both retail CD
 * images: all 30 .SHP files match byte-for-byte and have empty resource forks.
 * The installed Macintosh HOUSE.SHP, INVEN.SHP and TOUR.SHP also match their
 * Windows counterparts. No platform-specific byte swapping is needed for
 * these files; this does not establish support for the Macintosh executable
 * or for other releases and DreamFactory versions.
 *
 * It documents the fields we interpret, not every byte: gaps in the layouts
 * contain metadata or fields whose meaning is not established here. Do not
 * assume they are zero, padding, or portable C++ struct layouts.
 *
 * Outer container (offsets from the start of the file):
 * - +0x00: uint32 little-endian magic 0x00010000.
 * - +0x04: uint32 declared file size, including the container header.
 * - +0x10: uint32 first-section size (recorded by Archive, not used by Shop).
 * - +0x14: uint32 resource-directory entry count, including empty entries.
 * - +0x20: eight literal signature bytes "LPPALPPA".
 * - +0x400: directory table, one uint32 little-endian absolute file offset
 *   per entry. Earlier metadata/preamble bytes are not interpreted by Shop.
 *   A zero directory offset represents an empty slot.
 *
 * Each non-empty directory entry points to a resource record:
 * - Record +0x00: uint32 resource ID.
 * - Record +0x04: uint32 payload length, excluding this 12-byte header.
 * - Record +0x08: uint32 info field (its meaning depends on resource type).
 * - Record +0x0c: payload bytes.
 * These header integers are little-endian. Follow directory offsets rather
 * than assuming records are contiguous; payload length excludes any bytes
 * between records. IDs are looked up through Archive, not used as array
 * indices. Its lookup chooses the first non-empty match if IDs are duplicated.
 *
 * Resources are connected by IDs, not necessarily adjacent in the file:
 * @code
 * shop master --+--> optional shop script
 *               +--> prop master --+--> optional prop script
 *                    (per prop)    +--> named shape resource
 *                                       +--> pose-ID table
 *                                       +--> cell records --> cel bitmaps
 * @endcode
 * Locate the shop master by info tag 0x40000. Follow its prop table, each
 * prop's shape table, and each shape's cell references to reach the artwork.
 * A zero shop/prop script ID means no script, even if resource ID 0 exists.
 *
 * Shop-specific resource layouts:
 * Offsets in the next three sections are hexadecimal bytes from the native
 * "engine base": record start + 8, at the info field, four bytes before the
 * payload exposed by Archive. engineView() supplies this base. Integer fields
 * are little-endian; Pascal strings start with a one-byte character count.
 * A resource ID refers to another record in the same .SHP archive, not a byte
 * offset or necessarily its directory index. Table stride means bytes per entry.
 *
 * Shop master header (located by info tag 0x40000, not resource ID 0;
 * HOUSE.SHP has an empty directory slot before the real master):
 * - +0x924: uint32 shop script resource ID.
 * - +0x928: Pascal shop name.
 * - +0x938: uint32 prop count.
 * - +0x93c: prop table, stride 0x10; each entry starts with a uint32 ID of
 *   the resource describing that prop (its "prop master").
 *
 * Prop master resource:
 * - +0x26: uint32 prop script resource ID.
 * - +0x2a/+0x3a/+0x4a: Pascal prop name, set name and scene name.
 * - +0x5a: uint32 shape count.
 * - +0x5e: shape table, stride 0x20; each entry has a uint32 shape resource
 *   ID at +0x00 and a Pascal shape name at +0x10, relative to that entry.
 *
 * Shape resource:
 * - +0x2e: pose-ID table (uint16 entries); +0x70: uint16 pose count.
 * - +0x72: uint16 cell count; +0x76: cell table, stride 0x2c.
 * - Within each cell: uint32 cel resource ID at +0x00; uint16 cell ID at
 *   +0x08 (matches pose ID minus one); four int16 bounds at +0x12 in
 *   top/left/bottom/right order; int16 vertical/horizontal registration
 *   offsets at +0x1a/+0x1c; int16 angle at +0x28 and scale at +0x2a.
 *   In screen mode, image top/left = prop y/x minus registration V/H.
 *
 * Script resource payload (decoded by Script, not by the shop parser):
 * The shop/prop script IDs refer to script resources (info tag 0x0fa1).
 * Instructions are eight-byte records: uint32 operandB, uint16 opcode,
 * uint16 operandA, all little-endian. Opcode 0 terminates the instruction
 * sequence; the string pool follows. String references use a uint32
 * displacement read starting at opcode + 2, so it spans operandA and the
 * low 16 bits of the following record's operandB. Add that displacement to
 * the opcode field's byte offset within the payload to locate a Pascal
 * string. Script retains the original payload for these lookups and builds
 * a separate array of decoded instructions; see Script for opcode semantics.
 *
 * Cel resource (decoded by decodeCel()):
 * Its info field packs height in the low 16 bits and width in the high 16
 * bits; these are dimensions, not a fixed type tag. Unlike the shop-specific
 * layouts above, the following offsets start at the payload (record +0x0c):
 * - +0x00/+0x02: int16 little-endian originX/originY, retained in CelImage.
 *   These are separate from the cell's registration offsets used by renderProp().
 * - +0x04 onward: one compressed scanline per image row, from top to bottom.
 *   Each starts with a uint16 little-endian byte length, excluding the length
 *   field itself, followed by that many bytes of pixel commands and operands.
 * For each command byte C, the pixel count is C >> 2 and the operation is C & 3:
 * - 0: copy those pixel positions from the preceding row (no extra operand).
 * - 1: leave that many pixels transparent (no extra operand).
 * - 2: repeat the next byte's palette index for that many pixels.
 * - 3: copy that many literal palette-index bytes from the stream.
 * Transparency is tracked separately from palette indices; index 0 is not
 * inherently transparent. A copy from a transparent preceding-row position
 * stays transparent; on the first row there is no preceding cel row to copy.
 * The decoder produces a pixel array and an opacity mask, not RGB pixels.
 * The active game palette supplies the colors; no per-cel palette is read.
 *
 * In-memory state is not another on-disk record format: Prop's visibility,
 * position, owner and current pose are initialized and changed by the runtime.
 * The native record offsets noted on Prop fields below refer to TI.EXE's
 * runtime structure, not offsets to read from the .SHP prop master.
 *
 * Reverse-engineering references in TI.EXE: openshopfile FUN_00428450,
 * master parsing FUN_00428610, prop initialization FUN_00428750; propxy
 * FUN_0042a370, propvisible FUN_00429d00, propview FUN_004293a0, propdist
 * FUN_004295c0, propowner FUN_00428d40, countprops FUN_0042b4f0 and indextoprop
 * FUN_0042b550; shape/cell selection FUN_0042bed0 and display-item construction
 * FUN_0042bb90.
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
	ResourceView engineView(uint32 index) const;
	int resourceIndexById(uint32 id) const;
	Common::SharedPtr<CelImage> celResource(uint32 resId) const;
	PropCellResult resolvePropCel(const Prop &prop, int angle) const;

	Common::String _name;
	ResourceFile _resourceFile;
	int _master = -1;
	Common::ScopedPtr<Script> _script;
	Common::Array<Prop> _props;
	mutable Common::HashMap<uint32, Common::SharedPtr<CelImage> > _celCache;
};

} // End of namespace DreamFactory

#endif // DREAMFACTORY_SHOP_H
