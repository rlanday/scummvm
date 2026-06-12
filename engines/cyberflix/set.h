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

#ifndef CYBERFLIX_SET_H
#define CYBERFLIX_SET_H

#include "common/array.h"
#include "common/str.h"

#include "cyberflix/archive.h"
#include "cyberflix/image.h"

namespace Cyberflix {

/**
 * A CyberFlix "set" (a @c .SET room file under DATA/): one navigable room of
 * the game, parsed from the same LPPALPPA container as everything else.
 * Reversed from the native scene renderer the boot/stage scripts reach through
 * @c sendtoscene (opcode 0x2f02 -> dispatch case 0x21 -> TI.EXE FUN_004311e0 ->
 * FUN_00431200, sharing the stage render path FUN_0040b690/FUN_0040b7a0). Full
 * RE writeup in files/decomp/stage-notes.md ("SET SCENE / VIEW / PANORAMA").
 *
 * A set's resource 0 is its master header (info tag @c 0x00040000), addressed
 * through the same "record+8" engine-base frame as stages, laid out:
 *   +0x084 uint16 width        (== 512)
 *   +0x086 uint16 height       (== 264)
 *   +0x060 uint32 sceneTableId (resource id of the scene table)
 *
 * The SCENE TABLE is a plain data resource: a tight array (no count header) of
 * 0x2a-byte records, one per scene:
 *   +0x00 uint32 hash
 *   +0x04 uint16 0xfffc
 *   +0x06 uint32 viewDirId    (object/View directory: hotspot lists per angle)
 *   +0x0a uint32 panoramaAId  (panorama frame table A)
 *   +0x0e uint32 panoramaBId  (panorama frame table B)
 *   +0x12 uint32 scriptId     (scene behavior script, info 0x0fa1)
 *   +0x16 Pascal name         (e.g. "Scene1")
 *
 * A PANORAMA FRAME TABLE is the resource a scene record's panorama field points
 * at. In the engine-base frame the runtime addresses it through, it is:
 *   +0x04 uint32 entryCount
 *   +0x0c records, @c entryCount of them, stride 0x3c (TI.EXE FUN_00442970
 *         indexes @c base+0xc+frame*0x3c), each a camera/transform matrix plus,
 *         at record +0x2c, a uint32 BACKGROUND FRAME resource id.
 * (Equivalently, from the raw payload pointer: count @ +0x00, first record @
 * +0x04, frame id @ record +0x30 -- the framing this class uses; both resolve
 * to the same absolute frame-id dword.)
 *
 * The panorama is a CONTINUOUS CYCLIC DELTA-ANIMATION, not random-access
 * keyframes: FUN_00442970 advances/wraps a frame index and applies each frame
 * onto the retained framebuffer via the inter-frame decoder FUN_00423600. Frame
 * 0 is the cold-start keyframe; every later angle is inter-coded against the
 * previous one. There is no per-angle keyframe flag. To show an arbitrary angle
 * from cold, replay frames 0..angle through a FrameSequence (decoding a delta
 * angle standalone leaves its "copy from previous" pixels unwritten -> garbage).
 *
 * Rendering a room is therefore: pick a scene, pick a panorama table (A/B),
 * replay frames 0..angle, and apply the set clut. Panorama navigation, hotspots
 * and the behavior script come later.
 */
class Set {
public:
	Set() {}
	~Set() {}

	/** Master-header, scene-record and panorama-record field offsets. */
	enum {
		kMasterNameOffset = 0x070,
		// Viewport rect the runtime copies into its set state (FUN_004307f0
		// param_2[0x11/0x12/0x0f/0x10] <- B+0x80/0x82/0x84/0x86) and turns
		// into the QuickDraw draw rect {top, left, top+h, left+w} in
		// FUN_00441f40. Every Titanic .SET has {0, 0, 512, 264}: the panorama
		// occupies the TOP of the 512x384 screen; the bottom 120 px
		// (384 - 0x78, see FUN_00449150) belong to the inventory bar.
		kMasterViewLeftOffset = 0x080,
		kMasterViewTopOffset = 0x082,
		kMasterWidthOffset = 0x084,
		kMasterHeightOffset = 0x086,
		kSceneTableIdOffset = 0x060,
		kMasterDefaultSceneOffset = 0xa0e,
		kMasterDefaultViewOffset = 0xa1e,

		kSceneRecordStride = 0x2a,
		kSceneViewDirOffset = 0x06,
		kScenePanoramaAOffset = 0x0a,
		kScenePanoramaBOffset = 0x0e,
		kSceneScriptOffset = 0x12,
		kSceneNameOffset = 0x16,

		kPanoramaCountOffset = 0x00,
		kPanoramaRecordStride = 0x3c,
		kPanoramaFrameIdOffset = 0x30,

		// View directory (scene rec +0x06), offsets in our payload frame
		// (TI reads count @base+0x30, records @base+0x34 with base=payload-4;
		// FUN_00433960/FUN_004425e0). Per record: pascal name @+0x1e
		// ('View14'), s16 heading @+0x08 (256-unit circle, FUN_00426250).
		kViewDirCountOffset = 0x2c,
		kViewDirRecordsOffset = 0x30,
		kViewRecordStride = 0x2e,
		kViewHeadingOffset = 0x08,
		kViewNameOffset = 0x1e
	};

	/**
	 * Load and parse the set file @p name (a DATA/ basename, e.g. "bedsit1.set").
	 * Returns false if the file is missing, not a valid container, or lacks a
	 * well-formed master header / scene table. Replaces any previously loaded set.
	 */
	bool open(const Common::String &name);

	bool isOpen() const { return _master >= 0; }
	const Common::String &name() const { return _name; }

	/**
	 * The set's embedded name from the master header (pascal @ +0x070, e.g.
	 * "bedsit1" -- no ".set" extension). TI.EXE copies it into the set record
	 * (FUN_004307f0, FUN_0041af90 from header+0x70) and currentset() returns
	 * it; scripts (setupsound, themetype, ...) switch on this form.
	 */
	const Common::String &setName() const { return _setName; }

	/** Default scene name from the master header (@ +0xa0e), used by
	 *  opensetfile when no scene argument is given (FUN_004307f0). */
	const Common::String &defaultScene() const { return _defaultScene; }

	/** Default view name from the master header (@ +0xa1e), used by
	 *  opensetfile when no view argument is given (FUN_004307f0). */
	const Common::String &defaultView() const { return _defaultView; }

	uint16 width() const { return _width; }
	uint16 height() const { return _height; }

	/** Viewport origin from the master header (B+0x80/0x82); the panorama is
	 *  drawn at this screen position (FUN_00441f40), {0, 0} in every set. */
	int16 viewLeft() const { return _viewLeft; }
	int16 viewTop() const { return _viewTop; }
	uint32 sceneCount() const { return _sceneCount; }

	/** Scene @p index's name, or empty if out of range. */
	Common::String sceneName(uint32 index) const;
	/** Index of the scene named @p name (case-insensitive), or -1. */
	int findScene(const Common::String &name) const;

	/**
	 * Number of camera angles in scene @p scene's panorama table @p table
	 * (0 = A, 1 = B), or 0 if either index is out of range.
	 */
	uint32 angleCount(uint32 scene, uint32 table) const;

	/**
	 * Index of the view named @p name (case-insensitive) in scene @p scene's
	 * view directory, or -1. Mirrors the name scan of TI.EXE FUN_004425e0 /
	 * FUN_00433960 (records stride 0x2e, pascal name @+0x1e).
	 */
	int findView(uint32 scene, const Common::String &name) const;

	/**
	 * The panorama angle (record index in @p table) whose record is tagged
	 * with view index @p viewIdx, or -1. Mirrors the camera-set scan of
	 * TI.EXE FUN_004425e0: each panorama record carries the view index it
	 * faces (u32 @+0x38 in TI's record frame; -1 = between views).
	 */
	int angleForView(uint32 scene, uint32 table, int viewIdx) const;

	/**
	 * Render scene @p scene's background for panorama @p table (0 = A, 1 = B) at
	 * camera @p angle into @p out. The panorama is a cyclic delta-animation, so
	 * frames 0..angle are replayed onto a retained buffer (the engine's cold-start
	 * state); see the class doc. Returns false on a bad index or malformed frame.
	 */
	bool renderScene(uint32 scene, uint32 table, uint32 angle, FrameImage &out);

	/** Expand the set's embedded palette into @p rgb (256*3, R,G,B). */
	bool loadSetPalette(byte *rgb) const;

private:
	/** Engine-base pointer (record+8) of resource @p index, or nullptr. */
	const byte *engineBase(uint32 index) const;
	/** Payload pointer (record+12) of resource @p index, or nullptr. */
	const byte *payload(uint32 index) const;
	/** Archive index of the resource whose id is @p id, or -1. */
	int resourceIndexById(uint32 id) const;
	/** Scene record pointer for @p scene, or nullptr if out of range. */
	const byte *sceneRecord(uint32 scene) const;
	/** Panorama table payload for scene @p scene / table @p table, or nullptr. */
	const byte *panoramaTable(uint32 scene, uint32 table, uint32 &count) const;

	Common::String _name;
	Common::String _setName;      ///< Embedded master-header name (+0x070).
	Common::String _defaultScene; ///< Master-header default scene (+0xa0e).
	Common::String _defaultView;  ///< Master-header default view (+0xa1e).
	// _fileData outlives _archive: the archive's stream and every frame pointer
	// reference into this buffer.
	Common::Array<byte> _fileData;
	Archive _archive;
	int _master = -1;       ///< Archive index of the master-header resource.
	int _sceneTable = -1;   ///< Archive index of the scene-table resource.
	uint32 _sceneCount = 0;
	uint16 _width = 0;
	uint16 _height = 0;
	int16 _viewLeft = 0;  ///< Viewport origin x (master header +0x080).
	int16 _viewTop = 0;   ///< Viewport origin y (master header +0x082).
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_SET_H
