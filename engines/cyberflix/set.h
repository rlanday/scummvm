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
 * A PANORAMA FRAME TABLE is a data resource: uint32 entry count, then that many
 * 0x3c-byte records, each a camera/transform matrix plus, at record +0x30, a
 * uint32 BACKGROUND FRAME resource id. Those frames are full-screen inter-codec
 * keyframes (info tag high half 0x0200, the same codec as MOV video and stage
 * backgrounds), so each is decodable standalone via decodeFrame.
 *
 * Rendering a room is therefore: pick a scene, pick a panorama table (A/B) and
 * an angle, resolve that record's frame id, decode it, and apply the set clut.
 * Panorama navigation, hotspots and the scene behavior script come later.
 */
class Set {
public:
	Set() {}
	~Set() {}

	/** Master-header, scene-record and panorama-record field offsets. */
	enum {
		kMasterWidthOffset = 0x084,
		kMasterHeightOffset = 0x086,
		kSceneTableIdOffset = 0x060,

		kSceneRecordStride = 0x2a,
		kSceneViewDirOffset = 0x06,
		kScenePanoramaAOffset = 0x0a,
		kScenePanoramaBOffset = 0x0e,
		kSceneScriptOffset = 0x12,
		kSceneNameOffset = 0x16,

		kPanoramaCountOffset = 0x00,
		kPanoramaRecordStride = 0x3c,
		kPanoramaFrameIdOffset = 0x30
	};

	/**
	 * Load and parse the set file @p name (a DATA/ basename, e.g. "bedsit1.set").
	 * Returns false if the file is missing, not a valid container, or lacks a
	 * well-formed master header / scene table. Replaces any previously loaded set.
	 */
	bool open(const Common::String &name);

	bool isOpen() const { return _master >= 0; }
	const Common::String &name() const { return _name; }
	uint16 width() const { return _width; }
	uint16 height() const { return _height; }
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
	 * Decode scene @p scene's background for panorama @p table (0 = A, 1 = B) at
	 * camera @p angle into @p out. Each panorama frame is a standalone keyframe,
	 * so no inter-frame replay is needed. Returns false on a bad index or a
	 * malformed frame.
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
	// _fileData outlives _archive: the archive's stream and every frame pointer
	// reference into this buffer.
	Common::Array<byte> _fileData;
	Archive _archive;
	int _master = -1;       ///< Archive index of the master-header resource.
	int _sceneTable = -1;   ///< Archive index of the scene-table resource.
	uint32 _sceneCount = 0;
	uint16 _width = 0;
	uint16 _height = 0;
};

} // End of namespace Cyberflix

#endif // CYBERFLIX_SET_H
