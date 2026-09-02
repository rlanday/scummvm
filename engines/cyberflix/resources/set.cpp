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

namespace CyberFlix {

ResourceView Set::engineView(uint32 index) const {
	if (index >= _archive.getResourceCount())
		return ResourceView();
	return resourceEngineView(_fileData, _archive.getResource(index));
}

ResourceView Set::payloadView(uint32 index) const {
	if (index >= _archive.getResourceCount())
		return ResourceView();
	return resourcePayloadView(_fileData, _archive.getResource(index));
}

int Set::resourceIndexById(uint32 id) const {
	return CyberFlix::resourceIndexById(_archive, id);
}

Common::SharedPtr<Script> Set::scriptByIdShared(uint32 id) const {
	int idx = resourceIndexById(id);
	if (idx < 0 || static_cast<uint32>(idx) >= _scripts.size())
		return Common::SharedPtr<Script>();
	return _scripts[static_cast<uint32>(idx)];
}

ResourceView Set::sceneRecord(uint32 scene) const {
	if (scene >= _sceneCount || _sceneTable < 0)
		return ResourceView();
	return payloadView(static_cast<uint32>(_sceneTable)).recordAt(0, scene, kSceneRecordStride);
}

RecordRange Set::viewDirectory(uint32 scene) const {
	const ResourceView rec = sceneRecord(scene);
	uint32 directoryId;
	if (!rec.readUint32LE(kSceneViewDirOffset, directoryId))
		return RecordRange();
	int idx = resourceIndexById(directoryId);
	if (idx < 0)
		return RecordRange();
	const ResourceView directory = payloadView(static_cast<uint32>(idx));
	uint32 count;
	if (!directory.readUint32LE(kViewDirCountOffset, count))
		return RecordRange();
	return RecordRange(directory, kViewDirRecordsOffset, count, kViewRecordStride);
}

ResourceView Set::viewRecord(uint32 scene, const Common::String &view) const {
	int viewIdx = findView(scene, view);
	if (viewIdx < 0)
		return ResourceView();
	return viewDirectory(scene).record(static_cast<uint32>(viewIdx));
}

RecordRange Set::paintingTable(const ResourceView &viewRecord) const {
	uint32 tableId;
	if (!viewRecord.readUint32LE(kViewPaintingTableOffset, tableId))
		return RecordRange();
	if (tableId == 0)
		return RecordRange();
	int idx = resourceIndexById(tableId);
	if (idx < 0)
		return RecordRange();
	const ResourceView table = engineView(static_cast<uint32>(idx));
	uint32 count;
	if (!table.readUint32LE(0, count))
		return RecordRange();
	return RecordRange(table, 8, count, 0x24);
}

RecordRange Set::panoramaTable(uint32 scene, uint32 table) const {
	if (table > kPanoramaTableB)
		return RecordRange();
	const ResourceView rec = sceneRecord(scene);
	uint32 panoramaId;
	if (!rec.readUint32LE(table == 0 ? kScenePanoramaAOffset : kScenePanoramaBOffset, panoramaId))
		return RecordRange();
	int idx = resourceIndexById(panoramaId);
	if (idx < 0)
		return RecordRange();
	const ResourceView panorama = payloadView(static_cast<uint32>(idx));
	uint32 count;
	if (!panorama.readUint32LE(kPanoramaCountOffset, count))
		return RecordRange();
	return RecordRange(panorama, 8, count, kPanoramaRecordStride);
}

RecordRange Set::transitionTable(uint32 transitionId) const {
	int idx = resourceIndexById(transitionId);
	if (idx < 0)
		return RecordRange();
	const ResourceView transition = engineView(static_cast<uint32>(idx));
	uint32 count;
	if (!transition.readUint32LE(0x04, count))
		return RecordRange();
	return RecordRange(transition, 0x0c, count, kPanoramaRecordStride);
}

bool Set::applyFrameResource(uint32 frameId, FrameSequence &seq) const {
	// TI.EXE FUN_00442e90 treats a panorama/transition frame resource id of 0
	// as a no-op on the retained framebuffer; resource 0 is the SET master
	// header and must not be sent through the frame decoder.
	if (frameId == 0)
		return true;

	int idx = resourceIndexById(frameId);
	if (idx < 0) {
		warning("CyberFlix: set '%s' references missing frame res %u", _name.c_str(), frameId);
		return false;
	}
	const ResourceView frame = engineView(static_cast<uint32>(idx));
	if (!frame.valid())
		return false;
	if (seq.applyFrame(frame.dataAt(0, frame.size()), static_cast<uint32>(frame.size())) == 0) {
		warning("CyberFlix: set '%s' frame %u decode failed", _name.c_str(), frameId);
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
		uint32 recordViewDirectoryId;
		if (sceneRecord(i).readUint32LE(kSceneViewDirOffset, recordViewDirectoryId) &&
				recordViewDirectoryId == viewDirId)
			return static_cast<int>(i);
	}
	return -1;
}

int Set::nearestViewForHeading(uint32 scene, int heading) const {
	const RecordRange directory = viewDirectory(scene);
	if (!directory.valid())
		return -1;
	int best = -1;
	int bestDist = 1000;
	for (uint32 i = 0; i < directory.size(); ++i) {
		const ResourceView view = directory.record(i);
		const byte *v = view.dataAt(0, kViewRecordStride);
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
	const ResourceView table = engineView(static_cast<uint32>(_starTable));
	uint32 count;
	if (!table.readUint32LE(kStarTableCountOffset, count))
		return false;
	const RecordRange records(table, kStarTableRecordsOffset, count, kStarRecordStride);
	if (!records.valid())
		return false;

	for (uint32 i = 0; i < records.size(); ++i) {
		const ResourceView recordView = records.record(i);
		const byte *record = recordView.dataAt(0, kStarRecordStride);
		if (recordView.pascalEqualsIgnoreCase(kStarPrimaryNameOffset, name)) {
			x = READ_LE_INT16(record + kStarPrimaryXOffset);
			y = READ_LE_INT16(record + kStarPrimaryYOffset);
			z = READ_LE_INT16(record + kStarPrimaryZOffset);
			return true;
		}
		if (READ_LE_UINT32(record + kStarSecondaryFlagOffset) != 0 &&
				recordView.pascalEqualsIgnoreCase(kStarSecondaryNameOffset, name)) {
			x = READ_LE_INT16(record + kStarSecondaryXOffset);
			y = READ_LE_INT16(record + kStarSecondaryYOffset);
			z = READ_LE_INT16(record + kStarSecondaryZOffset);
			return true;
		}
	}
	return false;
}

void Set::reset() {
	_scripts.clear();
	_paintingScriptCacheScript.reset();
	_archive.close();
	_fileData.clear();

	_master = -1;
	_sceneTable = -1;
	_starTable = -1;
	_baseZ = 0; // FUN_004307f0 zeroes DAT_0046119a for the incoming set.
	_sceneCount = 0;
	_setScriptId = 0;
	_paintingScriptCacheValid = false;
	_paintingScriptCacheScene = 0;
	_paintingScriptCacheView.clear();
	_paintingScriptCacheName.clear();
	_width = _height = 0;
	_viewLeft = _viewTop = 0;
	_name.clear();
	_setName.clear();
	_defaultScene.clear();
	_defaultView.clear();
}

bool Set::open(const Common::String &name) {
	reset();
	_name = name;

	if (!openArchiveFile(name, "set", _fileData, _archive))
		return false;

	_master = findMasterHeaderIndex(_archive);
	if (_master < 0) {
		warning("CyberFlix: set '%s' has no master header", name.c_str());
		reset();
		return false;
	}

	const ResourceView master = engineView(static_cast<uint32>(_master));
	const byte *hdr = master.dataAt(0, kMasterDefaultViewOffset + 1);
	if (!hdr) {
		warning("CyberFlix: set '%s' master header truncated", name.c_str());
		reset();
		return false;
	}
	_width = READ_LE_UINT16(hdr + kMasterWidthOffset);
	_height = READ_LE_UINT16(hdr + kMasterHeightOffset);
	_viewLeft = static_cast<int16>(READ_LE_UINT16(hdr + kMasterViewLeftOffset));
	_viewTop = static_cast<int16>(READ_LE_UINT16(hdr + kMasterViewTopOffset));

	// Embedded names TI.EXE copies out of the master header (FUN_004307f0):
	// the set's own name (what currentset() returns) and the default scene
	// and view used when opensetfile gets no scene/view arguments.
	_setName = master.readPascalString(kMasterNameOffset, true);
	_defaultScene = master.readPascalString(kMasterDefaultSceneOffset, true);
	_defaultView = master.readPascalString(kMasterDefaultViewOffset, true);
	_setScriptId = READ_LE_UINT32(hdr + kSetScriptIdOffset);
	_starTable = resourceIndexById(READ_LE_UINT32(hdr + kStarTableIdOffset));

	uint32 sceneTableId = READ_LE_UINT32(hdr + kSceneTableIdOffset);
	_sceneTable = resourceIndexById(sceneTableId);
	if (_sceneTable < 0) {
		warning("CyberFlix: set '%s' references missing scene table %u", name.c_str(), sceneTableId);
		reset();
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
			warning("CyberFlix: failed to parse set '%s' script resource %u", name.c_str(), res.id);
	}

	debug(1, "CyberFlix: opened set '%s': %ux%u, %u scene(s)",
			name.c_str(), _width, _height, _sceneCount);
	return true;
}

Common::String Set::sceneName(uint32 index) const {
	const ResourceView record = sceneRecord(index);
	const byte *length = record.dataAt(kSceneNameOffset);
	if (!length || *length == 0 || *length >= 16)
		return Common::String();
	return record.readPascalString(kSceneNameOffset);
}

int Set::findScene(const Common::String &name) const {
	for (uint32 i = 0; i < _sceneCount; ++i) {
		const ResourceView record = sceneRecord(i);
		if (record.pascalEqualsIgnoreCase(kSceneNameOffset, name))
			return static_cast<int>(i);
	}
	return -1;
}

uint32 Set::angleCount(uint32 scene, uint32 table) const {
	return panoramaTable(scene, table).size();
}

int Set::findView(uint32 scene, const Common::String &name) const {
	if (name.empty())
		return -1;
	const RecordRange directory = viewDirectory(scene);
	if (!directory.valid())
		return -1;
	for (uint32 i = 0; i < directory.size(); ++i) {
		const ResourceView view = directory.record(i);
		const byte *v = view.dataAt(0, kViewRecordStride);
		byte len = v[kViewNameOffset];
		if (len && len < 16 && view.pascalEqualsIgnoreCase(kViewNameOffset, name))
			return static_cast<int>(i);
	}
	return -1;
}

Common::String Set::viewName(uint32 scene, uint32 index) const {
	const ResourceView view = viewDirectory(scene).record(index);
	const byte *length = view.dataAt(kViewNameOffset);
	if (!length || *length == 0 || *length >= 16)
		return Common::String();
	return view.readPascalString(kViewNameOffset);
}

int Set::angleForView(uint32 scene, uint32 table, int viewIdx) const {
	if (viewIdx < 0)
		return -1;
	const RecordRange panorama = panoramaTable(scene, table);
	if (!panorama.valid())
		return -1;
	// TI's panorama records run from base+0xc with base = payload-4, i.e.
	// payload+8; the view-index tag sits at record+0x38 (FUN_004425e0 reads
	// piVar5[0xe]). Records not facing a view directly are tagged -1.
	for (uint32 i = 0; i < panorama.size(); ++i) {
		const byte *r = panorama.record(i).dataAt(0, kPanoramaRecordStride);
		if (static_cast<int32>(READ_LE_UINT32(r + 0x38)) == viewIdx)
			return static_cast<int>(i);
	}
	return -1;
}

int Set::viewTagAtAngle(uint32 scene, uint32 table, uint32 angle) const {
	const ResourceView record = panoramaTable(scene, table).record(angle);
	const byte *r = record.dataAt(0, kPanoramaRecordStride);
	if (!r)
		return -1;
	int32 tag = static_cast<int32>(READ_LE_UINT32(r + 0x38));
	return tag >= 0 ? static_cast<int>(tag): -1;
}

bool Set::fillCameraFromRecord(const ResourceView &record, CameraData &camera) const {
	const byte *r = record.dataAt(0, kPanoramaRecordStride);
	const byte *hdr = _master >= 0
			? engineView(static_cast<uint32>(_master)).dataAt(0, kMasterCameraFieldsEnd) : nullptr;
	if (!r || !hdr)
		return false;

	camera.heading = READ_LE_INT16(r + kPanoramaHeadingOffset);
	camera.cameraX = READ_LE_INT16(r + kPanoramaCameraXOffset);
	camera.cameraY = READ_LE_INT16(r + kPanoramaCameraYOffset);
	camera.cameraZ = READ_LE_INT16(r + kPanoramaCameraZOffset);
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
	return fillCameraFromRecord(panoramaTable(scene, table).record(angle), camera);
}

bool Set::transitionCameraData(uint32 transitionId, uint32 frame, CameraData &camera) const {
	return fillCameraFromRecord(transitionTable(transitionId).record(frame), camera);
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
	const ResourceView record = panoramaTable(scene, 0).record(static_cast<uint32>(angle));
	const byte *r = record.dataAt(0, kPanoramaRecordStride);
	if (!r)
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
	uint32 scriptId;
	if (!sceneRecord(scene).readUint32LE(kSceneScriptOffset, scriptId))
		return nullptr;
	return scriptById(scriptId);
}

Common::SharedPtr<Script> Set::sceneScriptShared(uint32 scene) const {
	uint32 scriptId;
	if (!sceneRecord(scene).readUint32LE(kSceneScriptOffset, scriptId))
		return Common::SharedPtr<Script>();
	return scriptByIdShared(scriptId);
}

Common::String Set::hitTestPainting(uint32 scene, const Common::String &view, int16 x, int16 y) const {
	const RecordRange paintings = paintingTable(viewRecord(scene, view));
	if (!paintings.valid())
		return Common::String();
	for (int i = static_cast<int>(paintings.size()) - 1; i >= 0; --i) {
		const ResourceView record = paintings.record(static_cast<uint>(i));
		const byte *rec = record.dataAt(0, 0x24);
		int16 top = static_cast<int16>(READ_LE_UINT16(rec + 0x08));
		int16 left = static_cast<int16>(READ_LE_UINT16(rec + 0x0a));
		int16 bottom = static_cast<int16>(READ_LE_UINT16(rec + 0x0c));
		int16 right = static_cast<int16>(READ_LE_UINT16(rec + 0x0e));
		if (x >= left && x < right && y >= top && y < bottom)
			return record.readPascalString(0x14);
	}
	return Common::String();
}

bool Set::pointInPainting(uint32 scene, const Common::String &view,
		const Common::String &painting, int16 x, int16 y) const {
	const RecordRange paintings = paintingTable(viewRecord(scene, view));
	if (!paintings.valid())
		return false;
	for (uint32 i = 0; i < paintings.size(); ++i) {
		const ResourceView record = paintings.record(i);
		const byte *rec = record.dataAt(0, 0x24);
		if (!record.pascalEqualsIgnoreCase(0x14, painting))
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

	const ResourceView viewRecordData = viewRecord(scene, view);
	if (!viewRecordData.valid())
		return false;
	uint32 sceneScriptId;
	if (sceneRecord(scene).readUint32LE(kSceneScriptOffset, sceneScriptId))
		sceneScript = scriptByIdShared(sceneScriptId);
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
	const RecordRange paintings = paintingTable(viewRecordData);
	if (paintings.valid()) {
		for (uint32 i = 0; i < paintings.size(); ++i) {
			const ResourceView record = paintings.record(i);
			const byte *rec = record.dataAt(0, 0x24);
			if (record.pascalEqualsIgnoreCase(0x14, painting)) {
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
	return paintingTable(viewRecord(scene, view)).size();
}

Common::String Set::indexToPainting(uint32 scene, const Common::String &view, uint32 index) const {
	if (index == 0)
		return Common::String();
	const RecordRange paintings = paintingTable(viewRecord(scene, view));
	if (!paintings.valid() || index > paintings.size())
		return Common::String();
	return paintings.record(index - 1).readPascalString(0x14);
}

bool Set::transitionDestination(uint32 transitionId, uint32 &scene,
		Common::String &view, int &angle) const {
	const RecordRange transition = transitionTable(transitionId);
	uint32 viewDirectoryId;
	if (!transition.valid() || transition.size() == 0 ||
			!transition.view().readUint32LE(0x08, viewDirectoryId))
		return false;
	int sceneIdx = findSceneByViewDirId(viewDirectoryId);
	if (sceneIdx < 0)
		return false;
	const byte *last = transition.record(transition.size() - 1).dataAt(0, kPanoramaRecordStride);
	if (!last)
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
	const RecordRange panorama = panoramaTable(scene, table);
	if (!panorama.valid()) {
		warning("CyberFlix: set '%s' scene %u panorama table %u missing", _name.c_str(), scene, table);
		return false;
	}
	if (angle >= panorama.size()) {
		warning("CyberFlix: set '%s' scene %u table %u angle %u out of range (%u)",
				_name.c_str(), scene, table, angle, panorama.size());
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
	const byte *first = panorama.record(0).dataAt(0, kPanoramaRecordStride);
	if (!first)
		return false;
	if (READ_LE_UINT32(first + kPanoramaFrameIdOffset) != 0)
		seq.clear();
	for (uint32 a = 0; a <= angle; ++a) {
		const byte *r = panorama.record(a).dataAt(0, kPanoramaRecordStride);
		if (!r)
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
	const byte *r = panoramaTable(scene, table).record(angle).dataAt(0, kPanoramaRecordStride);
	if (!r)
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
	return transitionTable(transitionId).size();
}

bool Set::applyTransitionFrame(uint32 transitionId, uint32 frame, FrameSequence &seq) {
	const byte *r = transitionTable(transitionId).record(frame).dataAt(0, kPanoramaRecordStride);
	if (!r)
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

} // End of namespace CyberFlix
