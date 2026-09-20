/* PCSX2's per-title database, read from inside the core.
 *
 * WHAT WAS WRONG. PCSX2 keeps what it knows about individual games in
 * bin/resources/GameIndex.yaml - the clamp and round modes a title's floating
 * point needs, its game fixes, its GS hardware fixes - and opens that file out
 * of its resources directory at runtime. A core has no resources directory: its
 * file system is whatever the project mounted, and nobody mounts an emulator's
 * database. So the open always failed, the database was always EMPTY, and every
 * PS2 game ran without its own corrections. Final Fantasy X (SLUS-20312) asks
 * for eeClampMode 3, whose comment upstream reads "Fixes reverse controls as
 * well as bosses and characters facing the wrong way during certain battles" -
 * which is what the machine did (chimera issue #117).
 *
 * WHY THE DATABASE IS THE MOVIE'S FRIEND HERE, not its enemy. This core used to
 * refuse the database outright, on the grounds that a movie must replay on the
 * settings it carries rather than on whatever a file said that week. That is
 * true of a file; it is not true of this. The database is COMPILED IN - by
 * waterbox/gen-gamedb.py, from the pinned submodule - so it is part of the
 * core's identity, and the package sha1 a movie cites pins it as exactly as it
 * pins the emulator. Two runs of the same core read the same database, and a
 * pin bump that changes the database changes the core, which a movie already
 * notices. What stays refused is the part that rewrites the GAME: the patches
 * and dynaPatches the database also carries are not compiled in, EnablePatches
 * is off, and Patch:: is stubbed (waterbox/stubs/frontend.cpp).
 *
 * WHAT THIS FILE IS. Upstream parses the yaml with rapidyaml, which it takes
 * from the system; a guest build has no system to take it from. So the tree is
 * flattened at build time into lines (waterbox/gen-gamedb.py) and read here with
 * no parser at all. Every NAME in those lines is still resolved through
 * upstream's own tables - parseHWFixName, ParseSpeedHackName, GetGameFixName -
 * so a pin bump that renames a fix is answered by upstream, and one that adds a
 * key stops the generator rather than being dropped quietly.
 */
#include "GameDatabase.h"
#include "GS/GS.h"

#include "common/Console.h"
#include "common/StringUtil.h"

#include "fmt/format.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

/* the blob, generated from the pinned submodule's GameIndex.yaml */
extern const char* ChimeraGameDB(size_t* size);

/* upstream's own name -> id table for the GS hardware fixes (patch 0024 gives
 * it external linkage rather than this file keeping a second copy of it) */
namespace GameDatabaseSchema
{
	std::optional<GSHWFixId> parseHWFixName(const std::string_view name);
}

namespace
{
	/* One line of the blob: "key", or "key\tvalue". A line with no tab is a key
	 * with no value, which is what upstream's n.has_val() == false means. */
	struct Field
	{
		std::string_view key;
		std::string_view value;
		bool has_value;
	};

	std::string_view NextLine(std::string_view& rest)
	{
		const size_t nl = rest.find('\n');
		std::string_view line = (nl == std::string_view::npos) ? rest : rest.substr(0, nl);
		rest = (nl == std::string_view::npos) ? std::string_view() : rest.substr(nl + 1);
		return line;
	}

	Field SplitField(std::string_view line)
	{
		const size_t tab = line.find('\t');
		if (tab == std::string_view::npos)
			return Field{line, std::string_view(), false};
		return Field{line.substr(0, tab), line.substr(tab + 1), true};
	}

	/* "group/key" -> "key", when the field belongs to that group */
	bool InGroup(const Field& f, const char* group, std::string_view& key)
	{
		const std::string_view prefix(group);
		if (f.key.size() <= prefix.size() + 1 || f.key.compare(0, prefix.size(), prefix) != 0 ||
			f.key[prefix.size()] != '/')
		{
			return false;
		}
		key = f.key.substr(prefix.size() + 1);
		return true;
	}

	/* A round mode, validated the way upstream validates it. */
	void SetRoundMode(FPRoundMode& out, const Field& f, const char* what, const std::string_view serial)
	{
		const std::optional<int> value = StringUtil::FromChars<int>(f.value);
		if (value.has_value() && value.value() >= 0 && value.value() < static_cast<int>(FPRoundMode::MaxCount))
			out = static_cast<FPRoundMode>(value.value());
		else
			Console.Error(fmt::format("GameDB: Invalid {} round mode '{}', specified for serial: '{}'.", what, f.value, serial));
	}

	void SetClampMode(GameDatabaseSchema::ClampMode& out, const Field& f)
	{
		/* Upstream does not range-check these; an unparseable one is left
		 * Undefined rather than becoming a mode nobody asked for. */
		const std::optional<int> value = StringUtil::FromChars<int>(f.value);
		if (value.has_value())
			out = static_cast<GameDatabaseSchema::ClampMode>(value.value());
	}

	void AddGameFix(GameDatabaseSchema::GameEntry& entry, const Field& f, const std::string_view serial)
	{
		/* Enum values don't end with Hack, but the database's do. */
		std::string fix(f.value);
		bool validated = false;
		if (fix.ends_with("Hack"))
		{
			fix.erase(fix.size() - 4);
			for (GamefixId id = GamefixId_FIRST; id < GamefixId_COUNT; id = static_cast<GamefixId>(static_cast<int>(id) + 1))
			{
				if (fix.compare(Pcsx2Config::GamefixOptions::GetGameFixName(id)) == 0 &&
					std::find(entry.gameFixes.begin(), entry.gameFixes.end(), id) == entry.gameFixes.end())
				{
					entry.gameFixes.push_back(id);
					validated = true;
					break;
				}
			}
		}
		if (!validated)
			Console.Error(fmt::format("GameDB: Invalid gamefix: '{}', specified for serial: '{}'. Dropping!", fix, serial));
	}

	void AddSpeedHack(GameDatabaseSchema::GameEntry& entry, std::string_view name, const Field& f,
		const std::string_view serial)
	{
		const std::optional<SpeedHack> id = Pcsx2Config::SpeedhackOptions::ParseSpeedHackName(name);
		const std::optional<int> value = StringUtil::FromChars<int>(f.value);
		if (id.has_value() && value.has_value() &&
			std::none_of(entry.speedHacks.begin(), entry.speedHacks.end(),
				[&id](const auto& it) { return it.first == id.value(); }))
		{
			entry.speedHacks.emplace_back(id.value(), value.value());
		}
		else
		{
			Console.Error(fmt::format("GameDB: Invalid speedhack: '{}={}', specified for serial: '{}'. Dropping!",
				name, f.value, serial));
		}
	}

	/* How many entries named a hardware-renderer hack this build has no table
	 * for. Reported once at the end rather than three hundred times. */
	unsigned s_hw_functions_skipped = 0;

	void AddHWFix(GameDatabaseSchema::GameEntry& entry, std::string_view name, const Field& f,
		const std::string_view serial)
	{
		const std::optional<GameDatabaseSchema::GSHWFixId> id = GameDatabaseSchema::parseHWFixName(name);
		std::optional<s32> value;
		if (id.has_value() && (id.value() == GameDatabaseSchema::GSHWFixId::GetSkipCount ||
								  id.value() == GameDatabaseSchema::GSHWFixId::BeforeDraw ||
								  id.value() == GameDatabaseSchema::GSHWFixId::MoveHandler))
		{
			/* These three do not name a VALUE: they name a FUNCTION in the
			 * hardware renderer's table of per-game hacks, which is compiled
			 * only when there is a hardware renderer to hold it. Without one
			 * there is nothing to look the name up in and nothing that would
			 * read the answer, so the fix is dropped and counted. */
#ifdef CHIMERA_GUEST_GL
			const std::string_view str_value(f.has_value ? f.value : std::string_view());
			if (id.value() == GameDatabaseSchema::GSHWFixId::GetSkipCount)
				value = GSLookupGetSkipCountFunctionId(str_value);
			else if (id.value() == GameDatabaseSchema::GSHWFixId::BeforeDraw)
				value = GSLookupBeforeDrawFunctionId(str_value);
			else
				value = GSLookupMoveHandlerFunctionId(str_value);

			if (value.value_or(-1) < 0)
			{
				Console.Error(fmt::format("GameDB: Invalid GS HW Fix Value for '{}' in '{}': '{}'", name, serial, str_value));
				return;
			}
#else
			s_hw_functions_skipped++;
			return;
#endif
		}
		else
		{
			value = f.has_value ? StringUtil::FromChars<s32>(f.value) : std::optional<s32>(1);
		}
		if (!id.has_value() || !value.has_value())
		{
			Console.Error(fmt::format("GameDB: Invalid GS HW Fix: '{}' specified for serial '{}'. Dropping!", name, serial));
			return;
		}
		entry.gsHWFixes.emplace_back(id.value(), value.value());
	}
} // namespace

void ChimeraInitGameDB(std::unordered_map<std::string, GameDatabaseSchema::GameEntry>& db)
{
	size_t size = 0;
	const char* text = ChimeraGameDB(&size);
	std::string_view rest(text, size);

	std::string serial;
	GameDatabaseSchema::GameEntry entry;
	bool have_entry = false;

	const auto flush = [&db, &serial, &entry, &have_entry]() {
		if (have_entry)
			db.emplace(std::move(serial), std::move(entry));
		entry = GameDatabaseSchema::GameEntry();
		have_entry = false;
	};

	while (!rest.empty())
	{
		const std::string_view line = NextLine(rest);
		if (line.empty())
			continue;

		if (line[0] != '\t')
		{
			/* a serial, and the start of its entry. Serials are stored
			 * lower-case, because that is how they are looked up. */
			flush();
			serial = StringUtil::toLower(line);
			if (db.count(serial) == 1)
			{
				/* the generator would have to have emitted one twice */
				Console.Error(fmt::format("GameDB: Duplicate serial '{}' found in GameDB. Skipping, Serials are case-insensitive!", serial));
				continue;
			}
			have_entry = true;
			continue;
		}
		if (!have_entry)
			continue;

		const Field f = SplitField(line.substr(1));
		std::string_view key;
		if (f.key == "name")
		{
			entry.name = std::string(f.value);
		}
		else if (InGroup(f, "roundModes", key))
		{
			if (key == "eeRoundMode")
				SetRoundMode(entry.eeRoundMode, f, "EE", serial);
			else if (key == "eeDivRoundMode")
				SetRoundMode(entry.eeDivRoundMode, f, "EE division", serial);
			else if (key == "vuRoundMode")
			{
				SetRoundMode(entry.vu0RoundMode, f, "VU", serial);
				entry.vu1RoundMode = entry.vu0RoundMode;
			}
			else if (key == "vu0RoundMode")
				SetRoundMode(entry.vu0RoundMode, f, "VU0", serial);
			else if (key == "vu1RoundMode")
				SetRoundMode(entry.vu1RoundMode, f, "VU1", serial);
		}
		else if (InGroup(f, "clampModes", key))
		{
			if (key == "eeClampMode")
				SetClampMode(entry.eeClampMode, f);
			else if (key == "vuClampMode")
			{
				SetClampMode(entry.vu0ClampMode, f);
				entry.vu1ClampMode = entry.vu0ClampMode;
			}
			else if (key == "vu0ClampMode")
				SetClampMode(entry.vu0ClampMode, f);
			else if (key == "vu1ClampMode")
				SetClampMode(entry.vu1ClampMode, f);
		}
		else if (f.key == "gameFixes")
		{
			AddGameFix(entry, f, serial);
		}
		else if (InGroup(f, "speedHacks", key))
		{
			AddSpeedHack(entry, key, f, serial);
		}
		else if (InGroup(f, "gsHWFixes", key))
		{
			AddHWFix(entry, key, f, serial);
		}
		else if (f.key == "memcardFilters")
		{
			entry.memcardFilters.emplace_back(f.value);
		}
		else
		{
			Console.Error(fmt::format("GameDB: Unknown field '{}' for serial '{}'.", f.key, serial));
		}
	}
	flush();

	if (s_hw_functions_skipped != 0)
	{
		Console.WriteLn("GameDB: %u hardware-renderer hack functions skipped: this build has no hardware renderer",
			s_hw_functions_skipped);
	}
}

/* WHAT DOES THIS CORE KNOW ABOUT ONE SERIAL. Asked by the "gamedb_probe"
 * setting, answered before anything boots, and needing no disc: the database is
 * compiled in, so the question can be put to a machine with an empty tray.
 *
 * It exists because the failure this whole file is about was SILENT. An empty
 * database looks exactly like a full one from outside - every game simply runs
 * slightly wrong, for months. The gate asks this question on every run
 * (waterbox/run-gate.sh), and a bug report about a per-title fix can be
 * answered without the reporter's disc.
 */
void ChimeraGameDBProbe(const std::string_view serial)
{
	const GameDatabaseSchema::GameEntry* entry = GameDatabase::findGame(serial);
	if (entry == nullptr)
	{
		Console.WriteLn(fmt::format("GameDB probe: {} not found", serial));
		return;
	}

	const auto round = [](FPRoundMode m) {
		return (m < FPRoundMode::MaxCount) ? static_cast<int>(m) : -1;
	};
	std::string fixes;
	for (const GamefixId id : entry->gameFixes)
		fmt::format_to(std::back_inserter(fixes), "{}{}", fixes.empty() ? "" : ",",
			Pcsx2Config::GamefixOptions::GetGameFixName(id));

	Console.WriteLn(fmt::format(
		"GameDB probe: {} name=\"{}\" eeRound={} eeDivRound={} vu0Round={} vu1Round={} "
		"eeClamp={} vu0Clamp={} vu1Clamp={} gameFixes=[{}] speedHacks={} gsHWFixes={}",
		serial, entry->name, round(entry->eeRoundMode), round(entry->eeDivRoundMode),
		round(entry->vu0RoundMode), round(entry->vu1RoundMode),
		static_cast<int>(entry->eeClampMode), static_cast<int>(entry->vu0ClampMode),
		static_cast<int>(entry->vu1ClampMode), fixes, entry->speedHacks.size(),
		entry->gsHWFixes.size()));
}
