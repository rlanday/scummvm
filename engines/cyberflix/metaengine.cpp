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

#include "common/translation.h"

#include "engines/advancedDetector.h"

#include "cyberflix/cyberflix.h"
#include "cyberflix/detection.h"

namespace Cyberflix {

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
};

} // End of namespace Cyberflix

#if PLUGIN_ENABLED_DYNAMIC(CYBERFLIX)
	REGISTER_PLUGIN_DYNAMIC(CYBERFLIX, PLUGIN_TYPE_ENGINE, Cyberflix::CyberflixMetaEngine);
#else
	REGISTER_PLUGIN_STATIC(CYBERFLIX, PLUGIN_TYPE_ENGINE, Cyberflix::CyberflixMetaEngine);
#endif
