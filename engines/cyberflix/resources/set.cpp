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
#include "common/endian.h"
#include "common/ptr.h"

#include "cyberflix/set.h"
#include "cyberflix/resource_helpers.h"

namespace Cyberflix {

const byte *Set::engineBase(uint32 index) const {
	if (index >= _archive.getResourceCount())
		return nullptr;
	return resourceEngineBase(_fileData, _archive.getResource(index));
}

const byte *Set::payload(uint32 index) const {
	if (index >= _archive.getResourceCount())
		return nullptr;
	const Archive::Resource &res = _archive.getResource(index);
	if (res.empty || res.dataOffset > _fileData.size())
		return nullptr;
	return _fileData.begin() + res.dataOffset;
}

int Set::resourceIndexById(uint32 id) const {
	return Cyberflix::resourceIndexById(_archive, id);
}

Common::String Set::pascalString(const byte *p) const {
	return readPascalString(p, _fileData);
}

Common::SharedPtr<Script> Set::scriptByIdShared(uint32 id) const {
	int idx = resourceIndexById(id);
	if (idx < 0 || static_cast<uint32>(idx) >= _scripts.size())
		return Common::SharedPtr<Script>();
	return _scripts[static_cast<uint32>(idx)];
}

const byte *Set::sceneRecord(uint32 scene) const {
	if (scene >= _sceneCount || _sceneTable < 0)
		return nullptr;
	const byte *table = payload(static_cast<uint32>(_sceneTable));
	if (!table)
		return nullptr;
	const byte *rec = table + scene * kSceneRecordStride;
	if (rec + kSceneRecordStride > _fileData.end())
		return nullptr;
	return rec;
}

const byte *Set::viewDirectory(uint32 scene, uint32 &count) const {
	count = 0;
	const byte *rec = sceneRecord(scene);
	if (!rec)
		return nullptr;
	int idx = resourceIndexById(READ_LE_UINT32(rec + kSceneViewDirOffset));
	if (idx < 0)
		return nullptr;
	const byte *dir = payload(static_cast<uint32>(idx));
	if (!dir || dir + kViewDirRecordsOffset > _fileData.end())
		return nullptr;
	// Validate the file-supplied record count against the directory resource's
	// length (as the panorama/painting/star tables do), so a corrupt count
	// cannot make the lookups parse neighbouring resources' bytes as records.
	const uint32 length = _archive.getResource(static_cast<uint32>(idx)).length;
	uint32 c = READ_LE_UINT32(dir + kViewDirCountOffset);
	if (static_cast<uint64>(c) * kViewRecordStride + kViewDirRecordsOffset > length)
		return nullptr;
	count = c;
	return dir;
}

const byte *Set::viewRecord(uint32 scene, const Common::String &view) const {
	int viewIdx = findView(scene, view);
	if (viewIdx < 0)
		return nullptr;
	uint32 count = 0;
	const byte *dir = viewDirectory(scene, count);
	if (!dir || static_cast<uint32>(viewIdx) >= count)
		return nullptr;
	const byte *v = dir + kViewDirRecordsOffset + static_cast<uint32>(viewIdx) * kViewRecordStride;
	if (v + kViewRecordStride > _fileData.end())
		return nullptr;
	return v;
}

const byte *Set::paintingTable(const byte *viewRec, uint32 &count, uint32 &length) const {
	count = 0;
	length = 0;
	if (!viewRec || viewRec + kViewPaintingTableOffset + 4 > _fileData.end())
		return nullptr;
	uint32 tableId = READ_LE_UINT32(viewRec + kViewPaintingTableOffset);
	if (tableId == 0)
		return nullptr;
	int idx = resourceIndexById(tableId);
	if (idx < 0)
		return nullptr;
	const byte *table = engineBase(static_cast<uint32>(idx));
	if (!table || table + 8 > _fileData.end())
		return nullptr;
	const Archive::Resource &res = _archive.getResource(static_cast<uint32>(idx));
	length = res.length + 4; // engine-base frame includes the info dword
	uint32 c = READ_LE_UINT32(table);
	if (static_cast<uint64>(c) * 0x24 + 8 > length)
		return nullptr;
	count = c;
	return table;
}

const byte *Set::panoramaTable(uint32 scene, uint32 table, uint32 &count) const {
	count = 0;
	const byte *rec = sceneRecord(scene);
	if (!rec || table > kPanoramaTableB)
		return nullptr;
	uint32 panoId = READ_LE_UINT32(rec + (table == 0 ? kScenePanoramaAOffset : kScenePanoramaBOffset));
	int idx = resourceIndexById(panoId);
	if (idx < 0)
		return nullptr;
	const byte *pano = payload(static_cast<uint32>(idx));
	if (!pano || pano + kPanoramaCountOffset + 4 > _fileData.end())
		return nullptr;
	uint32 c = READ_LE_UINT32(pano + kPanoramaCountOffset);
	// Bound the record array against the resource payload.
	uint32 len = _archive.getResource(static_cast<uint32>(idx)).length;
	if (static_cast<uint64>(c) * kPanoramaRecordStride + 8 > len)
		return nullptr;
	count = c;
	return pano;
}

const byte *Set::transitionTable(uint32 transitionId, uint32 &count) const {
	count = 0;
	int idx = resourceIndexById(transitionId);
	if (idx < 0)
		return nullptr;
	const byte *transition = engineBase(static_cast<uint32>(idx));
	if (!transition || transition + 0x0c > _fileData.end())
		return nullptr;
	uint32 c = READ_LE_UINT32(transition + 0x04);
	const Archive::Resource &res = _archive.getResource(static_cast<uint32>(idx));
	if (static_cast<uint64>(c) * kPanoramaRecordStride + 0x0c > res.length + 4)
		return nullptr;
	count = c;
	return transition;
}

bool Set::applyFrameResource(uint32 frameId, FrameSequence &seq) const {
	// TI.EXE FUN_00442e90 treats a panorama/transition frame resource id of 0
	// as a no-op on the retained framebuffer; resource 0 is the SET master
	// header and must not be sent through the frame decoder.
	if (frameId == 0)
		return true;

	int idx = resourceIndexById(frameId);
	if (idx < 0) {
		warning("Cyberflix: set '%s' references missing frame res %u", _name.c_str(), frameId);
		return false;
	}
	const byte *frame = engineBase(static_cast<uint32>(idx));
	if (!frame)
		return false;
	uint32 frameLen = _archive.getResource(static_cast<uint32>(idx)).length + 4; // payload + info word
	if (seq.applyFrame(frame, frameLen) == 0) {
		warning("Cyberflix: set '%s' frame %u decode failed", _name.c_str(), frameId);
		return false;
	}
	return true;
}

bool Set::applyFrameResource(uint32 frameId, FrameSequence &seq, FrameImage &out) const {
	if (!applyFrameResource(frameId, seq))
		return false;
	seq.copyTo(out);
	return true;
}

int Set::findSceneByViewDirId(uint32 viewDirId) const {
	for (uint32 i = 0; i < _sceneCount; ++i) {
		const byte *rec = sceneRecord(i);
		if (rec && READ_LE_UINT32(rec + kSceneViewDirOffset) == viewDirId)
			return static_cast<int>(i);
	}
	return -1;
}

int Set::nearestViewForHeading(uint32 scene, int heading) const {
	uint32 count = 0;
	const byte *dir = viewDirectory(scene, count);
	if (!dir)
		return -1;
	int best = -1;
	int bestDist = 1000;
	for (uint32 i = 0; i < count; ++i) {
		const byte *v = dir + kViewDirRecordsOffset + i * kViewRecordStride;
		if (v + kViewRecordStride > _fileData.end())
			break;
		// Circular distance on the 256-unit compass. nativeAngleDistance()
		// masks both operands, so an out-of-range stored heading cannot
		// produce a negative distance that would always win the comparison.
		int dist = nativeAngleDistance(READ_LE_INT16(v + kViewHeadingOffset), heading);
		if (dist < bestDist) {
			bestDist = dist;
			best = static_cast<int>(i);
		}
	}
	return best;
}

bool Set::starXYZ(const Common::String &name, int16 &x, int16 &y, int16 &z) const {
	if (_starTable < 0)
		return false;
	const byte *table = engineBase(static_cast<uint32>(_starTable));
	if (!table || table + kStarTableRecordsOffset > _fileData.end())
		return false;
	const Archive::Resource &res = _archive.getResource(static_cast<uint32>(_starTable));
	const uint32 length = res.length + 4;
	const uint32 count = READ_LE_UINT32(table + kStarTableCountOffset);
	if (static_cast<uint64>(count) * kStarRecordStride + kStarTableRecordsOffset > length)
		return false;

	const byte *record = table + kStarTableRecordsOffset;
	for (uint32 i = 0; i < count; ++i, record += kStarRecordStride) {
		if (record + kStarRecordStride > _fileData.end())
			break;
		if (pascalEqualsIgnoreCase(record + kStarPrimaryNameOffset, _fileData.end(), name)) {
			x = READ_LE_INT16(record + kStarPrimaryXOffset);
			y = READ_LE_INT16(record + kStarPrimaryYOffset);
			z = READ_LE_INT16(record + kStarPrimaryZOffset);
			return true;
		}
		if (READ_LE_UINT32(record + kStarSecondaryFlagOffset) != 0 &&
				pascalEqualsIgnoreCase(record + kStarSecondaryNameOffset, _fileData.end(), name)) {
			x = READ_LE_INT16(record + kStarSecondaryXOffset);
			y = READ_LE_INT16(record + kStarSecondaryYOffset);
			z = READ_LE_INT16(record + kStarSecondaryZOffset);
			return true;
		}
	}
	return false;
}

bool Set::open(const Common::String &name) {
	_master = -1;
	_sceneTable = -1;
	_starTable = -1;
	_baseZ = 0; // FUN_004307f0 zeroes DAT_0046119a for the incoming set.
	_sceneCount = 0;
	_setScriptId = 0;
	_scripts.clear();
	_paintingScriptCacheValid = false;
	_paintingScriptCacheScript.reset();
	_width = _height = 0;
	_viewLeft = _viewTop = 0;
	_name = name;

	if (!openArchiveFile(name, "set", _fileData, _archive))
		return false;

	_master = findMasterHeaderIndex(_archive);
	if (_master < 0) {
		warning("Cyberflix: set '%s' has no master header", name.c_str());
		return false;
	}

	const byte *hdr = engineBase(static_cast<uint32>(_master));
	const uint64 masterLen = static_cast<uint64>(_archive.getResource(static_cast<uint32>(_master)).length) + 4;
	if (!hdr || masterLen < kMasterDefaultViewOffset + 1) {
		warning("Cyberflix: set '%s' master header truncated", name.c_str());
		_master = -1;
		return false;
	}
	_width = READ_LE_UINT16(hdr + kMasterWidthOffset);
	_height = READ_LE_UINT16(hdr + kMasterHeightOffset);
	_viewLeft = static_cast<int16>(READ_LE_UINT16(hdr + kMasterViewLeftOffset));
	_viewTop = static_cast<int16>(READ_LE_UINT16(hdr + kMasterViewTopOffset));

	// Embedded names TI.EXE copies out of the master header (FUN_004307f0):
	// the set's own name (what currentset() returns) and the default scene
	// and view used when opensetfile gets no scene/view arguments.
	_setName = readPascalString(hdr + kMasterNameOffset, _fileData, true);
	_defaultScene = readPascalString(hdr + kMasterDefaultSceneOffset, _fileData, true);
	_defaultView = readPascalString(hdr + kMasterDefaultViewOffset, _fileData, true);
	_setScriptId = READ_LE_UINT32(hdr + kSetScriptIdOffset);
	_starTable = resourceIndexById(READ_LE_UINT32(hdr + kStarTableIdOffset));

	uint32 sceneTableId = READ_LE_UINT32(hdr + kSceneTableIdOffset);
	_sceneTable = resourceIndexById(sceneTableId);
	if (_sceneTable < 0) {
		warning("Cyberflix: set '%s' references missing scene table %u", name.c_str(), sceneTableId);
		_master = -1;
		return false;
	}
	// The scene table is a tight array of fixed-size records, no count header.
	_sceneCount = _archive.getResource(static_cast<uint32>(_sceneTable)).length / kSceneRecordStride;

	_scripts.resize(_archive.getResourceCount());
	for (uint32 i = 0; i < _archive.getResourceCount(); ++i) {
		const Archive::Resource &res = _archive.getResource(i);
		if (res.empty || res.info != Script::kScriptInfoTag)
			continue;
		Common::ScopedPtr<Common::SeekableReadStream> stream(_archive.createReadStreamForResource(i));
		Common::SharedPtr<Script> script(new Script());
		if (stream && script->parse(stream.get()))
			_scripts[i] = script;
		else
			warning("Cyberflix: failed to parse set '%s' script resource %u", name.c_str(), res.id);
	}

	debug(1, "Cyberflix: opened set '%s': %ux%u, %u scene(s)",
			name.c_str(), _width, _height, _sceneCount);
	return true;
}

Common::String Set::sceneName(uint32 index) const {
	const byte *rec = sceneRecord(index);
	if (!rec)
		return Common::String();
	byte len = rec[kSceneNameOffset];
	if (len == 0 || len >= 16 || rec + kSceneNameOffset + 1 + len > _fileData.end())
		return Common::String();
	return Common::String(reinterpret_cast<const char *>(rec ) + kSceneNameOffset + 1, len);
}

int Set::findScene(const Common::String &name) const {
	for (uint32 i = 0; i < _sceneCount; ++i) {
		const byte *rec = sceneRecord(i);
		if (rec && pascalEqualsIgnoreCase(rec + kSceneNameOffset, _fileData.end(), name))
			return static_cast<int>(i);
	}
	return -1;
}

uint32 Set::angleCount(uint32 scene, uint32 table) const {
	uint32 count = 0;
	panoramaTable(scene, table, count);
	return count;
}

int Set::findView(uint32 scene, const Common::String &name) const {
	if (name.empty())
		return -1;
	uint32 count = 0;
	const byte *dir = viewDirectory(scene, count);
	if (!dir)
		return -1;
	for (uint32 i = 0; i < count; ++i) {
		const byte *v = dir + kViewDirRecordsOffset + i * kViewRecordStride;
		if (v + kViewRecordStride > _fileData.end())
			break;
		byte len = v[kViewNameOffset];
		if (len && len < 16 &&
				pascalEqualsIgnoreCase(v + kViewNameOffset, _fileData.end(), name))
			return static_cast<int>(i);
	}
	return -1;
}

Common::String Set::viewName(uint32 scene, uint32 index) const {
	uint32 count = 0;
	const byte *dir = viewDirectory(scene, count);
	if (!dir || index >= count)
		return Common::String();
	const byte *v = dir + kViewDirRecordsOffset + index * kViewRecordStride;
	if (v + kViewRecordStride > _fileData.end())
		return Common::String();
	byte len = v[kViewNameOffset];
	if (len == 0 || len >= 16 || v + kViewNameOffset + 1 + len > _fileData.end())
		return Common::String();
	return Common::String(reinterpret_cast<const char *>(v ) + kViewNameOffset + 1, len);
}

int Set::angleForView(uint32 scene, uint32 table, int viewIdx) const {
	if (viewIdx < 0)
		return -1;
	uint32 count = 0;
	const byte *pano = panoramaTable(scene, table, count);
	if (!pano)
		return -1;
	// TI's panorama records run from base+0xc with base = payload-4, i.e.
	// payload+8; the view-index tag sits at record+0x38 (FUN_004425e0 reads
	// piVar5[0xe]). Records not facing a view directly are tagged -1.
	for (uint32 i = 0; i < count; ++i) {
		const byte *r = pano + 8 + i * kPanoramaRecordStride;
		if (r + kPanoramaRecordStride > _fileData.end())
			break;
		if (static_cast<int32>(READ_LE_UINT32(r + 0x38)) == viewIdx)
			return static_cast<int>(i);
	}
	return -1;
}

int Set::viewTagAtAngle(uint32 scene, uint32 table, uint32 angle) const {
	uint32 count = 0;
	const byte *pano = panoramaTable(scene, table, count);
	if (!pano || angle >= count)
		return -1;
	const byte *r = pano + 8 + angle * kPanoramaRecordStride;
	if (r + kPanoramaRecordStride > _fileData.end())
		return -1;
	int32 tag = static_cast<int32>(READ_LE_UINT32(r + 0x38));
	return tag >= 0 ? static_cast<int>(tag): -1;
}

bool Set::fillCameraFromRecord(const byte *record, CameraData &camera) const {
	const byte *hdr = _master >= 0 ? engineBase(static_cast<uint32>(_master)) : nullptr;
	if (!hdr || hdr + kMasterCameraFieldsEnd > _fileData.end())
		return false;

	camera.heading = READ_LE_INT16(record + kPanoramaHeadingOffset);
	camera.cameraX = READ_LE_INT16(record + kPanoramaCameraXOffset);
	camera.cameraY = READ_LE_INT16(record + kPanoramaCameraYOffset);
	camera.cameraZ = READ_LE_INT16(record + kPanoramaCameraZOffset);
	// DAT_0046119a: zeroed at set open (FUN_004307f0), then script-driven via
	// the camerahi builtin — BOOTFILE's global openset handler calls
	// adjustcamera(), which sets it per set (halla 139, hallc 80, halld 150,
	// everything else 0).
	camera.baseZ = _baseZ;
	int16 farPlane = READ_LE_INT16(hdr + kMasterFarPlaneOffset);
	int16 nearDivisor = READ_LE_INT16(hdr + kMasterNearDivisorOffset);
	camera.nearPlane = nearDivisor ? farPlane / nearDivisor : farPlane;
	camera.farPlane = farPlane;
	camera.viewportLeft = _viewLeft;
	camera.viewportTop = _viewTop;
	camera.viewportRight = _viewLeft + static_cast<int16>(_width);
	camera.viewportBottom = _viewTop + static_cast<int16>(_height);
	// Focal length for the software pinhole projection used by world actors and
	// props; see Foley/van Dam et al., Computer Graphics: Principles and
	// Practice, perspective projection/viewing pipeline.
	camera.centerX = _viewLeft + static_cast<int16>(_width / 2);
	camera.centerY = _viewTop + static_cast<int16>(_height / 2);
	int16 halfW = static_cast<int16>(_width / 2);
	int16 halfH = static_cast<int16>(_height / 2);
	camera.focal = MAX(halfW, halfH);
	return true;
}

bool Set::cameraData(uint32 scene, uint32 table, uint32 angle, CameraData &camera) const {
	uint32 count = 0;
	const byte *pano = panoramaTable(scene, table, count);
	if (!pano || angle >= count)
		return false;
	const byte *r = pano + 8 + angle * kPanoramaRecordStride;
	if (r + kPanoramaRecordStride > _fileData.end())
		return false;
	return fillCameraFromRecord(r, camera);
}

bool Set::transitionCameraData(uint32 transitionId, uint32 frame, CameraData &camera) const {
	uint32 count = 0;
	const byte *transition = transitionTable(transitionId, count);
	if (!transition || frame >= count)
		return false;
	const byte *r = transition + 0x0c + frame * kPanoramaRecordStride;
	if (r + kPanoramaRecordStride > _fileData.end())
		return false;
	return fillCameraFromRecord(r, camera);
}

int Set::nextTaggedAngle(uint32 scene, uint32 table, int startAngle) const {
	uint32 count = angleCount(scene, table);
	if (startAngle < 0 || count == 0)
		return -1;
	for (uint32 i = 1; i <= count; ++i) {
		uint32 angle = (static_cast<uint32>(startAngle) + i) % count;
		if (viewTagAtAngle(scene, table, angle) >= 0)
			return static_cast<int>(angle);
	}
	return -1;
}

uint32 Set::forwardTransitionForView(uint32 scene, int viewIdx) const {
	if (viewIdx < 0)
		return 0;
	int angle = angleForView(scene, 0, viewIdx);
	if (angle < 0)
		return 0;
	uint32 count = 0;
	const byte *pano = panoramaTable(scene, 0, count);
	if (!pano || static_cast<uint32>(angle) >= count)
		return 0;
	const byte *r = pano + 8 + static_cast<uint32>(angle) * kPanoramaRecordStride;
	if (r + kPanoramaRecordStride > _fileData.end())
		return 0;
	return READ_LE_UINT32(r + 0x34);
}

const Script *Set::setScript() const {
	return scriptById(_setScriptId);
}

Common::SharedPtr<Script> Set::setScriptShared() const {
	return scriptByIdShared(_setScriptId);
}

const Script *Set::sceneScript(uint32 scene) const {
	const byte *rec = sceneRecord(scene);
	if (!rec)
		return nullptr;
	return scriptById(READ_LE_UINT32(rec + kSceneScriptOffset));
}

Common::SharedPtr<Script> Set::sceneScriptShared(uint32 scene) const {
	const byte *rec = sceneRecord(scene);
	if (!rec)
		return Common::SharedPtr<Script>();
	return scriptByIdShared(READ_LE_UINT32(rec + kSceneScriptOffset));
}

Common::String Set::hitTestPainting(uint32 scene, const Common::String &view, int16 x, int16 y) const {
	const byte *v = viewRecord(scene, view);
	uint32 count = 0, length = 0;
	const byte *table = paintingTable(v, count, length);
	if (!table)
		return Common::String();
	for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
		const byte *rec = table + 8 + static_cast<uint32>(i) * 0x24;
		if (static_cast<uint32>(rec - table) + 0x24 > length)
			break;
		int16 top = static_cast<int16>(READ_LE_UINT16(rec + 0x08));
		int16 left = static_cast<int16>(READ_LE_UINT16(rec + 0x0a));
		int16 bottom = static_cast<int16>(READ_LE_UINT16(rec + 0x0c));
		int16 right = static_cast<int16>(READ_LE_UINT16(rec + 0x0e));
		if (x >= left && x < right && y >= top && y < bottom)
			return pascalString(rec + 0x14);
	}
	return Common::String();
}

bool Set::pointInPainting(uint32 scene, const Common::String &view,
		const Common::String &painting, int16 x, int16 y) const {
	const byte *v = viewRecord(scene, view);
	uint32 count = 0, length = 0;
	const byte *table = paintingTable(v, count, length);
	if (!table)
		return false;
	for (uint32 i = 0; i < count; ++i) {
		const byte *rec = table + 8 + i * 0x24;
		if (8 + i * 0x24 + 0x24 > length)
			break;
		if (!pascalEqualsIgnoreCase(rec + 0x14, _fileData.end(), painting))
			continue;
		int16 top = static_cast<int16>(READ_LE_UINT16(rec + 0x08));
		int16 left = static_cast<int16>(READ_LE_UINT16(rec + 0x0a));
		int16 bottom = static_cast<int16>(READ_LE_UINT16(rec + 0x0c));
		int16 right = static_cast<int16>(READ_LE_UINT16(rec + 0x0e));
		return x >= left && x < right && y >= top && y < bottom;
	}
	return false;
}

const Script *Set::paintingScript(uint32 scene, const Common::String &view,
		const Common::String &painting) const {
	return paintingScriptShared(scene, view, painting).get();
}

Common::SharedPtr<Script> Set::paintingScriptShared(uint32 scene, const Common::String &view,
		const Common::String &painting) const {
	Common::SharedPtr<Script> paintingScript, sceneScript, setScript;
	if (!paintingDispatchScripts(scene, view, painting, paintingScript, sceneScript, setScript))
		return Common::SharedPtr<Script>();
	return paintingScript;
}

bool Set::paintingDispatchScripts(uint32 scene, const Common::String &view,
		const Common::String &painting, Common::SharedPtr<Script> &paintingScript,
		Common::SharedPtr<Script> &sceneScript,
		Common::SharedPtr<Script> &setScript) const {
	// sendtopainting is hit by idle/mouse SET scripts. Resolve all three scopes
	// in one pass so the engine can keep them alive without building temporary
	// Common::Array objects on every dispatch.
	paintingScript = Common::SharedPtr<Script>();
	sceneScript = Common::SharedPtr<Script>();
	setScript = Common::SharedPtr<Script>();

	const byte *v = viewRecord(scene, view);
	if (!v)
		return false;
	const byte *sceneRec = sceneRecord(scene);
	if (sceneRec)
		sceneScript = scriptByIdShared(READ_LE_UINT32(sceneRec + kSceneScriptOffset));
	setScript = scriptByIdShared(_setScriptId);

	// The same painting commonly receives repeated setcursor/idle messages, and
	// SET files are immutable while open, so caching the last painting-script
	// lookup avoids rescanning Pascal painting records in the VM hot path.
	if (_paintingScriptCacheValid && _paintingScriptCacheScene == scene &&
			_paintingScriptCacheView.equalsIgnoreCase(view) &&
			_paintingScriptCacheName.equalsIgnoreCase(painting)) {
		paintingScript = _paintingScriptCacheScript;
		return true;
	}

	Common::SharedPtr<Script> result;
	uint32 count = 0, length = 0;
	const byte *table = paintingTable(v, count, length);
	if (table) {
		for (uint32 i = 0; i < count; ++i) {
			const byte *rec = table + 8 + i * 0x24;
			if (8 + i * 0x24 + 0x24 > length)
				break;
			if (pascalEqualsIgnoreCase(rec + 0x14, _fileData.end(), painting)) {
				result = scriptByIdShared(READ_LE_UINT32(rec + 0x10));
				break;
			}
		}
	}

	_paintingScriptCacheValid = true;
	_paintingScriptCacheScene = scene;
	_paintingScriptCacheView = view;
	_paintingScriptCacheName = painting;
	_paintingScriptCacheScript = result;
	paintingScript = result;
	return true;
}

uint32 Set::paintingCount(uint32 scene, const Common::String &view) const {
	const byte *v = viewRecord(scene, view);
	uint32 count = 0, length = 0;
	return paintingTable(v, count, length) ? count : 0;
}

Common::String Set::indexToPainting(uint32 scene, const Common::String &view, uint32 index) const {
	if (index == 0)
		return Common::String();
	const byte *v = viewRecord(scene, view);
	uint32 count = 0, length = 0;
	const byte *table = paintingTable(v, count, length);
	if (!table || index > count)
		return Common::String();
	const byte *rec = table + 8 + (index - 1) * 0x24;
	if (static_cast<uint32>(rec - table) + 0x24 > length)
		return Common::String();
	return pascalString(rec + 0x14);
}

bool Set::transitionDestination(uint32 transitionId, uint32 &scene,
		Common::String &view, int &angle) const {
	uint32 count = 0;
	const byte *transition = transitionTable(transitionId, count);
	if (!transition || count == 0)
		return false;
	int sceneIdx = findSceneByViewDirId(READ_LE_UINT32(transition + 0x08));
	if (sceneIdx < 0)
		return false;
	const byte *last = transition + 0x0c + (count - 1) * kPanoramaRecordStride;
	if (last + kPanoramaRecordStride > _fileData.end())
		return false;
	int viewIdx = nearestViewForHeading(static_cast<uint32>(sceneIdx), READ_LE_INT16(last + 0x26));
	if (viewIdx < 0)
		return false;
	int viewAngle = angleForView(static_cast<uint32>(sceneIdx), 0, viewIdx);
	scene = static_cast<uint32>(sceneIdx);
	view = viewName(static_cast<uint32>(sceneIdx), static_cast<uint32>(viewIdx));
	angle = viewAngle >= 0 ? viewAngle : 0;
	return !view.empty();
}

bool Set::renderScene(uint32 scene, uint32 table, uint32 angle, FrameSequence &seq) {
	uint32 count = 0;
	const byte *pano = panoramaTable(scene, table, count);
	if (!pano) {
		warning("Cyberflix: set '%s' scene %u panorama table %u missing", _name.c_str(), scene, table);
		return false;
	}
	if (angle >= count) {
		warning("Cyberflix: set '%s' scene %u table %u angle %u out of range (%u)",
				_name.c_str(), scene, table, angle, count);
		return false;
	}

	// The panorama is a continuous cyclic delta-animation (TI.EXE FUN_00442970:
	// it advances/wraps a frame index and applies each frame onto the retained
	// framebuffer via FUN_00423600). Frame 0 is the cold-start keyframe; every
	// later angle is inter-coded against the previous one. So to show an
	// arbitrary angle from cold we replay frames 0..angle, exactly the buffer
	// state the engine would have built up. Decoding a delta angle standalone
	// would leave its "copy from previous" regions unwritten (visible garbage).
	//
	// Some authored records (for example GSTAIR2 Scene64/Scene65) use a frame
	// resource id of 0, which native FUN_00442e90 handles as "keep the retained
	// framebuffer". Only clear for a normal non-zero starting frame.
	const byte *first = pano + 8;
	if (first + kPanoramaRecordStride > _fileData.end())
		return false;
	if (READ_LE_UINT32(first + kPanoramaFrameIdOffset) != 0)
		seq.clear();
	for (uint32 a = 0; a <= angle; ++a) {
		const byte *r = pano + 8 + a * kPanoramaRecordStride;
		if (r + kPanoramaRecordStride > _fileData.end())
			return false;
		if (!applyFrameResource(READ_LE_UINT32(r + kPanoramaFrameIdOffset), seq))
			return false;
	}

	return true;
}

bool Set::renderScene(uint32 scene, uint32 table, uint32 angle, FrameImage &out) {
	FrameSequence seq;
	return renderScene(scene, table, angle, seq, out);
}

bool Set::renderScene(uint32 scene, uint32 table, uint32 angle, FrameSequence &seq, FrameImage &out) {
	if (!renderScene(scene, table, angle, seq))
		return false;
	seq.copyTo(out);
	return true;
}

bool Set::applyPanoramaFrame(uint32 scene, uint32 table, uint32 angle, FrameSequence &seq) {
	uint32 count = 0;
	const byte *pano = panoramaTable(scene, table, count);
	if (!pano || angle >= count)
		return false;
	const byte *r = pano + 8 + angle * kPanoramaRecordStride;
	if (r + kPanoramaRecordStride > _fileData.end())
		return false;
	return applyFrameResource(READ_LE_UINT32(r + kPanoramaFrameIdOffset), seq);
}

bool Set::applyPanoramaFrame(uint32 scene, uint32 table, uint32 angle, FrameSequence &seq, FrameImage &out) {
	if (!applyPanoramaFrame(scene, table, angle, seq))
		return false;
	seq.copyTo(out);
	return true;
}

uint32 Set::transitionFrameCount(uint32 transitionId) const {
	uint32 count = 0;
	transitionTable(transitionId, count);
	return count;
}

bool Set::applyTransitionFrame(uint32 transitionId, uint32 frame, FrameSequence &seq) {
	uint32 count = 0;
	const byte *transition = transitionTable(transitionId, count);
	if (!transition || frame >= count)
		return false;
	const byte *r = transition + 0x0c + frame * kPanoramaRecordStride;
	if (r + kPanoramaRecordStride > _fileData.end())
		return false;
	return applyFrameResource(READ_LE_UINT32(r + kPanoramaFrameIdOffset), seq);
}

bool Set::applyTransitionFrame(uint32 transitionId, uint32 frame, FrameSequence &seq, FrameImage &out) {
	if (!applyTransitionFrame(transitionId, frame, seq))
		return false;
	seq.copyTo(out);
	return true;
}

bool Set::loadSetPalette(Palette &rgb) const {
	if (_fileData.empty())
		return false;
	return loadPalette(_fileData.begin(), _fileData.size(), rgb);
}

} // End of namespace Cyberflix
