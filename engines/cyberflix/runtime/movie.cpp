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
#include "common/file.h"
#include "common/hashmap.h"
#include "common/memstream.h"
#include "common/path.h"
#include "common/system.h"
#include "common/util.h"

#include "audio/audiostream.h"
#include "audio/mixer.h"

#include "graphics/cursorman.h"
#include "graphics/surface.h"

#include "cyberflix/archive.h"
#include "cyberflix/audio_helpers.h"
#include "cyberflix/console.h"
#include "cyberflix/cyberflix.h"
#include "cyberflix/image.h"
#include "cyberflix/audio/cbx_audio.h"
#include "cyberflix/resource_helpers.h"
#include "cyberflix/runtime/graphics_helpers.h"

namespace Cyberflix {

// Sample-add an 8-bit unsigned mono SFX buffer into the music track at the given
// sample offset, extending the track with silence (0x80) if needed and clamping.
static void mixSfx(Common::Array<byte> &track, const Common::Array<byte> &sfx, uint64 atSample) {
	if (sfx.empty())
		return;
	const uint64 end64 = atSample + sfx.size();
	if (atSample > 0xffffffffU || end64 > 0xffffffffU)
		return;
	const uint32 start = static_cast<uint32>(atSample);
	const uint32 end = static_cast<uint32>(end64);
	if (track.size() < end)
		track.resize(end, 0x80);
	for (uint32 i = 0; i < sfx.size(); ++i) {
		int v = (static_cast<int>(track[start + i]) - 0x80) + (static_cast<int>(sfx[i]) - 0x80);
		v = CLIP(v, -128, 127);
		track[start + i] = static_cast<byte>(v + 0x80);
	}
}

static void playMovieFrameSfx(Audio::Mixer *mixer, Common::Array<Audio::SoundHandle> &handles,
		const Common::Array<byte> &pcm, byte volume) {
	if (!mixer || pcm.empty())
		return;

	Audio::SoundHandle handle;
	Audio::SeekableAudioStream *stream = makeOwnedRawPcmStream(pcm);
	if (!stream)
		return;
	mixer->playStream(Audio::Mixer::kSFXSoundType, &handle, stream);
	mixer->setChannelVolume(handle, volume);
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
enum MovieDrawOp {
	kMovieDrawBlit = 0x10,    ///< plain blit
	kMovieDrawFadeOut = 0x11, ///< blit + palette fade to black across the hold
	kMovieDrawFadeIn = 0x12   ///< blit (palette black) + palette fade in
};

// Cap on nested GOSUB movies. The native player has no explicit stack limit;
// this is a ScummVM guard against a malformed MARKER/RETURN loop.
static const uint kMovieReturnStackLimit = 5;

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

struct MovieReturnFrame {
	Common::String name;
	int frame;
};

// Resolve a frame name (as used by GOTO buttons) to its index in the per-frame
// table, mirroring FUN_0040e050. Returns -1 if not found.
static int resolveFrameName(const Common::Array<Common::String> &names, const Common::String &target) {
	for (uint i = 0; i < names.size(); ++i)
		if (names[i].equalsIgnoreCase(target))
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
		const Common::Array<Common::String> &frameNames,
		Common::Array<MovieReturnFrame> &returnStack, int gotoFallbackFrame,
		int &nextFrame, Common::String &nextMovieName,
		int &nextMovieStartFrame, const char *source) {
	switch (command) {
	case MovieCommand::kEnd:
		return true;
	case MovieCommand::kGoto: {
		int idx = resolveFrameName(frameNames, targetFrameName);
		nextFrame = (idx >= 0) ? idx : gotoFallbackFrame;
		return false;
	}
	case MovieCommand::kMarker:
		if (!movieName.empty()) {
			debug(0, "Cyberflix: movie '%s' MARKER at frame %d (%s) -> movie '%s'",
					currentMovieName.c_str(), currentFrame, source, movieName.c_str());
			nextMovieName = movieName;
			nextMovieStartFrame = 0;
		} else {
			warning("Cyberflix: movie '%s' marker %s frame %d has no movie name",
					currentMovieName.c_str(), source, currentFrame);
		}
		return true;
	case MovieCommand::kGosub: {
		int idx = resolveFrameName(frameNames, targetFrameName);
		if (!movieName.empty() && idx >= 0 && returnStack.size() < kMovieReturnStackLimit) {
			MovieReturnFrame ret;
			ret.name = currentMovieName;
			ret.frame = idx;
			returnStack.push_back(ret);
			nextMovieName = movieName;
			nextMovieStartFrame = 0;
		} else {
			warning("Cyberflix: movie '%s' cannot gosub %s movie '%s' target '%s'",
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
			warning("Cyberflix: movie '%s' return %s has an empty GOSUB stack",
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
		warning("Cyberflix: movie '%s' unsupported %s command %u at frame %d",
				currentMovieName.c_str(), source, static_cast<uint>(command), currentFrame);
		return true;
	}
}

void MovieRuntime::playMovie(CyberflixEngine &engine, const Common::String &name) {
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
		pendingStartFrame = 0;

	Common::File file;
	if (!file.open(Common::Path(currentMovieName))) {
		warning("Cyberflix: could not open movie '%s'", currentMovieName.c_str());
		return;
	}

	int64 fileSize = file.size();
	if (fileSize <= 0) {
		warning("Cyberflix: could not stat movie '%s'", currentMovieName.c_str());
		return;
	}
	uint32 size = static_cast<uint32>(fileSize);
	// fileData is declared before archive so it outlives it: the archive owns a
	// stream that points into this buffer, and the movie loop below reads
	// payloads through raw pointers into it.
	Common::Array<byte> fileData(size);
	if (file.read(fileData.begin(), size) != size) {
		warning("Cyberflix: could not read movie '%s'", currentMovieName.c_str());
		return;
	}
	file.close();

	Archive archive;
	if (!archive.open(new Common::MemoryReadStream(fileData.begin(), size, DisposeAfterUse::NO), currentMovieName)) {
		warning("Cyberflix: '%s' is not a valid movie container", currentMovieName.c_str());
		return;
	}

	// Full-screen frames are the resources whose info word's high half is 0x0200;
	// that word doubles as the frame's {uint16 H, uint16 W} header (W is always
	// 512, H is the low half: 264 for LOGO video, 384 for the PLAYMODE menu), so
	// the decoder source begins four bytes before the payload (see image.h). This
	// linear scan is only a fallback frame order; when the movie has a master
	// header we drive playback from its authoritative per-frame table below.
	Common::Array<uint32> frames;
	for (uint32 i = 0; i < archive.getResourceCount(); ++i) {
		const Archive::Resource &res = archive.getResource(i);
		if (!res.empty && (res.info >> 16) == kFrameInfoHigh && res.dataOffset >= 4)
			frames.push_back(i);
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
	Common::Array<byte> pcmBuf;
	// Cumulative start time (ms) of each video frame; last entry is the total
	// duration. Built from the per-frame event chunks below. Empty => no usable
	// master header, in which case the frame loop falls back to a fixed cadence.
	Common::Array<uint64> frameStartMs;
	// Per-frame table, captured from the master header: the video resource id to
	// composite (event record +0xc) and the navigation command of its event
	// chunk. When populated this is the authoritative playback order; the
	// kFrameInfoHigh scan above is the fallback.
	Common::Array<uint32> pfVideoRes;
	Common::Array<MovieCommand> pfNavCmd;
	// Per-frame name (event record +0x1a, Pascal) used to resolve GOTO targets,
	// and the per-frame interactive button table (empty => non-interactive frame
	// that obeys its nav command; non-empty => the player holds the frame and
	// waits for a click, as the main menu does).
	Common::Array<Common::String> pfName;
	Common::Array<Common::String> pfNavMovie;
	Common::Array<Common::String> pfNavTarget;
	Common::Array<Common::Array<MovieButton> > pfButtons;
	// Decoded SFX named by each frame's event chunk. Linear movies with a music
	// buffer premix these at frame start; interactive or otherwise-silent movies
	// play them live when the frame is reached.
	Common::Array<Common::SharedPtr<Common::Array<byte> > > pfFrameSfx;
	Common::HashMap<uint32, Common::SharedPtr<Common::Array<byte> > > decodedFrameSfx;
	// Per-frame hold duration in ms (event chunk +2 in scaled timer units,
	// floored by masterHdr[+0x1c]). Used to pace interactive movies frame by
	// frame (the menu and its pressed-button frames), independent of the audio
	// timeline that paces linear movies.
	Common::Array<uint32> pfHoldMs;
	// Per-frame draw command (MovieDrawOp). These author the menu fade-out
	// (PLAYMODE 'GAME 2' 0x11) and the movie fade-ins (frame 0 0x12).
	Common::Array<uint16> pfDrawOp;

	// Whether the movie may be skipped by the user. The original input handler
	// (TI.EXE FUN_0040e430) only honours the '.'/'Q'/'q' skip keys when the
	// master header flags byte (+0x18) has bit 0 set.
	bool movieSkippable = false;
	// Hover cursor: while waiting on an interactive frame the original polls
	// FUN_0040e5b0 each event-loop pass, which swaps the cursor to "CURS131"
	// over any button whose flag bit 0x2 is set and back to "CURS.ARROW"
	// otherwise. Disabled wholesale by master header flag +0x18 bit 0x10.
	bool movieHoverCursor = true;
	// Native FUN_0040ca80 selects the Win32 cursor state from master byte +6:
	// bit 0x10 hides it with FUN_00405210; otherwise FUN_004051b0 loads and
	// shows CURS.ARROW. Titanic's shipped movies use the visible-arrow path.
	bool movieShowsCursor = true;

	// Action-cue frame indices, resolved from the master header's two cue-name
	// fields at +0x40/+0x50 (TI.EXE FUN_0040ca80 resolves them via FUN_0040e050
	// before the frame loop). Reaching cue N during playback sets bit N of the
	// action-frame mask that the script builtin actionframe(N) tests.
	int actionCue1 = -1, actionCue2 = -1;

	// Screen position of the movie. The original never centers: each draw op
	// blits at the master header's QuickDraw rect {top,left,bottom,right}
	// @+0x86c offset by the s16 origin @+0x24 (x) / +0x26 (y) (FUN_0040eef0 ->
	// FUN_00410660 -> FUN_0041ad40). LOGO.MOV's rect is {0,0,264,512}: the
	// logo plays at the TOP of the screen.
	int movieX = 0, movieY = 0;

	// Multi-segment movies (SINK1-6, LEAVE, DEBRIS, ...) chain segments via a
	// linked list: each master header's dword at byte +0x2c gives the next
	// segment's base resource index (0 = last segment). Within a segment, all
	// resource IDs in the per-frame table and sub-resource references are
	// RELATIVE to that segment's base. The native player (FUN_0040ca80) walks
	// this chain after each segment reaches its natural end (status 3). We
	// follow the same chain up front, concatenating every segment's frames into
	// the arrays below so the frame loop plays them in order.
	Common::Array<uint32> segmentStartFrame; // frame index where each segment begins
	Common::Array<Palette> segmentPalette;   // each segment's palette from its master header +0x6c

	int masterIdx = findMasterHeaderIndex(archive);
	if (masterIdx < 0) {
		warning("Cyberflix: movie '%s' has no master header; playing without audio", currentMovieName.c_str());
	} else {
		// Walk the segment chain starting from the first master header.
		uint32 segBase = static_cast<uint32>(masterIdx);
		Common::HashMap<uint32, bool> visited; // guard against cycles in malformed data
		uint64 cumMs = 0; // accumulates frame hold times across all segments
		while (segBase < archive.getResourceCount() && !visited.contains(segBase)) {
			visited.setVal(segBase, true);
			const byte *hdr = resourceEngineBase(fileData, archive.getResource(segBase));
			if (!hdr || hdr + 0x87c > fileData.end())
				break;
			// Display/cursor properties come from the first segment only.
			if (segBase == static_cast<uint32>(masterIdx)) {
				movieSkippable = (hdr[0x18] & 1) != 0;
				movieHoverCursor = (hdr[0x18] & 0x10) == 0;
				movieShowsCursor = (hdr[6] & 0x10) == 0;
				// Dest position: rect {t,l} @+0x86c plus origin @+0x24/+0x26.
				movieX = static_cast<int16>(READ_LE_UINT16(hdr + 0x86e)) + static_cast<int16>(READ_LE_UINT16(hdr + 0x24));
				movieY = static_cast<int16>(READ_LE_UINT16(hdr + 0x86c)) + static_cast<int16>(READ_LE_UINT16(hdr + 0x26));
			}
			// Sub-resource indices are relative to segBase (TI.EXE
			// DAT_0045ef70 added to every id offset).
			uint32 musicTableIdx = segBase + READ_LE_UINT32(hdr + 0x64);
			uint32 sfxTableIdx   = segBase + READ_LE_UINT32(hdr + 0x60);
			uint32 pfCount       = READ_LE_UINT32(hdr + 0x878);
			const byte *pfTable  = hdr + 0x87c;

			segmentStartFrame.push_back(pfVideoRes.size());

			// Extract this segment's palette from the master header (+0x6c..+0x86c,
			// 0x800 bytes = 256 ColorSpec entries x 8 bytes). The native player
			// (FUN_0040ca80 lines 228-236) copies these into DAT_0045ef94 on each
			// segment load. Multi-segment movies (SINK1-6, LEAVE) have different
			// palettes per segment; using segment 0's palette for all segments
			// corrupts the colours of every subsequent segment.
			if (hdr + 0x86c <= fileData.end()) {
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
				segmentPalette.push_back(segPal);
			} else {
				segmentPalette.push_back(Palette());
			}
			// Minimum per-frame hold, in the scaled timer's units (FUN_00405130
			// returns timeGetTime * 0.06, so 1 unit == 1000/60 ms). For LOGO this
			// floor is 3 units == 50 ms == 20 fps.
			uint32 frameFloorUnits = READ_LE_UINT32(hdr + 0x1c);
			if (frameFloorUnits == 0)
				frameFloorUnits = 3;

			// 1. MUSIC track: decode each music-table cue's 22050 Hz resource in
			//    order. (Skip non-22050 cues such as the silent 11025 Hz pad.)
			if (musicTableIdx < archive.getResourceCount()) {
				const byte *mt = resourceEngineBase(fileData, archive.getResource(musicTableIdx));
				if (mt && mt + 0x10e <= fileData.end()) {
					uint32 mc = READ_LE_UINT32(mt + 0x10a);
					for (uint32 e = 0; e < mc; ++e) {
						const byte *ent = mt + 0x10e + e * 0x1a;
						if (ent + 0x1a > fileData.end())
							break;
						uint32 rid = segBase + READ_LE_UINT32(ent + 4);
						if (rid >= archive.getResourceCount())
							continue;
						const Archive::Resource &r = archive.getResource(rid);
						// The rate field lives at payload+0x18, so the resource
						// must be at least a full 0x1c-byte audio header.
						if (r.empty || r.info != kAudioResourceInfoTag || r.dataOffset < 4 ||
								r.length < 0x1c)
							continue;
						const byte *payload = fileData.begin() + r.dataOffset;
						if (READ_LE_UINT32(payload + 0x18) != kAudioRate22050)
							continue;
						decodeCbxAudio(payload, r.length, pcmBuf);
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
			const byte *st = (sfxTableIdx < archive.getResourceCount())
					? resourceEngineBase(fileData, archive.getResource(sfxTableIdx)) : nullptr;
			uint32 sfxCount = (st && st + 8 <= fileData.end()) ? READ_LE_UINT32(st + 4) : 0;
			for (uint32 f = 0; f < pfCount; ++f) {
				const byte *rec = pfTable + f * 0x2a;
				if (rec + 0x2a > fileData.end())
					break;
				const byte *eb = nullptr;
				uint32 eventId = segBase + READ_LE_UINT32(rec + 0x10);
				uint32 ebLen = 0;
				if (eventId < archive.getResourceCount()) {
					eb = resourceEngineBase(fileData, archive.getResource(eventId));
					ebLen = archive.getResource(eventId).length;
				}

				frameStartMs.push_back(cumMs);
				pfVideoRes.push_back(segBase + READ_LE_UINT32(rec + 0xc));
				pfNavCmd.push_back(static_cast<MovieCommand>((eb && eb + 2 <= fileData.end())
						? READ_LE_UINT16(eb) : static_cast<uint16>(MovieCommand::kNext)));
				pfDrawOp.push_back((eb && eb + 0xe <= fileData.end())
						? READ_LE_UINT16(eb + 0xc) : static_cast<uint16>(kMovieDrawBlit));
				pfName.push_back(readPascalString(rec + 0x1a, fileData, true));
				// FUN_0040d710 commands 3/4 use a Pascal movie name at event
				// chunk +0x22; commands 2/4 use a Pascal frame target at +0x32.
				// The decompile's +0x19 target is word-indexed; in the raw
				// record+8 event frame used here it is byte offset +0x32.
				pfNavMovie.push_back((eb && eb + 0x23 <= fileData.end())
						? readPascalString(eb + 0x22, fileData, true) : Common::String());
				pfNavTarget.push_back((eb && eb + 0x33 <= fileData.end())
						? readPascalString(eb + 0x32, fileData, true) : Common::String());

				// Interactive buttons: raw event chunks store a u32 count at
				// +0x442 and 0x40-byte button records at +0x446. Ghidra's
				// FUN_0040d710 decompile reports +0x221 because of a widened
				// pointer type; FUN_0040e5b0 and HELP*.MOV raw dumps verify the
				// byte offsets.
				Common::Array<MovieButton> buttons;
				uint32 btnCount = (eb && ebLen >= 0x446) ? READ_LE_UINT32(eb + 0x442) : 0;
				if (eb && ebLen >= 0x446) // clamp the file-supplied count to the chunk
					btnCount = MIN<uint32>(btnCount, (ebLen - 0x446) / 0x40);
				for (uint32 b = 0; b < btnCount; ++b) {
					const byte *br = eb + 0x446 + b * 0x40;
					if (br + 0x40 > fileData.end())
						break;
					MovieButton mb;
					mb.action = static_cast<MovieCommand>(READ_LE_UINT16(br));
					mb.flags  = br[2];
					// QuickDraw rect order {t, l, b, r} (see MovieButton).
					mb.top    = static_cast<int16>(READ_LE_UINT16(br + 8));
					mb.left   = static_cast<int16>(READ_LE_UINT16(br + 10));
					mb.bottom = static_cast<int16>(READ_LE_UINT16(br + 12));
					mb.right  = static_cast<int16>(READ_LE_UINT16(br + 14));
					mb.marker = readPascalString(br + 0x20, fileData, true);
					mb.target = readPascalString(br + 0x30, fileData, true);
					buttons.push_back(mb);
				}
				pfButtons.push_back(buttons);

				Common::SharedPtr<Common::Array<byte> > frameSfx;
				if (eb) {
					Common::String cue = readPascalString(eb + 0x12, fileData, true);
					if (!cue.empty()) {
						uint32 sfxResId = static_cast<uint32>(-1);
						for (uint32 e = 0; e < sfxCount; ++e) {
							const byte *ent = st + 8 + e * 0x2a;
							if (ent + 0x2a > fileData.end())
								break;
							if (readPascalString(ent + 0xa, fileData, true) == cue) {
								sfxResId = segBase + READ_LE_UINT32(ent + 4);
								break;
							}
						}
						if (sfxResId < archive.getResourceCount()) {
							Common::HashMap<uint32, Common::SharedPtr<Common::Array<byte> > >::const_iterator cached =
									decodedFrameSfx.find(sfxResId);
							if (cached != decodedFrameSfx.end()) {
								frameSfx = cached->_value;
							} else {
								Common::SharedPtr<Common::Array<byte> > decoded(new Common::Array<byte>());
								const Archive::Resource &sr = archive.getResource(sfxResId);
								if (!sr.empty && sr.info == kAudioResourceInfoTag && sr.dataOffset >= 4) {
									// Frame-event cue resources are sometimes referenced more than
									// once in a movie. Cache them only for this playMovie() call:
									// most movies play once, so a persistent decoded-audio cache would
									// just retain large one-shot PCM buffers.
									decodeCbxAudio(fileData.begin() + sr.dataOffset, sr.length, *decoded);
								}
								decodedFrameSfx[sfxResId] = decoded;
								frameSfx = decoded;
							}
						}
					}
				}
				pfFrameSfx.push_back(frameSfx);

				// Advance the timeline by this frame's hold (scaled units -> ms).
				uint32 units = frameFloorUnits;
				if (eb && eb + 6 <= fileData.end()) {
					uint32 d = READ_LE_UINT32(eb + 2);
					if (d > units)
						units = d;
				}
				uint64 holdMs = static_cast<uint64>(units) * 1000 / 60;
				if (holdMs > 0xffffffffU)
					holdMs = 0xffffffffU;
				pfHoldMs.push_back(static_cast<uint32>(holdMs));
				cumMs += holdMs;
			}

			// 3. Action-cue frames: resolve the master header's cue names
			//    against the per-frame name column (TI.EXE iVar12/iVar9 in
			//    FUN_0040ca80). Missing names resolve to -1 (never matched).
			// Only the first segment's action cues are used.
			if (segBase == static_cast<uint32>(masterIdx)) {
				actionCue1 = resolveFrameName(pfName, readPascalString(hdr + 0x40, fileData, true));
				actionCue2 = resolveFrameName(pfName, readPascalString(hdr + 0x50, fileData, true));
			}

			// Follow the segment chain: master header dword at byte +0x2c gives
			// the next segment's base resource index.
			segBase = READ_LE_UINT32(hdr + 0x2c);
			if (segBase == 0)
				break; // 0 = last segment; never treat resource 0 as a segment
		}
		frameStartMs.push_back(cumMs); // total movie duration (all segments)
	}

	// A movie is interactive if any frame carries buttons (the main menu,
	// BEDCARDS, BEDCAB, ...). Such movies loop their soundtrack while they wait
	// for the user; linear movies (the logo) play their track once.
	bool hasInteractive = false;
	for (uint i = 0; i < pfButtons.size(); ++i)
		if (!pfButtons[i].empty()) {
			hasInteractive = true;
			break;
		}
	const bool playFrameSfxLive = hasInteractive || pcmBuf.empty();
	uint32 frameSfxBytes = 0;
	for (uint i = 0; i < pfFrameSfx.size(); ++i)
		if (pfFrameSfx[i])
			frameSfxBytes += pfFrameSfx[i]->size();
	if (!playFrameSfxLive) {
		for (uint i = 0; i < pfFrameSfx.size(); ++i) {
			if (!pfFrameSfx[i] || pfFrameSfx[i]->empty())
				continue;
			uint64 atMs = (i < frameStartMs.size()) ? frameStartMs[i] : 0;
			uint64 atSample = atMs * kAudioSampleRate / 1000;
			mixSfx(pcmBuf, *pfFrameSfx[i], atSample);
		}
	}

	// The movie palette is NOT programmed up front: the original keeps a
	// palette-dirty flag (DAT_0045ee90) and the per-frame draw command decides
	// — op 0x12 fades it in from black, op 0x11 fades out to black, any other
	// op snaps it on its first presented frame (FUN_0040eef0 preamble). This
	// keeps the menu's authored fade-out (clut left black) intact across the
	// movie boundary instead of flashing the palette on at movie start.
	Palette moviePal = {};
	bool haveMoviePal = loadPalette(fileData.begin(), size, moviePal);
	bool moviePalApplied = false;

	// Esc skips a movie flagged skippable (header +0x18 bit 0); quit always
	// stops. Frame pacing is documented at the frame loop below; with no usable
	// timeline we fall back to a fixed cadence.
	FrameSequence seq;
	const uint32 kFallbackFrameDelayMs = 66; // ~15 fps when there is no frame timeline
	bool skip = false;
	Common::Event event;

	// Mirror the movie player's Win32 cursor setup: most Titanic movies show the
	// arrow even during linear playback, so keep ScummVM's software cursor active
	// instead of letting the host cursor appear over the movie surface. Remember
	// the actual decision: setGameCursor() can fail (missing TI.EXE), and the
	// restore below must undo what really happened or the cursor stays hidden.
	bool movieHidCursor;
	if (movieShowsCursor && engine.setGameCursor("CURS.ARROW")) {
		CursorMan.showMouse(true);
		movieHidCursor = false;
	} else {
		CursorMan.showMouse(false);
		movieHidCursor = true;
	}

	Audio::SoundHandle audioHandle;
	Common::Array<Audio::SoundHandle> frameSfxHandles;
	Common::String nextMovieName;
	int nextMovieStartFrame = 0;
	bool hasMovieAudio = false;
	if (!pcmBuf.empty()) {
		Audio::SeekableAudioStream *stream = makeOwnedRawPcmStream(pcmBuf);
		if (stream) {
			if (hasInteractive) {
				Audio::AudioStream *loop = new Audio::LoopingAudioStream(stream, 0);
				engine._mixer->playStream(Audio::Mixer::kSFXSoundType, &audioHandle, loop);
			} else {
				engine._mixer->playStream(Audio::Mixer::kSFXSoundType, &audioHandle, stream);
			}
			engine._mixer->setChannelVolume(audioHandle, engine.audioRuntime().effectiveAudioVolume(255));
			hasMovieAudio = true;
		}
	}

	debug(0, "Cyberflix: movie '%s' frames=%u audioBytes=%u frameSfxBytes=%u audioMs=%u",
			currentMovieName.c_str(), pfVideoRes.empty() ? frames.size() : pfVideoRes.size(), pcmBuf.size(), frameSfxBytes,
			static_cast<uint32>((static_cast<uint64>(pcmBuf.size()) * 1000 / kAudioSampleRate)));

	// Composite frames in order into a persistent surface (frames are
	// inter-coded) and present them. Esc skips a skippable movie; quit stops.
	//
	// There is NO stored frames-per-second field. Each frame carries its own
	// hold time in its event chunk (offset +2, floored by masterHdr[+0x1c]),
	// expressed in the scaled-timer units returned by TI.EXE FUN_00405130
	// (timeGetTime * 0.06, i.e. 1 unit == 1000/60 ms). We precompute the
	// cumulative start time of every frame into frameStartMs above.
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
	const bool usePF = !pfVideoRes.empty();
	const int frameCount = usePF ? static_cast<int>(pfVideoRes.size()) : static_cast<int>(frames.size());
	if (frameCount == 0)
		warning("Cyberflix: movie '%s' has no frames to show", currentMovieName.c_str());

	uint32 wallStartMs = engine._system->getMillis();
	int fi = (initialFrame >= 0 && initialFrame < frameCount) ? initialFrame : 0;
	debug(0, "Cyberflix: movie '%s' start frame=%d/%d segments=%d usePF=%d interactive=%d audio=%d skippable=%d cue1=%d cue2=%d",
			currentMovieName.c_str(), fi, frameCount, static_cast<int>(segmentStartFrame.size()),
			usePF ? 1 : 0, hasInteractive ? 1 : 0,
			hasMovieAudio ? 1 : 0, movieSkippable ? 1 : 0, actionCue1, actionCue2);
	while (fi >= 0 && fi < frameCount && !engine.shouldQuit() && !skip) {
		const uint frameIndex = static_cast<uint>(fi);
		// Multi-segment movies code each segment independently. The native
		// player (FUN_0040ca80) re-enters the segment-load path (LAB_0040cad3)
		// for each segment, reinitializing the decode context. Resetting the
		// frame decoder here prevents the new segment's delta frames from
		// coding against the stale content of the previous segment's last frame.
		for (uint s = 1; s < segmentStartFrame.size(); ++s) {
			if (segmentStartFrame[s] == frameIndex) {
				seq.clear();
				// Switch to this segment's palette and force it to be
				// re-programmed (each segment fades in from black via drawOp
				// 0x12, which sets moviePalApplied itself; for plain-blit
				// segments the dirty-flag path handles it).
				if (s < segmentPalette.size()) {
					moviePal = segmentPalette[s];
					moviePalApplied = false;
				}
				break;
			}
		}
		uint32 resIdx = usePF ? pfVideoRes[frameIndex] : frames[frameIndex];
		if (resIdx >= archive.getResourceCount()) {
			warning("Cyberflix: movie '%s' frame %d: resIdx %u out of range (%u resources)",
					currentMovieName.c_str(), fi, resIdx, archive.getResourceCount());
			break;
		}
		const Archive::Resource &res = archive.getResource(resIdx);
		if (res.empty || res.dataOffset < 4 ||
				seq.applyFrame(fileData.begin() + res.dataOffset - 4, res.length + 4) == 0) {
			warning("Cyberflix: movie '%s' frame %d failed to decode", currentMovieName.c_str(), fi);
			break;
		}

		const bool interactive = usePF && frameIndex < pfButtons.size() && !pfButtons[frameIndex].empty();

		// Record action-cue hits for the actionframe() builtin. The original
		// ORs the bits after decoding every frame it iterates (FUN_0043b800
		// call sites 0x0040d19a/0x0040d1af), clicked-to frames included.
		if (fi == actionCue1)
			engine._actionFrameMask |= 1;
		if (fi == actionCue2)
			engine._actionFrameMask |= 2;
		if (playFrameSfxLive && frameIndex < pfFrameSfx.size() && pfFrameSfx[frameIndex])
			playMovieFrameSfx(engine._mixer, frameSfxHandles, *pfFrameSfx[frameIndex], engine.audioRuntime().effectiveAudioVolume(255));

		// Current playback clock: real audio position while the track plays,
		// else elapsed wall time (covers the post-music fade and silent movies).
		uint32 nowMs = (hasMovieAudio && engine._mixer->isSoundHandleActive(audioHandle))
				? engine._mixer->getSoundElapsedTime(audioHandle)
				: (engine._system->getMillis() - wallStartMs);
		uint64 frameEndMs = (frameIndex + 1 < frameStartMs.size())
				? frameStartMs[frameIndex + 1] : static_cast<uint64>(frameIndex + 1) * kFallbackFrameDelayMs;

		// Drop the present of a late linear frame to let the picture catch up to
		// the audio; always present in interactive movies. The original player
		// blits every frame (FUN_0040e8b0) before running the nav/button
		// interpreter (FUN_0040d710), so the pressed-button ("squished") frames
		// reached by a click are always shown. Frame-drop is a sync aid for the
		// long linear movies (the logo) only.
		bool present = hasInteractive || nowMs < frameEndMs || fi + 1 >= frameCount;

		const byte *pixels = seq.pixels();
		int w = seq.width(), h = seq.height();
		// Movie rect origin from the master header (no centering in the
		// original; FUN_00410660 + FUN_0041ad40): (0,0) for all known movies,
		// so 264-high frames play at the top of the screen.
		int x0 = movieX;
		int y0 = movieY;
		// This frame's draw command (FUN_0040eef0): 0x11/0x12 are the palette
		// fade-out/fade-in frames; anything else is a plain blit that snaps
		// the movie palette on if it is not up yet (the original's
		// palette-dirty preamble in FUN_0040eef0).
		uint16 drawOp = (usePF && frameIndex < pfDrawOp.size()) ? pfDrawOp[frameIndex] : static_cast<uint16>(kMovieDrawBlit);
		bool fadedThisFrame = false;
		if (present) {
			if (haveMoviePal && !moviePalApplied && drawOp != kMovieDrawFadeOut && drawOp != kMovieDrawFadeIn) {
				engine.programPalette(moviePal);
				moviePalApplied = true;
			}
			Graphics::Surface *screen = engine._system->lockScreen();
			// Movie frames are opaque. Clip once and copy full rows instead of
			// doing a per-pixel getBasePtr() loop for every presented frame.
			copyFramePixelsToScreen(*screen, pixels, w, h, x0, y0);
			engine._system->unlockScreen();
			engine._system->updateScreen();

			// Palette fade across this frame's authored hold time, one step per
			// 60 Hz tick (FUN_00410120 / FUN_004101a0): 0x12 = reveal the frame
			// from black, 0x11 = fade the frame out, leaving the palette black
			// for whatever follows (the menu -> room -> movie chain relies on it).
			if (haveMoviePal && (drawOp == kMovieDrawFadeOut || drawOp == kMovieDrawFadeIn)) {
				uint32 holdMs = (usePF && frameIndex < pfHoldMs.size()) ? pfHoldMs[frameIndex]
						: kFallbackFrameDelayMs;
				int steps = static_cast<int>(holdMs * 60 / 1000);
				Palette black = {};
				if (drawOp == kMovieDrawFadeIn) {
					engine.fadePaletteSteps(black, moviePal, steps);
					moviePalApplied = true;
				} else {
					engine.fadePaletteSteps(moviePal, black, steps);
					moviePalApplied = false;
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
			while (nextFi < 0 && !engine.shouldQuit() && !skip) {
				bool cursorDirty = false;
				const Common::Point oldMouse = engine._eventMan->getMousePos();
				// FUN_0040e5b0: every poll, point-in-rect the mouse against the
				// frame's hover-eligible buttons (flag bit 0x2; plain rect test,
				// no pixel mask) and show "CURS131" over one, "CURS.ARROW"
				// otherwise.
				if (movieHoverCursor) {
					Common::Point m = engine._eventMan->getMousePos();
					int hover = 0;
					for (uint b = 0; b < pfButtons[frameIndex].size(); ++b) {
						const MovieButton &mb = pfButtons[frameIndex][b];
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
					engine.handleMovieHotkeys(event, movieSkippable, audioHandle, skip);
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
						for (uint b = 0; b < pfButtons[frameIndex].size(); ++b) {
							const MovieButton &mb = pfButtons[frameIndex][b];
							if (!mb.contains(fx, fy))
								continue;
							commandRan = true;
							if (runMovieCommand(mb.action, currentMovieName, fi, frameCount,
									mb.marker, mb.target, pfName, returnStack,
									fi, nextFi, nextMovieName,
									nextMovieStartFrame, "button")) {
								skip = true;
							}
							debug(1, "Cyberflix: movie '%s' button frame %d '%s' click (%d,%d) action %u marker '%s' target '%s' -> frame %d movie '%s'",
									currentMovieName.c_str(), fi,
									(frameIndex < pfName.size()) ? pfName[frameIndex].c_str() : "",
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
				if (nextFi < 0 && !skip) {
					// Composite the cursor at its new position and keep the
					// window live. Unlike native's hardware cursor, ScummVM's
					// software cursor only needs a present when motion or hover
					// state actually changed; unconditional swaps dominate
					// blocking interactive movie waits.
					if (cursorDirty) {
						engine._console->onFrame();
						engine._system->updateScreen();
					}
					engine._system->delayMillis(10);
				}
			}
			if (nextFi >= 0)
				fi = nextFi;
			continue;
		}

		MovieCommand nav = usePF ? pfNavCmd[frameIndex] : MovieCommand::kNext;

		// Interactive movies (the menu and its pressed-button frames) are paced
		// frame by frame off a local wall clock by each frame's own authored
		// hold, NOT the global audio timeline (a click jumps around the frame
		// table, so cumulative audio time is meaningless here). This is what
		// makes the "squished" pressed-button frame visible for its hold before
		// the menu returns.
		if (hasInteractive) {
			// A 0x11/0x12 fade already spent this frame's hold on the palette
			// ramp (the original spreads the fade across the frame duration).
			uint32 holdMs = fadedThisFrame ? 0
					: ((frameIndex < pfHoldMs.size()) ? pfHoldMs[frameIndex] : kFallbackFrameDelayMs);
			uint32 holdStart = engine._system->getMillis();
			while (!engine.shouldQuit() && !skip) {
				bool cursorDirty = false;
				const Common::Point oldMouse = engine._eventMan->getMousePos();
				while (engine._eventMan->pollEvent(event)) {
					if (event.type == Common::EVENT_MOUSEMOVE)
						cursorDirty = true;
					holdStart += engine.handleMovieHotkeys(event, movieSkippable, audioHandle, skip);
				}
				if (engine._system->getMillis() - holdStart >= holdMs)
					break;
				const Common::Point newMouse = engine._eventMan->getMousePos();
				if (cursorDirty || oldMouse.x != newMouse.x || oldMouse.y != newMouse.y)
					engine._system->updateScreen();
				engine._system->delayMillis(5);
			}
			if (engine.shouldQuit() || skip)
				break;
			int nextFi = -1;
			if (runMovieCommand(nav, currentMovieName, fi, frameCount,
					(frameIndex < pfNavMovie.size()) ? pfNavMovie[frameIndex] : Common::String(),
					(frameIndex < pfNavTarget.size()) ? pfNavTarget[frameIndex] : Common::String(),
					pfName, returnStack, fi + 1, nextFi, nextMovieName,
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
			for (uint s = 1; s < segmentStartFrame.size(); ++s) {
				if (segmentStartFrame[s] == nextFrameIndex) {
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
				if (movieShowsCursor && event.type == Common::EVENT_MOUSEMOVE)
					cursorDirty = true;
				wallStartMs += engine.handleMovieHotkeys(event, movieSkippable, audioHandle, skip);
			}
			if (engine.shouldQuit() || skip)
				break;
			uint32 t = (hasMovieAudio && engine._mixer->isSoundHandleActive(audioHandle))
					? engine._mixer->getSoundElapsedTime(audioHandle)
					: (engine._system->getMillis() - wallStartMs);
			if (t >= frameEndMs)
				break;
			if (movieShowsCursor)
				CursorMan.showMouse(true);
			const Common::Point newMouse = engine._eventMan->getMousePos();
			if (movieShowsCursor && (cursorDirty || oldMouse.x != newMouse.x || oldMouse.y != newMouse.y)) {
				engine._console->onFrame();
				engine._system->updateScreen();
			}
			// Hidden-cursor movies have no software-cursor motion to present
			// between frames. Keep skippable movies at ~60 Hz polling for
			// responsive Esc/Ctrl+Q, but non-skippable movies only need to notice
			// pause/quit/global keys promptly. Polling those at ~30 Hz avoids
			// waking SDL/Cocoa's event pump twice as often in the sampled
			// scripted movie path without affecting cursor responsiveness.
			const uint32 pollCapMs = movieSkippable ? 16 : 33;
			const uint64 remaining = frameEndMs - t;
			engine._system->delayMillis(static_cast<uint32>(MIN<uint64>(remaining, pollCapMs)));
		}
		if (engine.shouldQuit() || skip)
			break;
		int nextFi = -1;
		if (runMovieCommand(nav, currentMovieName, fi, frameCount,
				(frameIndex < pfNavMovie.size()) ? pfNavMovie[frameIndex] : Common::String(),
				(frameIndex < pfNavTarget.size()) ? pfNavTarget[frameIndex] : Common::String(),
				pfName, returnStack, fi + 1, nextFi, nextMovieName,
				nextMovieStartFrame, "frame"))
			break;
		fi = nextFi >= 0 ? nextFi : fi + 1;
	}

	debug(0, "Cyberflix: movie '%s' frame loop ended: fi=%d/%d quit=%d skip=%d nextMovie='%s'",
			currentMovieName.c_str(), fi, frameCount, engine.shouldQuit() ? 1 : 0, skip ? 1 : 0,
			nextMovieName.c_str());

	engine._mixer->stopHandle(audioHandle);
	for (uint i = 0; i < frameSfxHandles.size(); ++i)
		engine._mixer->stopHandle(frameSfxHandles[i]);
	engine._eventMan->purgeKeyboardEvents();
	if (movieHidCursor)
		CursorMan.showMouse(true);
	if (!nextMovieName.empty()) {
		pendingMovieName = nextMovieName;
		pendingStartFrame = nextMovieStartFrame;
		continue;
	}
	break;
	}
}


} // End of namespace Cyberflix
