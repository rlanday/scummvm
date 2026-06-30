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

#include "base/plugins.h"

#include "common/algorithm.h"
#include "common/savefile.h"
#include "common/system.h"
#include "common/translation.h"

#include "engines/advancedDetector.h"

#include "cyberflix/cyberflix.h"
#include "cyberflix/detection.h"

namespace Cyberflix {

static bool readFastSaveString(Common::SeekableReadStream &in, int64 end, Common::String &s) {
	if (in.pos() + 4 > end)
		return false;
	uint32 len = in.readUint32LE();
	if (in.pos() + len > end)
		return false;
	s.clear();
	if (!len)
		return !in.err();
	Common::Array<char> buf;
	buf.resize(len);
	return in.read(buf.begin(), len) == len && (s = Common::String(buf.begin(), len), true);
}

static bool readFastChunkHeader(Common::SeekableReadStream &in, char tag[5], int64 &end) {
	if (in.pos() + 8 > in.size())
		return false;
	if (in.read(tag, 4) != 4)
		return false;
	tag[4] = 0;
	uint32 size = in.readUint32LE();
	end = in.pos() + size;
	return end <= in.size();
}

static bool readCyberflixSaveDescription(Common::SeekableReadStream &in, Common::String &description) {
	char magic[5] = {};
	if (in.read(magic, 4) != 4 || memcmp(magic, "CFXS", 4))
		return false;
	if (in.readUint32LE() != 1)
		return false;

	char tag[5];
	int64 end = 0;
	if (!readFastChunkHeader(in, tag, end) || strcmp(tag, "HEAD"))
		return false;

	Common::String signature;
	if (!readFastSaveString(in, end, signature) ||
			!readFastSaveString(in, end, description))
		return false;
	return !in.err();
}

static const ADExtraGuiOptionsMap optionsList[] = {
	{
		GAMEOPTION_FONT_ANTIALIASING,
		{
			_s("Enable font anti-aliasing"),
			_s("Render CyberFlix text with smooth TrueType glyphs instead of the original monochrome Windows-style glyphs."),
			CYBERFLIX_OPTION_FONT_ANTIALIASING,
			false,
			0,
			0
		}
	},
	{
		GAMEOPTION_ENHANCED_PANORAMA_SETTLING,
		{
			_s("Enhance panorama settling after movement"),
			_s("After a SET turn or forward move reaches a named view, redraw the stable panorama view immediately for a sharper image than the original transition frame."),
			CYBERFLIX_OPTION_ENHANCED_PANORAMA_SETTLING,
			false,
			0,
			0
		}
	},
	AD_EXTRA_GUI_OPTIONS_TERMINATOR
};

class CyberflixMetaEngine : public AdvancedMetaEngine<Cyberflix::CyberflixGameDescription> {
public:
	const char *getName() const override {
		return "cyberflix";
	}

	const ADExtraGuiOptionsMap *getAdvancedExtraGuiOptions() const override {
		return optionsList;
	}

	Common::Error createInstance(OSystem *syst, Engine **engine, const Cyberflix::CyberflixGameDescription *desc) const override {
		*engine = new Cyberflix::CyberflixEngine(syst, desc);
		return Common::kNoError;
	}

	bool hasFeature(MetaEngineFeature f) const override {
		return checkExtendedSaves(f);
	}

	SaveStateList listSaves(const char *target) const override {
		Common::SaveFileManager *saveFileMan = g_system->getSavefileManager();
		Common::StringArray filenames = saveFileMan->listSavefiles(getSavegameFilePattern(target));

		SaveStateList saveList;
		for (const auto &file : filenames) {
			const char *slotStr = file.c_str() + file.size() - 2;
			const char *prev = slotStr - 1;
			if (*prev >= '0' && *prev <= '9')
				slotStr = prev;
			int slotNum = atoi(slotStr);
			if (slotNum < 0 || slotNum > getMaximumSaveSlot())
				continue;

			Common::ScopedPtr<Common::InSaveFile> saveFile(saveFileMan->openForLoading(file));
			Common::String description;
			if (!saveFile || !readCyberflixSaveDescription(*saveFile, description))
				continue;

			// CyberFlix saves are large because they persist open audio/runtime
			// state. The common extended-save metadata lives in a footer, and
			// seeking there inside a compressed save forces gzip to inflate most
			// of the file for every slot. The CyberFlix HEAD chunk is at the
			// front, so use it for the initial chooser list; the GUI still calls
			// querySaveMetaInfos() lazily for visible/selected slots that need
			// thumbnails, dates, and playtime.
			saveList.push_back(SaveStateDescriptor(this, slotNum, description));
		}

		Common::sort(saveList.begin(), saveList.end(), SaveStateDescriptorSlotComparator());
		return saveList;
	}
};

} // End of namespace Cyberflix

#if PLUGIN_ENABLED_DYNAMIC(CYBERFLIX)
	REGISTER_PLUGIN_DYNAMIC(CYBERFLIX, PLUGIN_TYPE_ENGINE, Cyberflix::CyberflixMetaEngine);
#else
	REGISTER_PLUGIN_STATIC(CYBERFLIX, PLUGIN_TYPE_ENGINE, Cyberflix::CyberflixMetaEngine);
#endif
