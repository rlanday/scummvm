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
#include "common/events.h"
#include "common/hashmap.h"
#include "common/system.h"
#include "common/util.h"

#include "audio/audiostream.h"
#include "audio/mixer.h"

#include "graphics/cursorman.h"
#include "graphics/surface.h"

#include "dreamfactory/archive.h"
#include "dreamfactory/debug.h"
#include "dreamfactory/audio_helpers.h"
#include "dreamfactory/console.h"
#include "dreamfactory/dreamfactory.h"
#include "dreamfactory/image.h"
#include "dreamfactory/audio/cbx_audio.h"
#include "dreamfactory/resource_helpers.h"
#include "dreamfactory/runtime/graphics_helpers.h"

namespace DreamFactory {

// Sample-add an 8-bit unsigned mono SFX buffer into the music track at the given
// sample offset, extending the track with silence (0x80) if needed and clamping.
static void mixSfx(Common::Array<byte> &track, const Common::Array<byte> &sfx, uint64 atSample) {
	if (sfx.empty())
		return;
	if (atSample > 0xffffffffU || sfx.size() > 0xffffffffU - atSample)
		return;
	const uint32 start = static_cast<uint32>(atSample);
	const uint32 end = start + static_cast<uint32>(sfx.size());
	if (track.size() < end)
		track.resize(end, 0x80);
	for (uint i = 0; i < sfx.size(); ++i) {
		int v = (static_cast<int>(track[start + i]) - 0x80) + (static_cast<int>(sfx[i]) - 0x80);
		v = CLIP(v, -128, 127);
		track[start + i] = static_cast<byte>(v + 0x80);
	}
}

// Frame cues play into ONE slot. TI.EXE FUN_0040ebf0 stops the cue channel
// (FUN_0042f690(0,0,0,1)) and frees the loaded sound (FUN_00430430) before
// loading and starting the next one, so a frame's cue always cuts off whatever
// the previous frame started. Letting them overlap instead stacks every cue in
// the movie on top of itself -- TOUR9.MOV alone would play sixteen narration
// lines at once.
static void playMovieFrameSfx(Audio::Mixer &mixer, Common::Array<Audio::SoundHandle> &handles,
		const Common::Array<byte> &pcm, byte volume) {
	if (pcm.empty())
		return;

	for (Audio::SoundHandle &handle : handles)
		mixer.stopHandle(handle);
	handles.clear();

	Audio::SoundHandle handle;
	Audio::SeekableAudioStream *stream = makeOwnedRawPcmStream(pcm);
	if (!stream)
		return;
	mixer.playStream(Audio::Mixer::kSFXSoundType, &handle, stream);
	mixer.setChannelVolume(handle, volume);
	handles.push_back(handle);
}

// Movie event chunks and interactive button records share the same small command
// language. Event chunks store the command at +0x00 and run it after the frame's
// hold time; button records store it at +0x00 and run it immediately after a
// hit-test. The operands are parallel too: MARKER/GOSUB read a Pascal movie name
// and GOTO/GOSUB read a Pascal frame target. GOSUB starts another movie and
// pushes the current movie plus return target; RETURN pops that stack. The
// purser desk book uses this on MAINO1.MOV frame "blackframe": nav GOSUB
// "man.mov", return frame "blackframe 2".
//
// Most Titanic movies only need END/GOTO/NEXT/PREV: they are straight playback
// or a single interactive frame with local button jumps. The movie-to-movie
// commands are much rarer because they implement a nested interactive insert:
// a parent movie fades out, temporarily runs another movie, then resumes at a
// named return frame. We did not hit that path until the purser's counter book,
// where clicking the book in MAINO1.MOV must gosub into MAN.MOV and MAN.MOV's
// end frames must RETURN to MAINO1.MOV so the parent can fade back in.
enum class MovieCommand : uint16 {
	kEnd = 1,
	kGoto = 2,
	kMarker = 3,
	kGosub = 4,
	kReturn = 5,
	kNext = 6,
	kPrev = 7
};

// Per-frame draw command (event chunk +0xc; TI.EXE FUN_0040eef0 switch).
//
// The switch has 23 cases. 0x10/0x11/0x12 take the frame pointer and are the
// plain blit and the two palette fades; every other case takes the movie rect
// and is a geometric transition that reveals the new frame a band at a time
// (handlers at 0x0040f250..0x00410520). Those transition ops correspond to the
// script-side visualeffect selectors as op N == 0x5dc1 + N, but they are a
// separate implementation from the script effect family at 0x00443bb0+.
//
// Only these four transition ops appear anywhere in the shipped data: op 0 in
// RUBAIYAT, op 1 in CONKDEAD/RUBAIYAT, op 3 in BEDMEM (the fireplace scrapbook)
// and op 0x0b in the TOUR movies. The other sixteen handlers are dead content.
enum MovieDrawOp {
	kMovieDrawBarnClose = 0x00, ///< two bands from the edges, meeting at the centre
	kMovieDrawBarnOpen = 0x01,  ///< two bands from the centre, moving apart
	kMovieDrawIrisOpen = 0x03,  ///< box growing from the centre on all four sides
	kMovieDrawWipe = 0x0b,      ///< one band sweeping from the right edge leftwards
	kMovieDrawBlit = 0x10,      ///< plain blit
	kMovieDrawFadeOut = 0x11,   ///< blit + palette fade to black across the hold
	kMovieDrawFadeIn = 0x12     ///< blit (palette black) + palette fade in
};

static bool isMovieTransitionOp(uint16 op) {
	return op == kMovieDrawBarnClose || op == kMovieDrawBarnOpen ||
			op == kMovieDrawIrisOpen || op == kMovieDrawWipe;
}

// Cap on nested GOSUB movies. The native player has no explicit stack limit;
// this is a ScummVM guard against a malformed MARKER/RETURN loop.
static const uint kMovieReturnStackLimit = 5;
static const uint32 kMusicCueCountOffset = 0x10a;
static const uint32 kMusicCueTableOffset = 0x10e;
static const uint32 kMusicCueRecordSize = 0x1a;
static const uint32 kMovieFrameTableOffset = 0x87c;
static const uint32 kMovieFrameRecordSize = 0x2a;
static const uint32 kMovieSfxTableOffset = 8;
static const uint32 kMovieSfxRecordSize = 0x2a;
static const uint32 kMovieButtonCountOffset = 0x442;
static const uint32 kMovieButtonTableOffset = 0x446;
static const uint32 kMovieButtonRecordSize = 0x40;

// A clickable region on an interactive movie frame. The original player reads
// the count at event chunk +0x442 and 0x40-byte records at +0x446;
// FUN_0040d710 hit-tests the rect against the click point and runs the action.
// Field offsets within the record:
//   +0x00 u16 action, one of MovieCommand,
//   +0x02 byte flags (bit0 => also require a per-pixel mask hit on click;
//         bit1 => hover-cursor eligible, see FUN_0040e5b0),
//   +0x08 QuickDraw rect {top, left, bottom, right} as int16: FUN_0041ac60
//         tests the packed point's low short (y) against rect[0]/rect[2] and
//         its high short (x) against rect[1]/rect[3]. (Verified in data:
//         PLAYMODE's GAME button rect {232,208,277,324} is 116 wide, 45 tall.)
//   +0x20 Pascal string = MARKER/GOSUB movie name,
//   +0x30 Pascal string = GOTO target frame name (or GOSUB return frame).
struct MovieButton {
	MovieCommand action = MovieCommand::kEnd;
	byte flags = 0;
	int16 left = 0, top = 0, right = 0, bottom = 0;
	Common::String marker;
	Common::String target;
	bool contains(int x, int y) const {
		return x >= left && x < right && y >= top && y < bottom;
	}
};

// Metadata for one entry in the movie's authored frame table.
struct MovieFrame {
	uint64 startMs = 0;
	uint32 videoResource = 0;
	MovieCommand navigation = MovieCommand::kNext;
	Common::String name;
	Common::String navigationMovie;
	Common::String navigationTarget;
	Common::Array<MovieButton> buttons;
	Common::SharedPtr<Common::Array<byte> > sfx;
	uint32 holdMs = 0;
	uint16 drawOp = kMovieDrawBlit;
	bool waitForCue = false;
};

struct MovieSegment {
	uint startFrame = 0;
	Palette palette;
};

struct MovieReturnFrame {
	Common::String name;
	int frame;
};

// Blit one clipped band of the decoded frame (TI.EXE FUN_00410660: intersect
// the band with the movie rect, offset by the movie origin, then copy).
void MovieRuntime::blitMovieBand(DreamFactoryEngine &engine, const byte *pixels, int w, int h,
		int x0, int y0, int left, int top, int right, int bottom) {
	left = MAX(left, 0);
	top = MAX(top, 0);
	right = MIN(right, w);
	bottom = MIN(bottom, h);
	if (right <= left || bottom <= top)
		return;

	Graphics::Surface *screen = engine._system->lockScreen();
	if (!screen)
		return;
	for (int y = top; y < bottom; ++y) {
		const int dstY = y0 + y;
		if (dstY < 0 || dstY >= screen->h)
			continue;
		int dstX = x0 + left;
		int count = right - left;
		int srcX = left;
		if (dstX < 0) {
			srcX -= dstX;
			count += dstX;
			dstX = 0;
		}
		if (dstX + count > screen->w)
			count = screen->w - dstX;
		if (count > 0)
			memcpy(screen->getBasePtr(dstX, dstY), pixels + y * w + srcX, count);
	}
	engine._system->unlockScreen();
}

// Geometric frame transitions (TI.EXE FUN_0040eef0 cases other than
// 0x10..0x12). Each reveals the newly decoded frame a band at a time over
// @p steps ticks of the 60 Hz scaled timer, then blits the whole rect. The
// per-band geometry below is a direct port of FUN_0040f250 (barn close),
// FUN_0040f330 (barn open), FUN_0040f570 (iris open) and FUN_0040fb40 (wipe);
// the pacing mirrors FUN_00410620, which waits until tick (start + i).
void MovieRuntime::runMovieTransition(DreamFactoryEngine &engine, uint16 op, const byte *pixels,
		int w, int h, int x0, int y0, int steps) {
	if (steps < 1)
		steps = 1;

	// Halved increments: the two-sided effects each cover half the extent.
	const int stepX = (w / (steps * 2)) + 1;
	const int stepY = (h / (steps * 2)) + 1;
	const int cx = w / 2, cy = h / 2;
	// The single-band wipe divides by steps rather than steps*2.
	const int wipeStep = (w / steps) + 1;

	// Starting geometry per op, matching each handler's pre-loop setup.
	int aL = 0, aT = 0, aR = 0, aB = 0; // primary band/box
	int bL = 0, bT = 0, bR = 0, bB = 0; // second band (barn-door ops only)
	const bool twoBands = (op == kMovieDrawBarnClose || op == kMovieDrawBarnOpen);
	switch (op) {
	case kMovieDrawBarnClose: // bands at the left and right edges, closing inwards
		aL = 0;         aT = 0; aR = stepX;  aB = h;
		bL = w - stepX; bT = 0; bR = w;      bB = h;
		break;
	case kMovieDrawBarnOpen: // bands at the centre, opening outwards
		aL = cx - stepX; aT = 0; aR = cx;         aB = h;
		bL = cx;         bT = 0; bR = cx + stepX; bB = h;
		break;
	case kMovieDrawIrisOpen: // box at the centre, growing on all four sides
		aL = cx - stepX; aT = cy - stepY; aR = cx + stepX; aB = cy + stepY;
		break;
	case kMovieDrawWipe: // band at the right edge, sweeping left
		aL = w - wipeStep; aT = 0; aR = w; aB = h;
		break;
	default:
		return;
	}

	const uint32 startMs = engine._system->getMillis();
	for (int i = 0; i < steps && !engine.shouldQuit(); ++i) {
		blitMovieBand(engine, pixels, w, h, x0, y0, aL, aT, aR, aB);
		if (twoBands)
			blitMovieBand(engine, pixels, w, h, x0, y0, bL, bT, bR, bB);
		engine._system->updateScreen();

		switch (op) {
		case kMovieDrawBarnClose: // both bands march towards the centre
			aL += stepX; aR += stepX;
			bL -= stepX; bR -= stepX;
			break;
		case kMovieDrawBarnOpen: // both bands march towards the edges
			aL -= stepX; aR -= stepX;
			bL += stepX; bR += stepX;
			break;
		case kMovieDrawIrisOpen: // grow on every side
			aL -= stepX; aR += stepX;
			aT -= stepY; aB += stepY;
			break;
		case kMovieDrawWipe: // band marches left
			aL -= wipeStep; aR -= wipeStep;
			break;
		default:
			break;
		}

		// One step per 60 Hz tick, and let the user abort (FUN_00410620 polls
		// the event pump and returns non-zero on quit/skip).
		const uint32 deadline = startMs + static_cast<uint32>((static_cast<uint64>(i + 1) * 1000 / 60));
		const uint32 now = engine._system->getMillis();
		if (now < deadline)
			engine._system->delayMillis(deadline - now);
		Common::Event event;
		while (engine._eventMan->pollEvent(event)) {
			if (event.type == Common::EVENT_QUIT || event.type == Common::EVENT_RETURN_TO_LAUNCHER)
				engine.requestQuit();
		}
	}

	// Trailing full-rect blit (every handler ends with FUN_00410660(rect, rect)).
	Graphics::Surface *screen = engine._system->lockScreen();
	if (!screen)
		return;
	copyFramePixelsToScreen(*screen, pixels, w, h, x0, y0);
	engine._system->unlockScreen();
	engine._system->updateScreen();
}

// Resolve a frame name (as used by GOTO buttons) to its index in the per-frame
// table, mirroring FUN_0040e050. Returns -1 if not found.
static int resolveFrameName(const Common::Array<MovieFrame> &frames, const Common::String &target) {
	for (uint i = 0; i < frames.size(); ++i)
		if (frames[i].name.equalsIgnoreCase(target))
			return static_cast<int>(i);
	return -1;
}

// Runs one movie command. Returns true when the current movie should stop its
// frame loop: END returns to the script, MARKER/GOSUB/RETURN switch movies (or
// return to the caller), and malformed/unknown commands abort this movie rather
// than spinning. GOTO/NEXT/PREV only set @p nextFrame.
static bool runMovieCommand(MovieCommand command, const Common::String &currentMovieName,
		int currentFrame, int frameCount, const Common::String &movieName,
		const Common::String &targetFrameName,
		const Common::Array<MovieFrame> &frames,
		Common::Array<MovieReturnFrame> &returnStack, int gotoFallbackFrame,
		int &nextFrame, Common::String &nextMovieName,
		int &nextMovieStartFrame, const char *source) {
	switch (command) {
	case MovieCommand::kEnd:
		return true;
	case MovieCommand::kGoto: {
		int idx = resolveFrameName(frames, targetFrameName);
		nextFrame = (idx >= 0) ? idx : gotoFallbackFrame;
		return false;
	}
	case MovieCommand::kMarker:
		if (!movieName.empty()) {
			debugC(1, kDebugMovies, "DreamFactory: movie '%s' MARKER at frame %d (%s) -> movie '%s'",
					currentMovieName.c_str(), currentFrame, source, movieName.c_str());
			nextMovieName = movieName;
			nextMovieStartFrame = 0;
		} else {
			warning("DreamFactory: movie '%s' marker %s frame %d has no movie name",
					currentMovieName.c_str(), source, currentFrame);
		}
		return true;
	case MovieCommand::kGosub: {
		int idx = resolveFrameName(frames, targetFrameName);
		if (!movieName.empty() && idx >= 0 && returnStack.size() < kMovieReturnStackLimit) {
			MovieReturnFrame ret;
			ret.name = currentMovieName;
			ret.frame = idx;
			returnStack.push_back(ret);
			nextMovieName = movieName;
			nextMovieStartFrame = 0;
		} else {
			warning("DreamFactory: movie '%s' cannot gosub %s movie '%s' target '%s'",
					currentMovieName.c_str(), source, movieName.c_str(),
					targetFrameName.c_str());
		}
		return true;
	}
	case MovieCommand::kReturn:
		if (!returnStack.empty()) {
			MovieReturnFrame ret = returnStack.back();
			returnStack.pop_back();
			nextMovieName = ret.name;
			nextMovieStartFrame = ret.frame;
		} else {
			warning("DreamFactory: movie '%s' return %s has an empty GOSUB stack",
					currentMovieName.c_str(), source);
		}
		return true;
	case MovieCommand::kNext:
		nextFrame = (currentFrame + 1 < frameCount) ? currentFrame + 1 : currentFrame;
		return false;
	case MovieCommand::kPrev:
		nextFrame = (currentFrame > 0) ? currentFrame - 1 : 0;
		return false;
	default:
		warning("DreamFactory: movie '%s' unsupported %s command %u at frame %d",
				currentMovieName.c_str(), source, static_cast<uint>(command), currentFrame);
		return true;
	}
}

// TI.EXE FUN_0042fcc0 ("DAT_00460aac == 0") reports whether the single cue
// channel is still sounding; FUN_0040e0b0 spins on it for frames flagged at
// event chunk +6 bit 0.
bool MovieRuntime::cueStillPlaying(DreamFactoryEngine &engine,
		const Common::Array<Audio::SoundHandle> &handles) {
	for (const Audio::SoundHandle &handle : handles) {
		if (engine._mixer->isSoundHandleActive(handle))
			return true;
	}
	return false;
}

// Parsed data is local to one movie, so decoded cues and borrowing resource
// views cannot survive replacement by a chained movie.
struct MovieData {
	ResourceFile resourceFile;
	Common::Array<uint32> fallbackResources;
	Common::Array<byte> pcm;
	Common::Array<MovieFrame> frames;
	bool skippable = false;
	bool hoverCursor = true;
	bool showsCursor = true;
	int actionCue1 = -1, actionCue2 = -1;
	int x = 0, y = 0;
	Common::Array<MovieSegment> segments;
	bool hasInteractive = false;
	uint64 frameSfxBytes = 0;
	bool playFrameSfxLive = false;
	Palette palette = {};
	bool hasPalette = false;

	bool load(const Common::String &name);
};

bool MovieData::load(const Common::String &name) {
	if (!resourceFile.open(name, "movie"))
		return false;
	const Archive &archive = resourceFile.archive();
	const Common::Array<byte> &fileData = resourceFile.data();
	// Full-screen frames are the resources whose info word's high half is 0x0200;
	// that word doubles as the frame's {uint16 H, uint16 W} header (W is always
	// 512, H is the low half: 264 for LOGO video, 384 for the PLAYMODE menu), so
	// the decoder source begins four bytes before the payload (see image.h). This
	// linear scan is only a fallback frame order; when the movie has a master
	// header we drive playback from its authoritative per-frame table below.
	for (uint32 i = 0; i < archive.getResourceCount(); ++i) {
		const Archive::Resource &res = archive.getResource(i);
		if (!res.empty && (res.info >> 16) == kFrameInfoHigh && res.dataOffset >= 4)
			fallbackResources.push_back(i);
	}

	// Build the soundtrack. A linear movie's master header (info==0x40000)
	// references two cue tables: a MUSIC table (the continuous score, played from
	// t=0) and an SFX/event table (named one-shots triggered by individual video
	// frames). For linear movies with music, decode the MUSIC cues into one track
	// and mix frame-triggered SFX into it at their frame start. For interactive
	// movies or movies with no music track, keep those SFX separate and play them
	// live when their frame is reached. This preserves LOGO.MOV's sample-locked
	// gunshots while allowing BEDCARDS.MOV's silent interactive stopwatch frames
	// to fire their voice cues.
	//
	// NB: do NOT concatenate the SFX resources onto the music track. Doing so
	// lengthened the track (so frames played too slowly) and made the effects
	// sound at their concatenation offset instead of their trigger frame.
	// The master header's frame table is authoritative when present. Otherwise
	// playback uses fallbackResources at a fixed cadence.
	Common::HashMap<uint32, Common::SharedPtr<Common::Array<byte> > > decodedFrameSfx;

	// Whether the movie may be skipped by the user. The original input handler
	// (TI.EXE FUN_0040e430) only honours the '.'/'Q'/'q' skip keys when the
	// master header flags byte (+0x18) has bit 0 set.
	// Hover cursor: while waiting on an interactive frame the original polls
	// FUN_0040e5b0 each event-loop pass, which swaps the cursor to "CURS131"
	// over any button whose flag bit 0x2 is set and back to "CURS.ARROW"
	// otherwise. Disabled wholesale by master header flag +0x18 bit 0x10.
	// Native FUN_0040ca80 selects the Win32 cursor state from master byte +6:
	// bit 0x10 hides it with FUN_00405210; otherwise FUN_004051b0 loads and
	// shows CURS.ARROW. Titanic's shipped movies use the visible-arrow path.

	// Action-cue frame indices, resolved from the master header's two cue-name
	// fields at +0x40/+0x50 (TI.EXE FUN_0040ca80 resolves them via FUN_0040e050
	// before the frame loop). Reaching cue N during playback sets bit N of the
	// action-frame mask that the script builtin actionframe(N) tests.

	// Screen position of the movie. The original never centers: each draw op
	// blits at the master header's QuickDraw rect {top,left,bottom,right}
	// @+0x86c offset by the s16 origin @+0x24 (x) / +0x26 (y) (FUN_0040eef0 ->
	// FUN_00410660 -> FUN_0041ad40). LOGO.MOV's rect is {0,0,264,512}: the
	// logo plays at the TOP of the screen.

	// Multi-segment movies (SINK1-6, LEAVE, DEBRIS, ...) chain segments via a
	// linked list: each master header's dword at byte +0x2c gives the next
	// segment's base resource index (0 = last segment). Within a segment, all
	// resource IDs in the per-frame table and sub-resource references are
	// RELATIVE to that segment's base. The native player (FUN_0040ca80) walks
	// this chain after each segment reaches its natural end (status 3). We
	// follow the same chain up front, concatenating every segment's frames into
	// the tables below so the frame loop plays them in order.

	int masterIdx = findMasterHeaderIndex(archive);
	if (masterIdx < 0) {
		warning("DreamFactory: movie '%s' has no master header; playing without audio", name.c_str());
	} else {
		// Walk the segment chain starting from the first master header.
		uint32 segBase = static_cast<uint32>(masterIdx);
		Common::HashMap<uint32, bool> visited; // guard against cycles in malformed data
		uint64 cumMs = 0; // accumulates frame hold times across all segments
		while (segBase < archive.getResourceCount() && !visited.contains(segBase)) {
			visited.setVal(segBase, true);
			const Archive::Resource &segmentResource = archive.getResource(segBase);
			const ResourceView segment = resourceEngineView(fileData, segmentResource);
			const byte *hdr = segment.dataAt(0, kMovieFrameTableOffset);
			if (!hdr)
				break;
			// Display/cursor properties come from the first segment only.
			if (segBase == static_cast<uint32>(masterIdx)) {
				skippable = (hdr[0x18] & 1) != 0;
				hoverCursor = (hdr[0x18] & 0x10) == 0;
				showsCursor = (hdr[6] & 0x10) == 0;
				// Dest position: rect {t,l} @+0x86c plus origin @+0x24/+0x26.
				x = static_cast<int16>(READ_LE_UINT16(hdr + 0x86e)) + static_cast<int16>(READ_LE_UINT16(hdr + 0x24));
				y = static_cast<int16>(READ_LE_UINT16(hdr + 0x86c)) + static_cast<int16>(READ_LE_UINT16(hdr + 0x26));
			}
			// Sub-resource indices are relative to segBase (TI.EXE
			// DAT_0045ef70 added to every id offset).
			uint32 musicTableIdx = segBase + READ_LE_UINT32(hdr + 0x64);
			uint32 sfxTableIdx   = segBase + READ_LE_UINT32(hdr + 0x60);
			uint32 pfCount = boundedRecordCount(READ_LE_UINT32(hdr + 0x878),
					segment.size(), kMovieFrameTableOffset, kMovieFrameRecordSize);
			const RecordRange frameRecords(segment, kMovieFrameTableOffset,
					pfCount, kMovieFrameRecordSize);

			MovieSegment movieSegment;
			movieSegment.startFrame = frames.size();

			// Extract this segment's palette from the master header (+0x6c..+0x86c,
			// 0x800 bytes = 256 ColorSpec entries x 8 bytes). The native player
			// (FUN_0040ca80 lines 228-236) copies these into DAT_0045ef94 on each
			// segment load. Multi-segment movies (SINK1-6, LEAVE) have different
			// palettes per segment; using segment 0's palette for all segments
			// corrupts the colours of every subsequent segment.
			if (segment.contains(0, 0x86c)) {
				Palette segPal = {};
				const byte *clut = hdr + 0x6c;
				for (uint32 k = 0; k < kPaletteColorCount; ++k) {
					const byte *ent = clut + k * 8;
					const uint32 color = Palette::colorOffset(k);
					segPal[color + 0] = ent[3]; // high byte of R
					segPal[color + 1] = ent[5]; // high byte of G
					segPal[color + 2] = ent[7]; // high byte of B
				}
				segPal[0] = segPal[1] = segPal[2] = 0;
				const uint32 lastColor = Palette::colorOffset(kPaletteLastColor);
				segPal[lastColor + 0] = segPal[lastColor + 1] = segPal[lastColor + 2] = 0xff;
				movieSegment.palette = segPal;
			}
			segments.push_back(movieSegment);
			// Minimum per-frame hold, in the scaled timer's units (FUN_00405130
			// returns timeGetTime * 0.06, so 1 unit == 1000/60 ms). For LOGO this
			// floor is 3 units == 50 ms == 20 fps.
			uint32 frameFloorUnits = READ_LE_UINT32(hdr + 0x1c);
			if (frameFloorUnits == 0)
				frameFloorUnits = 3;

			// 1. MUSIC track: decode each music-table cue's 22050 Hz resource in
			//    order. (Skip non-22050 cues such as the silent 11025 Hz pad.)
			if (musicTableIdx < archive.getResourceCount()) {
				const Archive::Resource &musicTableResource = archive.getResource(musicTableIdx);
				const ResourceView musicTable = resourceEngineView(fileData, musicTableResource);
				const byte *mt = musicTable.dataAt(0, kMusicCueTableOffset);
				if (mt) {
					uint32 mc = boundedRecordCount(READ_LE_UINT32(mt + kMusicCueCountOffset),
							musicTable.size(), kMusicCueTableOffset, kMusicCueRecordSize);
					const RecordRange musicCues(musicTable, kMusicCueTableOffset,
							mc, kMusicCueRecordSize);
					for (const ResourceView cueRecord : musicCues) {
						const byte *ent = cueRecord.dataAt(0, kMusicCueRecordSize);
						uint32 rid = segBase + READ_LE_UINT32(ent + 4);
						if (rid >= archive.getResourceCount())
							continue;
						const Archive::Resource &r = archive.getResource(rid);
						// The rate field lives at payload+0x18, so the resource
						// must be at least a full 0x1c-byte audio header.
						if (r.empty || r.info != kAudioResourceInfoTag || r.dataOffset < 4 ||
								r.length < 0x1c)
							continue;
						const ResourceView payload = resourcePayloadView(fileData, r);
						const byte *payloadData = payload.dataAt(0, 0x1c);
						if (!payloadData)
							continue;
						if (READ_LE_UINT32(payloadData + 0x18) != kAudioRate22050)
							continue;
						decodeCbxAudio(payloadData, static_cast<uint32>(payload.size()), pcm);
					}
				}
			}

			// 2. Per-frame timeline + SFX. Walk the per-frame table: each frame's
			//    event chunk (info==0x6 resource at record[+0x10]) gives its hold
			//    duration at engine offset +2 (floored by frameFloorUnits) and an
			//    optional cue NAME at +0x12. We accumulate the real start time of
			//    every frame, and for each named cue we look it up in the SFX
			//    table and MIX that effect into the music track at the frame's
			//    time (sample-add). The two LOGO frames that hold 333/500 ms make
			//    the video timeline (~16.6 s) slightly longer than the music
			//    (~15.9 s); the trailing fade plays over silence, as in the
			//    original.
			const ResourceView sfxTable = sfxTableIdx < archive.getResourceCount()
					? resourceEngineView(fileData, archive.getResource(sfxTableIdx)) : ResourceView();
			const byte *st = sfxTable.dataAt(0, kMovieSfxTableOffset);
			uint32 sfxCount = st
					? boundedRecordCount(READ_LE_UINT32(st + 4), sfxTable.size(),
							kMovieSfxTableOffset, kMovieSfxRecordSize)
					: 0;
			const RecordRange sfxRecords(sfxTable, kMovieSfxTableOffset,
					sfxCount, kMovieSfxRecordSize);
			for (const ResourceView frameRecord : frameRecords) {
				const byte *rec = frameRecord.dataAt(0, kMovieFrameRecordSize);
				uint32 eventId = segBase + READ_LE_UINT32(rec + 0x10);
				ResourceView eventData;
				if (eventId < archive.getResourceCount()) {
					const Archive::Resource &eventResource = archive.getResource(eventId);
					eventData = resourceEngineView(fileData, eventResource);
				}
				const byte *eb = eventData.dataAt(0, 0);

				MovieFrame frame;
				frame.startMs = cumMs;
				frame.videoResource = segBase + READ_LE_UINT32(rec + 0xc);
				frame.navigation = static_cast<MovieCommand>(eventData.contains(0, 2)
						? READ_LE_UINT16(eb) : static_cast<uint16>(MovieCommand::kNext));
				frame.waitForCue = eventData.contains(6) && (eb[6] & 1) != 0;
				frame.drawOp = eventData.contains(0, 0xe)
						? READ_LE_UINT16(eb + 0xc) : static_cast<uint16>(kMovieDrawBlit);
				frame.name = frameRecord.readPascalString(0x1a, true);
				// FUN_0040d710 commands 3/4 use a Pascal movie name at event
				// chunk +0x22; commands 2/4 use a Pascal frame target at +0x32.
				// The decompile's +0x19 target is word-indexed; in the raw
				// record+8 event frame used here it is byte offset +0x32.
				frame.navigationMovie = eventData.readPascalString(0x22, true);
				frame.navigationTarget = eventData.readPascalString(0x32, true);

				// Interactive buttons: raw event chunks store a u32 count at
				// +0x442 and 0x40-byte button records at +0x446. Ghidra's
				// FUN_0040d710 decompile reports +0x221 because of a widened
				// pointer type; FUN_0040e5b0 and HELP*.MOV raw dumps verify the
				// byte offsets.
				uint32 declaredButtonCount = 0;
				eventData.readUint32LE(kMovieButtonCountOffset, declaredButtonCount);
				uint32 btnCount = boundedRecordCount(declaredButtonCount,
						eventData.size(), kMovieButtonTableOffset, kMovieButtonRecordSize);
				const RecordRange buttons(eventData, kMovieButtonTableOffset,
						btnCount, kMovieButtonRecordSize);
				for (const ResourceView button : buttons) {
					const byte *br = button.dataAt(0, kMovieButtonRecordSize);
					MovieButton mb;
					mb.action = static_cast<MovieCommand>(READ_LE_UINT16(br));
					mb.flags  = br[2];
					// QuickDraw rect order {t, l, b, r} (see MovieButton).
					mb.top    = static_cast<int16>(READ_LE_UINT16(br + 8));
					mb.left   = static_cast<int16>(READ_LE_UINT16(br + 10));
					mb.bottom = static_cast<int16>(READ_LE_UINT16(br + 12));
					mb.right  = static_cast<int16>(READ_LE_UINT16(br + 14));
					mb.marker = button.readPascalString(0x20, true);
					mb.target = button.readPascalString(0x30, true);
					frame.buttons.push_back(mb);
				}

				if (eventData.valid()) {
					Common::String cue = eventData.readPascalString(0x12, true);
					if (!cue.empty()) {
						uint32 sfxResId = static_cast<uint32>(-1);
						for (const ResourceView sfxRecord : sfxRecords) {
							const byte *ent = sfxRecord.dataAt(0, kMovieSfxRecordSize);
							if (sfxRecord.readPascalString(0xa, true) == cue) {
								sfxResId = segBase + READ_LE_UINT32(ent + 4);
								break;
							}
						}
						if (sfxResId < archive.getResourceCount()) {
							Common::HashMap<uint32, Common::SharedPtr<Common::Array<byte> > >::const_iterator cached =
									decodedFrameSfx.find(sfxResId);
							if (cached != decodedFrameSfx.end()) {
								frame.sfx = cached->_value;
							} else {
								Common::SharedPtr<Common::Array<byte> > decoded(new Common::Array<byte>());
								const Archive::Resource &sr = archive.getResource(sfxResId);
								const ResourceView audioData = resourcePayloadView(fileData, sr);
								if (sr.info == kAudioResourceInfoTag && audioData.valid()) {
									// Frame-event cue resources are sometimes referenced more than
									// once in a movie. Cache them only for this playMovie() call:
									// most movies play once, so a persistent decoded-audio cache would
									// just retain large one-shot PCM buffers.
									decodeCbxAudio(audioData.dataAt(0, audioData.size()),
											static_cast<uint32>(audioData.size()), *decoded);
								}
								decodedFrameSfx[sfxResId] = decoded;
								frame.sfx = decoded;
							}
						}
					}
				}
				// Advance the timeline by this frame's hold (scaled units -> ms).
				uint32 units = frameFloorUnits;
				if (eventData.contains(0, 6)) {
					uint32 d = READ_LE_UINT32(eb + 2);
					if (d > units)
						units = d;
				}
				uint64 holdMs = static_cast<uint64>(units) * 1000 / 60;
				if (holdMs > 0xffffffffU)
					holdMs = 0xffffffffU;
				frame.holdMs = static_cast<uint32>(holdMs);
				frames.push_back(frame);
				cumMs += holdMs;
			}

			// 3. Action-cue frames: resolve the master header's cue names
			//    against the per-frame name column (TI.EXE iVar12/iVar9 in
			//    FUN_0040ca80). Missing names resolve to -1 (never matched).
			// Only the first segment's action cues are used.
			if (segBase == static_cast<uint32>(masterIdx)) {
				actionCue1 = resolveFrameName(frames, segment.readPascalString(0x40, true));
				actionCue2 = resolveFrameName(frames, segment.readPascalString(0x50, true));
			}

			// Follow the segment chain: master header dword at byte +0x2c gives
			// the next segment's base resource index.
			segBase = READ_LE_UINT32(hdr + 0x2c);
			if (segBase == 0)
				break; // 0 = last segment; never treat resource 0 as a segment
		}
	}

	// A movie is interactive if any frame carries buttons (the main menu,
	// BEDCARDS, BEDCAB, ...). Such movies loop their soundtrack while they wait
	// for the user; linear movies (the logo) play their track once.
	for (const MovieFrame &frame : frames) {
		if (!frame.buttons.empty()) {
			hasInteractive = true;
			break;
		}
	}
	playFrameSfxLive = hasInteractive || pcm.empty();
	for (const MovieFrame &frame : frames)
		if (frame.sfx)
			frameSfxBytes += frame.sfx->size();
	if (!playFrameSfxLive) {
		for (const MovieFrame &frame : frames) {
			if (!frame.sfx || frame.sfx->empty())
				continue;
			if (frame.startMs > 0xffffffffU * static_cast<uint64>(1000) / kAudioSampleRate)
				continue;
			const uint64 atSample = frame.startMs * kAudioSampleRate / 1000;
			mixSfx(pcm, *frame.sfx, atSample);
		}
	}


	hasPalette = loadPalette(fileData.begin(), fileData.size(), palette);
	return true;
}

// Mutable presentation state is separate from the authored movie data.
struct MoviePlaybackState {
	FrameSequence frames;
	Audio::SoundHandle audioHandle;
	Common::Array<Audio::SoundHandle> sfxHandles;
	bool paletteApplied = false;
	bool skip = false;
	bool hidCursor = false;
	bool hasAudio = false;
};

void MovieRuntime::playMovie(DreamFactoryEngine &engine, const Common::String &name) {
	if (name.empty())
		return;

	Common::String pendingMovieName = name;
	int pendingStartFrame = 0;
	Common::Array<MovieReturnFrame> returnStack;
	engine._actionFrameMask = 0; // playmovie clears the mask once (TI.EXE FUN_00446f80).

	while (!pendingMovieName.empty() && !engine.shouldQuit()) {
		const Common::String currentMovieName = pendingMovieName;
		const int initialFrame = pendingStartFrame;
		pendingMovieName.clear();

		MovieData movie;
		MoviePlaybackState playback;
		if (!movie.load(currentMovieName))
			return;
		const Archive &archive = movie.resourceFile.archive();
		const Common::Array<byte> &fileData = movie.resourceFile.data();

		// The movie palette is NOT programmed up front: the original keeps a
		// palette-dirty flag (DAT_0045ee90) and the per-frame draw command decides
		// — op 0x12 fades it in from black, op 0x11 fades out to black, any other
		// op snaps it on its first presented frame (FUN_0040eef0 preamble). This
		// keeps the menu's authored fade-out (clut left black) intact across the
		// movie boundary instead of flashing the palette on at movie start.

		// Esc skips a movie flagged skippable (header +0x18 bit 0); quit always
		// stops. Frame pacing is documented at the frame loop below; with no usable
		// timeline we fall back to a fixed cadence.
		const uint32 kFallbackFrameDelayMs = 66; // ~15 fps when there is no frame timeline
		Common::Event event;

		// Mirror the movie player's Win32 cursor setup: most Titanic movies show the
		// arrow even during linear playback, so keep ScummVM's software cursor active
		// instead of letting the host cursor appear over the movie surface. Remember
		// the actual decision: setGameCursor() can fail (missing TI.EXE), and the
		// restore below must undo what really happened or the cursor stays hidden.
		if (movie.showsCursor && engine.setGameCursor("CURS.ARROW")) {
			CursorMan.showMouse(true);
			playback.hidCursor = false;
		} else {
			CursorMan.showMouse(false);
			playback.hidCursor = true;
		}

		Common::String nextMovieName;
		int nextMovieStartFrame = 0;
		if (!movie.pcm.empty()) {
			Audio::SeekableAudioStream *stream = makeOwnedRawPcmStream(movie.pcm);
			if (stream) {
				if (movie.hasInteractive) {
					Audio::AudioStream *loop = new Audio::LoopingAudioStream(stream, 0);
					engine._mixer->playStream(Audio::Mixer::kSFXSoundType, &playback.audioHandle, loop);
				} else {
					engine._mixer->playStream(Audio::Mixer::kSFXSoundType, &playback.audioHandle, stream);
				}
				engine._mixer->setChannelVolume(playback.audioHandle, engine.audioRuntime().effectiveAudioVolume(255));
				playback.hasAudio = true;
			}
		}

		debugC(1, kDebugMovies, "DreamFactory: movie '%s' frames=%u audioBytes=%u frameSfxBytes=%llu audioMs=%u",
				currentMovieName.c_str(), movie.frames.empty() ? movie.fallbackResources.size() : movie.frames.size(),
				movie.pcm.size(), static_cast<unsigned long long>(movie.frameSfxBytes),
				static_cast<uint32>((static_cast<uint64>(movie.pcm.size()) * 1000 / kAudioSampleRate)));


		// Composite frames in order into a persistent surface (frames are
		// inter-coded) and present them. Esc skips a skippable movie; quit stops.
		//
		// There is NO stored frames-per-second field. Each frame carries its own
		// hold time in its event chunk (offset +2, floored by masterHdr[+0x1c]),
		// expressed in the scaled-timer units returned by TI.EXE FUN_00405130
		// (timeGetTime * 0.06, i.e. 1 unit == 1000/60 ms). We precompute the
		// start and hold time of every frame into movie.frames above.
		//
		// SYNC: linear-movie SFX (e.g. LOGO.MOV's gunshots) are mixed into the
		// soundtrack at their exact frame time, so they are locked to the music
		// sample-for-sample. To keep the *picture* locked to those sounds even when
		// frame decoding/blit lags, we clock the video off the real audio position
		// (the mixer's elapsed time) rather than a free-running wall clock, and DROP
		// the present of any frame whose slot has already passed (still decoding it,
		// since frames are inter-coded). This mirrors the original's adaptive
		// frame-drop in FUN_0040e8b0. Once the music ends (the video timeline can run
		// ~0.75 s longer than the music, e.g. LOGO's trailing fade) we continue on the
		// wall clock so the fade still plays out.
		const bool hasFrameTable = !movie.frames.empty();
		const int frameCount = hasFrameTable ? static_cast<int>(movie.frames.size())
				: static_cast<int>(movie.fallbackResources.size());
		if (frameCount == 0)
			warning("DreamFactory: movie '%s' has no frames to show", currentMovieName.c_str());

		uint32 wallStartMs = engine._system->getMillis();
		int fi = (initialFrame >= 0 && initialFrame < frameCount) ? initialFrame : 0;
		debugC(1, kDebugMovies, "DreamFactory: movie '%s' start frame=%d/%d segments=%d usePF=%d interactive=%d audio=%d skippable=%d cue1=%d cue2=%d",
				currentMovieName.c_str(), fi, frameCount, static_cast<int>(movie.segments.size()),
				hasFrameTable ? 1 : 0, movie.hasInteractive ? 1 : 0,
				playback.hasAudio ? 1 : 0, movie.skippable ? 1 : 0, movie.actionCue1, movie.actionCue2);
		while (fi >= 0 && fi < frameCount && !engine.shouldQuit() && !playback.skip) {
			const uint frameIndex = static_cast<uint>(fi);
			const MovieFrame *movieFrame = hasFrameTable ? &movie.frames[frameIndex] : nullptr;
			// Multi-segment movies code each segment independently. The native
			// player (FUN_0040ca80) re-enters the segment-load path (LAB_0040cad3)
			// for each segment, reinitializing the decode context. Resetting the
			// frame decoder here prevents the new segment's delta frames from
			// coding against the stale content of the previous segment's last frame.
			for (uint s = 1; s < movie.segments.size(); ++s) {
				if (movie.segments[s].startFrame == frameIndex) {
					playback.frames.clear();
					// Switch to this segment's palette and force it to be
					// re-programmed (each segment fades in from black via drawOp
					// 0x12, which sets playback.paletteApplied itself; for plain-blit
					// segments the dirty-flag path handles it).
					movie.palette = movie.segments[s].palette;
					playback.paletteApplied = false;
					break;
				}
			}
			uint32 resIdx = movieFrame ? movieFrame->videoResource : movie.fallbackResources[frameIndex];
			if (resIdx >= archive.getResourceCount()) {
				warning("DreamFactory: movie '%s' frame %d: resIdx %u out of range (%u resources)",
						currentMovieName.c_str(), fi, resIdx, archive.getResourceCount());
				break;
			}
			const Archive::Resource &res = archive.getResource(resIdx);
			const ResourceView encodedFrame = resourceEngineView(fileData, res);
			if (!encodedFrame.valid() ||
					playback.frames.applyFrame(encodedFrame.dataAt(0, encodedFrame.size()),
							static_cast<uint32>(encodedFrame.size())) == 0) {
				warning("DreamFactory: movie '%s' frame %d failed to decode", currentMovieName.c_str(), fi);
				break;
			}

			const bool interactive = movieFrame && !movieFrame->buttons.empty();

			// Record action-cue hits for the actionframe() builtin. The original
			// ORs the bits after decoding every frame it iterates (FUN_0043b800
			// call sites 0x0040d19a/0x0040d1af), clicked-to frames included.
			if (fi == movie.actionCue1)
				engine._actionFrameMask |= 1;
			if (fi == movie.actionCue2)
				engine._actionFrameMask |= 2;
			if (movie.playFrameSfxLive && movieFrame && movieFrame->sfx)
				playMovieFrameSfx(*engine._mixer, playback.sfxHandles, *movieFrame->sfx,
						engine.audioRuntime().effectiveAudioVolume(255));

			// Current playback clock: real audio position while the track plays,
			// else elapsed wall time (covers the post-music fade and silent movies).
			uint32 nowMs = (playback.hasAudio && engine._mixer->isSoundHandleActive(playback.audioHandle))
					? engine._mixer->getSoundElapsedTime(playback.audioHandle)
					: (engine._system->getMillis() - wallStartMs);
			uint64 frameEndMs = movieFrame ? movieFrame->startMs + movieFrame->holdMs
					: static_cast<uint64>(frameIndex + 1) * kFallbackFrameDelayMs;

			// Drop the present of a late linear frame to let the picture catch up to
			// the audio; always present in interactive movies. The original player
			// blits every frame (FUN_0040e8b0) before running the nav/button
			// interpreter (FUN_0040d710), so the pressed-button ("squished") frames
			// reached by a click are always shown. Frame-drop is a sync aid for the
			// long linear movies (the logo) only.
			bool present = movie.hasInteractive || nowMs < frameEndMs || fi + 1 >= frameCount;

			const byte *pixels = playback.frames.pixels();
			int w = playback.frames.width(), h = playback.frames.height();
			// Movie rect origin from the master header (no centering in the
			// original; FUN_00410660 + FUN_0041ad40): (0,0) for all known movies,
			// so 264-high frames play at the top of the screen.
			int x0 = movie.x;
			int y0 = movie.y;
			// This frame's draw command (FUN_0040eef0): 0x11/0x12 are the palette
			// fade-out/fade-in frames; anything else is a plain blit that snaps
			// the movie palette on if it is not up yet (the original's
			// palette-dirty preamble in FUN_0040eef0).
			uint16 drawOp = movieFrame ? movieFrame->drawOp : static_cast<uint16>(kMovieDrawBlit);
			bool fadedThisFrame = false;
			if (present) {
				if (movie.hasPalette && !playback.paletteApplied && drawOp != kMovieDrawFadeOut && drawOp != kMovieDrawFadeIn) {
					engine.programPalette(movie.palette);
					playback.paletteApplied = true;
				}
				if (isMovieTransitionOp(drawOp)) {
					// This frame is revealed by a geometric transition rather than a
					// straight blit. It runs over the frame's authored hold, one band
					// per 60 Hz tick, so the frame loop's own wait afterwards is
					// already (or nearly) satisfied.
					uint32 holdMs = movieFrame ? movieFrame->holdMs : kFallbackFrameDelayMs;
					int steps = static_cast<int>(holdMs * 60 / 1000);
					debugC(1, kDebugMovies, "DreamFactory: movie '%s' frame %d transition op %#04x over %d step(s)",
							currentMovieName.c_str(), fi, drawOp, steps);
					runMovieTransition(engine, drawOp, pixels, w, h, x0, y0, steps);
				} else {
					Graphics::Surface *screen = engine._system->lockScreen();
					if (!screen) {
						warning("DreamFactory: movie '%s' frame %d could not lock the screen",
								currentMovieName.c_str(), fi);
						break;
					}
					// Movie frames are opaque. Clip once and copy full rows instead of
					// doing a per-pixel getBasePtr() loop for every presented frame.
					copyFramePixelsToScreen(*screen, pixels, w, h, x0, y0);
					engine._system->unlockScreen();
					engine._system->updateScreen();
				}

				// Palette fade across this frame's authored hold time, one step per
				// 60 Hz tick (FUN_00410120 / FUN_004101a0): 0x12 = reveal the frame
				// from black, 0x11 = fade the frame out, leaving the palette black
				// for whatever follows (the menu -> room -> movie chain relies on it).
				if (movie.hasPalette && (drawOp == kMovieDrawFadeOut || drawOp == kMovieDrawFadeIn)) {
					uint32 holdMs = movieFrame ? movieFrame->holdMs : kFallbackFrameDelayMs;
					int steps = static_cast<int>(holdMs * 60 / 1000);
					Palette black = {};
					if (drawOp == kMovieDrawFadeIn) {
						engine.fadePaletteSteps(black, movie.palette, steps);
						playback.paletteApplied = true;
					} else {
						engine.fadePaletteSteps(movie.palette, black, steps);
						playback.paletteApplied = false;
					}
					fadedThisFrame = true;
				}
			}

			if (interactive) {
				// Interactive frame (the main menu): the original player suppresses
				// the frame's nav command and waits on the button table
				// (FUN_0040d710). Hold here, looping the soundtrack, until the user
				// clicks a button or quits. A click inside a button rect runs its
				// action: GOTO jumps to the named frame, NEXT/PREV step, END (and any
				// click on an action-1 button) returns from the movie.
				int nextFi = -1;
				// Hover cursor state: -1 unknown, 0 arrow, 1 hand ("CURS131").
				int hoverState = -1;
				while (nextFi < 0 && !engine.shouldQuit() && !playback.skip) {
					bool cursorDirty = false;
					const Common::Point oldMouse = engine._eventMan->getMousePos();
					// FUN_0040e5b0: every poll, point-in-rect the mouse against the
					// frame's hover-eligible buttons (flag bit 0x2; plain rect test,
					// no pixel mask) and show "CURS131" over one, "CURS.ARROW"
					// otherwise.
					if (movie.hoverCursor) {
						Common::Point m = engine._eventMan->getMousePos();
						int hover = 0;
						for (const MovieButton &mb : movieFrame->buttons) {
							if ((mb.flags & 0x2) && mb.contains(m.x - x0, m.y - y0)) {
								hover = 1;
								break;
							}
						}
						if (hover != hoverState) {
							engine.setGameCursor(hover ? "CURS131" : "CURS.ARROW");
							cursorDirty = true;
							hoverState = hover;
						}
					}
					while (engine._eventMan->pollEvent(event)) {
						if (event.type == Common::EVENT_MOUSEMOVE)
							cursorDirty = true;
						engine.handleMovieHotkeys(event, movie.skippable, playback.audioHandle, playback.skip);
						if (event.type == Common::EVENT_LBUTTONDOWN) {
							// TI.EXE FUN_0040e230 waits for event code 1
							// (WM_LBUTTONDOWN), then reads the current mouse point
							// from the movie port instead of using the queued event
							// payload. Keep click hit-testing consistent with the
							// hover helper above, which also polls the current point.
							Common::Point m = engine._eventMan->getMousePos();
							int fx = m.x - x0;
							int fy = m.y - y0;
							bool commandRan = false;
							for (const MovieButton &mb : movieFrame->buttons) {
								if (!mb.contains(fx, fy))
									continue;
								commandRan = true;
								if (runMovieCommand(mb.action, currentMovieName, fi, frameCount,
										mb.marker, mb.target, movie.frames, returnStack,
										fi, nextFi, nextMovieName,
										nextMovieStartFrame, "button")) {
									playback.skip = true;
								}
								debugC(1, kDebugMovies, "DreamFactory: movie '%s' button frame %d '%s' click (%d,%d) action %u marker '%s' target '%s' -> frame %d movie '%s'",
										currentMovieName.c_str(), fi,
										movieFrame->name.c_str(),
										fx, fy, static_cast<uint>(mb.action), mb.marker.c_str(), mb.target.c_str(),
										nextFi, nextMovieName.c_str());
								break;
							}
							// Stop draining the queue once a command has run: a second
							// buffered click (double-click) must not dispatch another
							// command and overwrite this one's target or double-push
							// the GOSUB return stack.
							if (commandRan)
								break;
						}
					}
					const Common::Point newMouse = engine._eventMan->getMousePos();
					if (oldMouse.x != newMouse.x || oldMouse.y != newMouse.y)
						cursorDirty = true;
					if (nextFi < 0 && !playback.skip) {
						// Composite the cursor at its new position and keep the
						// window live. Unlike native's hardware cursor, ScummVM's
						// software cursor only needs a present when motion or hover
						// state actually changed; unconditional swaps dominate
						// blocking interactive movie waits.
						if (cursorDirty) {
							engine.getDebugger()->onFrame();
							engine._system->updateScreen();
						}
						engine._system->delayMillis(10);
					}
				}
				if (nextFi >= 0)
					fi = nextFi;
				continue;
			}

			MovieCommand nav = movieFrame ? movieFrame->navigation : MovieCommand::kNext;

			// Interactive movies (the menu and its pressed-button frames) are paced
			// frame by frame off a local wall clock by each frame's own authored
			// hold, NOT the global audio timeline (a click jumps around the frame
			// table, so cumulative audio time is meaningless here). This is what
			// makes the "squished" pressed-button frame visible for its hold before
			// the menu returns.
			if (movie.hasInteractive) {
				// A 0x11/0x12 fade already spent this frame's hold on the palette
				// ramp (the original spreads the fade across the frame duration).
				uint32 holdMs = fadedThisFrame ? 0
						: (movieFrame ? movieFrame->holdMs : kFallbackFrameDelayMs);
				uint32 holdStart = engine._system->getMillis();
				// FUN_0040e0b0: a frame flagged at event chunk +6 bit 0 holds past
				// its authored time until the cue channel goes idle. This path is
				// taken by every frame of a movie that has any interactive frame at
				// all, which includes the narration-paced tour movies.
				const bool waitForCue = movieFrame && movieFrame->waitForCue;
				if (waitForCue)
					debugC(1, kDebugMovies, "DreamFactory: movie '%s' frame %d holds for its cue",
							currentMovieName.c_str(), fi);
				while (!engine.shouldQuit() && !playback.skip) {
					bool cursorDirty = false;
					const Common::Point oldMouse = engine._eventMan->getMousePos();
					while (engine._eventMan->pollEvent(event)) {
						if (event.type == Common::EVENT_MOUSEMOVE)
							cursorDirty = true;
						holdStart += engine.handleMovieHotkeys(event, movie.skippable, playback.audioHandle, playback.skip);
					}
					if (engine._system->getMillis() - holdStart >= holdMs &&
							!(waitForCue && cueStillPlaying(engine, playback.sfxHandles)))
						break;
					const Common::Point newMouse = engine._eventMan->getMousePos();
					if (cursorDirty || oldMouse.x != newMouse.x || oldMouse.y != newMouse.y)
						engine._system->updateScreen();
					engine._system->delayMillis(5);
				}
				if (engine.shouldQuit() || playback.skip)
					break;
				int nextFi = -1;
				if (runMovieCommand(nav, currentMovieName, fi, frameCount,
						movieFrame ? movieFrame->navigationMovie : Common::String(),
						movieFrame ? movieFrame->navigationTarget : Common::String(),
						movie.frames, returnStack, fi + 1, nextFi, nextMovieName,
						nextMovieStartFrame, "frame"))
					break;
				fi = nextFi >= 0 ? nextFi : fi + 1;
				continue;
			}

			if (nav == MovieCommand::kEnd) {
				// END: nav cmd 1 marks the last frame of a segment. The original
				// player (FUN_0040ca80: when FUN_0040d710 returns 1 -> the outer
				// segment loop checks for a next segment via master header +0x2c).
				// For multi-segment movies we flatten all segments into one frame
				// array: if the next frame index is the start of another segment,
				// advance to it. Otherwise (single-segment movie, or the last
				// segment's final frame) show this frame and RETURN from playback.
				const uint nextFrameIndex = frameIndex + 1;
				bool atSegmentBoundary = false;
				for (uint s = 1; s < movie.segments.size(); ++s) {
					if (movie.segments[s].startFrame == nextFrameIndex) {
						atSegmentBoundary = true;
						break;
					}
				}
				if (atSegmentBoundary) {
					fi = fi + 1;
					continue;
				}
				break;
			}

			// Wait until this frame's authored end time on the playback clock, then
			// run the same nav command interpreter path as FUN_0040d710.
			for (;;) {
				bool cursorDirty = false;
				const Common::Point oldMouse = engine._eventMan->getMousePos();
				while (engine._eventMan->pollEvent(event)) {
					if (movie.showsCursor && event.type == Common::EVENT_MOUSEMOVE)
						cursorDirty = true;
					wallStartMs += engine.handleMovieHotkeys(event, movie.skippable, playback.audioHandle, playback.skip);
				}
				if (engine.shouldQuit() || playback.skip)
					break;
				uint32 t = (playback.hasAudio && engine._mixer->isSoundHandleActive(playback.audioHandle))
						? engine._mixer->getSoundElapsedTime(playback.audioHandle)
						: (engine._system->getMillis() - wallStartMs);
				if (t >= frameEndMs) {
					// Same cue gate as the interactive path above.
					const bool waitForCue = movieFrame && movieFrame->waitForCue;
					if (!waitForCue || !cueStillPlaying(engine, playback.sfxHandles))
						break;
					// The original rebases the next deadline off "now" once the wait
					// ends (FUN_0040e8b0), so park our cumulative clock to match and
					// keep the following frames' spacing.
					wallStartMs = engine._system->getMillis() - static_cast<uint32>(frameEndMs);
				}
				if (movie.showsCursor)
					CursorMan.showMouse(true);
				const Common::Point newMouse = engine._eventMan->getMousePos();
				if (movie.showsCursor && (cursorDirty || oldMouse.x != newMouse.x || oldMouse.y != newMouse.y)) {
					engine.getDebugger()->onFrame();
					engine._system->updateScreen();
				}
				// Hidden-cursor movies have no software-cursor motion to present
				// between frames. Keep skippable movies at ~60 Hz polling for
				// responsive Esc/Ctrl+Q, but non-skippable movies only need to notice
				// pause/quit/global keys promptly. Polling those at ~30 Hz avoids
				// waking SDL/Cocoa's event pump twice as often in the sampled
				// scripted movie path without affecting cursor responsiveness.
				const uint32 pollCapMs = movie.skippable ? 16 : 33;
				const uint64 remaining = frameEndMs - t;
				engine._system->delayMillis(static_cast<uint32>(MIN<uint64>(remaining, pollCapMs)));
			}
			if (engine.shouldQuit() || playback.skip)
				break;
			int nextFi = -1;
			if (runMovieCommand(nav, currentMovieName, fi, frameCount,
					movieFrame ? movieFrame->navigationMovie : Common::String(),
					movieFrame ? movieFrame->navigationTarget : Common::String(),
					movie.frames, returnStack, fi + 1, nextFi, nextMovieName,
					nextMovieStartFrame, "frame"))
				break;
			fi = nextFi >= 0 ? nextFi : fi + 1;
		}

		debugC(1, kDebugMovies, "DreamFactory: movie '%s' frame loop ended: fi=%d/%d quit=%d skip=%d nextMovie='%s'",
				currentMovieName.c_str(), fi, frameCount, engine.shouldQuit() ? 1 : 0, playback.skip ? 1 : 0,
				nextMovieName.c_str());

		engine._mixer->stopHandle(playback.audioHandle);
		for (Audio::SoundHandle &handle : playback.sfxHandles)
			engine._mixer->stopHandle(handle);
		engine._eventMan->purgeKeyboardEvents();
		if (playback.hidCursor)
			CursorMan.showMouse(true);
		if (!nextMovieName.empty()) {
			pendingMovieName = nextMovieName;
			pendingStartFrame = nextMovieStartFrame;
			continue;
		}
		break;
	}
}


} // End of namespace DreamFactory
