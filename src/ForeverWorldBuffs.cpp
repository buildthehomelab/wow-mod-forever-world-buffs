/*
 * mod-forever-world-buffs
 *
 * WoW Forever's world buff rule: world buffs only work outside raids. Walking into a raid pauses
 * them, and walking out gives them back with the time they had left. They can't make a raid easier,
 * so raids can be balanced around players without them, and nobody loses the buffs they gathered
 * for questing.
 *
 * On entering a raid map the buffs are removed and their remaining time is saved, in memory and in
 * the characters database, so a logout or a server restart inside the raid keeps them. Leaving the
 * raid adds them back with that time. A dead player can't take auras, so they come back on
 * resurrection instead.
 *
 * Released under the MIT License.
 */

#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "Tokenize.h"
#include "WorldSession.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

namespace
{
    // The classic world buffs: dragon heads and Rend's head, the Heart of Hakkar, Songflower
    // Serenade, the Dire Maul North tribute buffs and Sayge's Darkmoon Faire fortunes.
    constexpr char const* DEFAULT_SPELLS =
        "22888,16609,24425,15366,22817,22818,22820,23735,23736,23737,23738,23766,23767,23768,23769";

    struct Config
    {
        bool enabled = true;
        bool includeDungeons = false;
        bool announce = true;
        std::set<uint32> spells;
    };

    struct PausedBuff
    {
        uint32 spellId;
        int32 duration;    // ms left when it was paused
        int32 maxDuration; // ms, so the buff bar shows the same full length
    };

    Config config;

    // Paused buffs by character. Map threads reach this too (resurrection), so it's locked.
    std::unordered_map<ObjectGuid::LowType, std::vector<PausedBuff>> paused;
    std::mutex pausedLock;

    // Async writes share one queue that several DB workers pull from, so two writes for the same
    // buff can land in either order. Each write carries a number from this counter and the row only
    // takes a write newer than the one it holds, so a slow pause can't bring back a resumed buff.
    std::atomic<uint64> nextSeq{ 1 };

    void WriteState(ObjectGuid::LowType guid, uint32 spellId, int32 duration, int32 maxDuration, bool isPaused)
    {
        CharacterDatabase.Execute("INSERT INTO `character_forever_world_buffs` (`guid`, `spell`, `duration`, `max_duration`, `paused`, `seq`) "
            "VALUES ({}, {}, {}, {}, {}, {}) AS incoming ON DUPLICATE KEY UPDATE "
            "`duration` = IF(incoming.`seq` > `seq`, incoming.`duration`, `duration`), "
            "`max_duration` = IF(incoming.`seq` > `seq`, incoming.`max_duration`, `max_duration`), "
            "`paused` = IF(incoming.`seq` > `seq`, incoming.`paused`, `paused`), "
            "`seq` = GREATEST(`seq`, incoming.`seq`)",
            guid, spellId, duration, maxDuration, isPaused ? 1 : 0, nextSeq++);
    }

    std::set<uint32> ParseSpells(std::string const& list)
    {
        std::set<uint32> spells;
        for (std::string_view token : Acore::Tokenize(list, ',', false))
        {
            size_t first = token.find_first_not_of(" \t");
            if (first == std::string_view::npos)
                continue;
            token = token.substr(first, token.find_last_not_of(" \t") - first + 1);

            if (Optional<uint32> spellId = Acore::StringTo<uint32>(token))
                spells.insert(*spellId);
            else
                LOG_ERROR("module", "mod-forever-world-buffs: ForeverWorldBuffs.Spells has '{}', which is not a number; skipped.", token);
        }
        return spells;
    }

    // The first config load runs before the spell data is loaded, so this waits for OnStartup.
    void ValidateSpells()
    {
        for (uint32 spellId : config.spells)
            if (!sSpellMgr->GetSpellInfo(spellId))
                LOG_ERROR("module", "mod-forever-world-buffs: ForeverWorldBuffs.Spells has {}, which is not a spell.", spellId);
    }

    bool PausesWorldBuffs(Map const* map)
    {
        return config.enabled && map && (map->IsRaid() || (config.includeDungeons && map->IsNonRaidDungeon()));
    }

    std::string SpellName(Player* player, uint32 spellId)
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            return std::to_string(spellId);

        char const* name = spellInfo->SpellName[player->GetSession()->GetSessionDbcLocale()];
        return (name && *name) ? name : spellInfo->SpellName[LOCALE_enUS];
    }

    void Announce(Player* player, std::string const& prefix, std::vector<uint32> const& spellIds)
    {
        if (!config.announce || spellIds.empty() || player->GetSession()->IsHeadless())
            return;

        std::string names;
        for (uint32 spellId : spellIds)
            names += (names.empty() ? "" : ", ") + SpellName(player, spellId);

        ChatHandler(player->GetSession()).PSendSysMessage("{} {}.", prefix, names);
    }

    void Pause(Player* player)
    {
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        std::vector<uint32> pausedNow;

        for (uint32 spellId : config.spells)
        {
            Aura* aura = player->GetAura(spellId);
            if (!aura || aura->IsPermanent() || aura->GetDuration() <= 0)
                continue;

            PausedBuff buff{ spellId, aura->GetDuration(), aura->GetMaxDuration() };
            player->RemoveAurasDueToSpell(spellId);

            {
                std::lock_guard<std::mutex> lock(pausedLock);
                std::vector<PausedBuff>& buffs = paused[guid];

                // Can't normally happen (the same buff on you while one is paused), but keep the longer one.
                auto it = std::find_if(buffs.begin(), buffs.end(), [&](PausedBuff const& b) { return b.spellId == spellId; });
                if (it == buffs.end())
                    buffs.push_back(buff);
                else if (it->duration < buff.duration)
                    *it = buff;
                else
                    continue;
            }

            WriteState(guid, buff.spellId, buff.duration, buff.maxDuration, true);
            pausedNow.push_back(spellId);
        }

        Announce(player, player->GetMap()->IsRaid() ? "World buffs don't work in raids. Paused until you leave:"
            : "World buffs don't work in dungeons. Paused until you leave:", pausedNow);
    }

    void Resume(Player* player)
    {
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        std::vector<PausedBuff> buffs;

        {
            std::lock_guard<std::mutex> lock(pausedLock);
            auto it = paused.find(guid);
            if (it == paused.end())
                return;

            buffs = std::move(it->second);
            paused.erase(it);
        }

        std::vector<uint32> resumed;
        std::vector<PausedBuff> failed;
        for (PausedBuff const& buff : buffs)
        {
            // A fresh copy of the buff picked up since then wins if it has more time left.
            Aura* current = player->GetAura(buff.spellId);
            if (current && (current->IsPermanent() || current->GetDuration() >= buff.duration))
            {
                WriteState(guid, buff.spellId, 0, 0, false);
                continue;
            }

            if (Aura* aura = player->AddAura(buff.spellId, player))
            {
                // Never longer than a fresh cast: a spell script can cut the duration on apply, like
                // Songflower Serenade and Rallying Cry above level 63.
                bool cut = aura->GetDuration() < buff.duration;
                aura->SetMaxDuration(std::max(buff.maxDuration, buff.duration));
                aura->SetDuration(std::min(buff.duration, aura->GetDuration()));
                WriteState(guid, buff.spellId, 0, 0, false);
                if (!cut)
                    resumed.push_back(buff.spellId);
            }
            else if (!sSpellMgr->GetSpellInfo(buff.spellId))
            {
                // The spell is gone from the game data; there is nothing left to give back.
                WriteState(guid, buff.spellId, 0, 0, false);
                LOG_ERROR("module", "mod-forever-world-buffs: paused spell {} of {} ({}) no longer exists; dropped.", buff.spellId, player->GetName(), guid);
            }
            else
            {
                // Immune right now; keep it paused so it isn't lost, and try again next time.
                failed.push_back(buff);
                LOG_ERROR("module", "mod-forever-world-buffs: could not give spell {} back to {} ({}); still paused.", buff.spellId, player->GetName(), guid);
            }
        }

        if (!failed.empty())
        {
            std::lock_guard<std::mutex> lock(pausedLock);
            std::vector<PausedBuff>& stillPaused = paused[guid];
            stillPaused.insert(stillPaused.end(), failed.begin(), failed.end());
        }

        Announce(player, "Your world buffs are back:", resumed);
    }

    void Sync(Player* player)
    {
        if (!player || !player->IsInWorld())
            return;

        if (PausesWorldBuffs(player->GetMap()))
            Pause(player);
        // A dead player can't take the auras; OnPlayerResurrect brings them back.
        else if (player->IsAlive())
            Resume(player);
    }
}

class ForeverWorldBuffsWorldScript : public WorldScript
{
public:
    ForeverWorldBuffsWorldScript() : WorldScript("ForeverWorldBuffsWorldScript",
        { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP }) { }

    void OnAfterConfigLoad(bool reload) override
    {
        config.enabled         = sConfigMgr->GetOption<bool>("ForeverWorldBuffs.Enable", true);
        config.includeDungeons = sConfigMgr->GetOption<bool>("ForeverWorldBuffs.IncludeDungeons", false);
        config.announce        = sConfigMgr->GetOption<bool>("ForeverWorldBuffs.Announce", true);
        config.spells          = ParseSpells(sConfigMgr->GetOption<std::string>("ForeverWorldBuffs.Spells", DEFAULT_SPELLS));

        if (reload)
            ValidateSpells();
    }

    void OnStartup() override
    {
        ValidateSpells();

        if (QueryResult result = CharacterDatabase.Query("SELECT MAX(`seq`) FROM `character_forever_world_buffs`"))
            nextSeq = result->Fetch()[0].Get<uint64>() + 1;

        // Resumed rows only matter while writes are in flight, and none are at startup.
        CharacterDatabase.DirectExecute("DELETE FROM `character_forever_world_buffs` WHERE `paused` = 0");

        QueryResult result = CharacterDatabase.Query("SELECT `guid`, `spell`, `duration`, `max_duration` FROM `character_forever_world_buffs` WHERE `paused` = 1");
        if (!result)
            return;

        std::lock_guard<std::mutex> lock(pausedLock);
        uint32 count = 0;
        do
        {
            Field* fields = result->Fetch();
            paused[fields[0].Get<uint32>()].push_back({ fields[1].Get<uint32>(), fields[2].Get<int32>(), fields[3].Get<int32>() });
            ++count;
        } while (result->NextRow());

        LOG_INFO("server.loading", ">> mod-forever-world-buffs: loaded {} paused world buffs.", count);
    }
};

class ForeverWorldBuffsPlayerScript : public PlayerScript
{
public:
    ForeverWorldBuffsPlayerScript() : PlayerScript("ForeverWorldBuffsPlayerScript",
        { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_MAP_CHANGED, PLAYERHOOK_ON_PLAYER_RESURRECT, PLAYERHOOK_ON_DELETE_FROM_DB }) { }

    void OnPlayerLogin(Player* player) override
    {
        Sync(player);
    }

    // Also runs during login, before the client is ready; OnPlayerLogin handles that case.
    void OnPlayerMapChanged(Player* player) override
    {
        if (player->GetSession()->PlayerLoading())
            return;

        Sync(player);
    }

    void OnPlayerResurrect(Player* player, float /*restorePercent*/, bool& /*applySickness*/) override
    {
        Sync(player);
    }

    // Final deletes only (client, .character erase, account removal), not soft deletes, so a
    // restored character still has its paused buffs.
    void OnPlayerDeleteFromDB(CharacterDatabaseTransaction trans, uint32 guid) override
    {
        {
            std::lock_guard<std::mutex> lock(pausedLock);
            paused.erase(guid);
        }
        trans->Append("DELETE FROM `character_forever_world_buffs` WHERE `guid` = {}", guid);
    }
};

void AddForeverWorldBuffsScripts()
{
    new ForeverWorldBuffsWorldScript();
    new ForeverWorldBuffsPlayerScript();
}
