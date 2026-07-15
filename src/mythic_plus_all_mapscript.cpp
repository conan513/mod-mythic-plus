/*
 * Credits: silviu20092
 */

#include "ScriptMgr.h"
#include "GameTime.h"
#include "mythic_affix.h"
#include "mythic_plus.h"

class mythic_plus_all_mapscript : public AllMapScript
{
private:
    std::unordered_map<uint32, uint32> instanceTimer;
public:
    mythic_plus_all_mapscript() : AllMapScript("mythic_plus_all_mapscript",
        {
            ALLMAPHOOK_ON_PLAYER_ENTER_ALL,
            ALLMAPHOOK_ON_MAP_UPDATE,
            ALLMAPHOOK_ON_DESTROY_INSTANCE
        })
    {
    }

    void OnPlayerEnterAll(Map* map, Player* player) override
    {
        const MythicPlus::MythicPlusDungeonInfo* savedDungeon = sMythicPlus->GetSavedDungeonInfo(map->GetInstanceId());

        // edge case when players were in a M+ dungeon, server was restarted and the system was disabled in the meantime
        if (!sMythicPlus->IsEnabled() && map->GetInstanceId() != 0)
        {
            if (savedDungeon != nullptr)
            {
                if (savedDungeon->mythicLevel > 0)
                {
                    MythicPlus::BroadcastToPlayer(player, "Tried to join a saved Mythic Plus instance but now the system is disabled.");
                    MythicPlus::FallbackTeleport(player);
                    return;
                }
            }
            else
            {
                // save even non Mythic Plus dungeon in DB, this is required for further edge checks
                if (sMythicPlus->MatchMythicPlusMapDiff(map))
                    sMythicPlus->SaveDungeonInfo(map->GetInstanceId(), map->GetId(), 0, 0L, 0, 0, 0, false, false);
            }
        }

        if (sMythicPlus->CanMapBeMythicPlus(map))
        { 
            if (savedDungeon != nullptr)
            {
                if (!savedDungeon->isMythic)
                    return; // don't care about regular dungeons

                const MythicLevel* mythicLevel = sMythicPlus->GetMythicLevel(savedDungeon->mythicLevel);
                if (savedDungeon->mythicLevel > 0 && mythicLevel == nullptr)
                {
                    // edge case where a dungeon was saved as M+ but now the level does not longer exist (removed from DB)
                    MythicPlus::BroadcastToPlayer(player, "This dungeon was saved as Mythic Plus but now it's level does no longer exist");
                    MythicPlus::FallbackTeleport(player);
                    return;
                }

                if (mythicLevel == nullptr)
                    return;

                if (player->GetLevel() < DEFAULT_MAX_LEVEL)
                {
                    MythicPlus::BroadcastToPlayer(player, "You must be max level in order to join Mythic Plus");
                    MythicPlus::FallbackTeleport(player);
                    return;
                }

                MythicPlus::MapData* mapData = sMythicPlus->GetMapData(map);
                mapData->mythicPlusStartTimer = savedDungeon->startTime;
                mapData->done = savedDungeon->done;
                mapData->timeLimit = savedDungeon->timeLimit;
                mapData->deaths = savedDungeon->deaths;
                mapData->penaltyOnDeath = savedDungeon->penaltyOnDeath;

                if (!mapData->mythicLevel)
                {
                    MythicPlus::BroadcastToPlayer(player, "You have just joined a Mythic Plus dungeon. All affixes have been set and saved for this specific instance.");
                    mapData->mythicLevel = mythicLevel;
                }
                else
                    MythicPlus::BroadcastToPlayer(player, "You have joined an in progress Mythic Plus dungeon. All affixes were set and are active for this specific instance.");

                sMythicPlus->PrintMythicLevelInfo(mapData->mythicLevel, player);

                // --- Sync entering player's retail M+ UI ---
                MythicPlus::Utils::SendAddonMessage(player, "MODERNWOW", "M+:START:" + Acore::ToString(map->GetId()) + ":" + Acore::ToString(mapData->timeLimit) + ":" + Acore::ToString(mapData->mythicLevel->level) + ":" + Acore::ToString(mapData->deaths) + ":" + Acore::ToString(mapData->penaltyOnDeath));
                if (!mapData->done)
                {
                    long long diff = GameTime::GetGameTime().count() - mapData->mythicPlusStartTimer;
                    MythicPlus::Utils::SendAddonMessage(player, "MODERNWOW", "M+:TIME:" + Acore::ToString(diff) + ":" + Acore::ToString(mapData->deaths));

                    // Send current trash percentage
                    uint32 trashPct = mapData->totalTrashCount > 0
                        ? std::min(mapData->killedTrashCount * 100 / mapData->totalTrashCount, 100u)
                        : 0;
                    MythicPlus::Utils::SendAddonMessage(player, "MODERNWOW", "M+:TRASH:" + Acore::ToString(trashPct));

                    // Also sync keystone info
                    sMythicPlus->SyncKeystoneToClient(player);
                }


                QueryResult bResult = CharacterDatabase.Query("SELECT creature_entry FROM mythic_plus_dungeon_snapshot WHERE id = {} AND starttime = {}", map->GetInstanceId(), savedDungeon->startTime);
                if (bResult)
                {
                    do
                    {
                        Field* fields = bResult->Fetch();
                        uint32 entry = fields[0].Get<uint32>();
                        MythicPlus::Utils::SendAddonMessage(player, "MODERNWOW", "M+:BOSS:" + Acore::ToString(entry));
                    } while (bResult->NextRow());
                }


                std::ostringstream oss;
                if (!mapData->done)
                {
                    long long diff = GameTime::GetGameTime().count() - mapData->mythicPlusStartTimer;
                    mapData->updateTimer = diff * 1000;
                    if (diff + mapData->GetPenaltyTime() <= mapData->timeLimit)
                    {
                        oss << "Mythic Plus dungeon is in progress. Current timer: ";
                        oss << secsToTimeString(diff);
                        oss << ". Beat this timer to get loot: ";
                        oss << secsToTimeString(mapData->timeLimit);
                    }
                    else
                    {
                        oss << "Mythic Plus dungeon is in progress but no loot will be received. ";
                        oss << "Limit timer: " << secsToTimeString(mapData->timeLimit);
                        oss << ". Current timer: " << secsToTimeString(diff);
                        mapData->receiveLoot = false;
                    }

                    if (mapData->penaltyOnDeath > 0)
                    {
                        std::ostringstream oss2;
                        oss2 << "Dying will result in a penalty of ";
                        oss2 << secsToTimeString(mapData->penaltyOnDeath);
                        if (mapData->GetPenaltyTime() > 0)
                        {
                            oss2 << ". Current penalty: ";
                            oss2 << secsToTimeString(mapData->GetPenaltyTime());

                            oss << ". Current penalty: ";
                            oss << secsToTimeString(mapData->GetPenaltyTime());
                        }
                        else
                            oss2 << ". No deaths so far!";
                        MythicPlus::BroadcastToPlayer(player, MythicPlus::Utils::RedColored(oss2.str()));
                    }
                }
                else
                    oss << "Joined a Mythic Plus dungeon that is already done.";
                MythicPlus::AnnounceToPlayer(player, oss.str());
            }
        }
        else
        {
            // if there is a saved dungeon for this non M+ dungeon (as mythic+), it means the server was restarted and now the dungeon
            // is no longer M+ capable
            if (savedDungeon != nullptr && savedDungeon->isMythic)
            {
                MythicPlus::BroadcastToPlayer(player, "This dungeon was saved as Mythic Plus but now it can no longer be Mythic Plus");
                MythicPlus::FallbackTeleport(player);
                return;
            }
        }
    }

    void OnMapUpdate(Map* map, uint32 diff) override
    {
        if (sMythicPlus->IsMapInMythicPlus(map))
        {
            MythicPlus::MapData* mapData = sMythicPlus->GetMapData(map, false);
            ASSERT(mapData);
            if (mapData->receiveLoot && !mapData->done)
            {
                uint32 oldSeconds = mapData->updateTimer / 1000;
                mapData->updateTimer += diff;
                uint32 newSeconds = mapData->updateTimer / 1000;

                if (sMythicPlus->GetTimerBroadcastEnabled())
                {
                    uint32 penalty = mapData->GetPenaltyTime();
                    uint32 totalElapsed = newSeconds + penalty;
                    
                    if (totalElapsed <= mapData->timeLimit)
                    {
                        uint32 remaining = mapData->timeLimit - totalElapsed;
                        uint32 oldRemaining = mapData->timeLimit - (oldSeconds + penalty);

                        bool shouldAnnounce = false;
                        std::string timeStr;

                        auto checkCross = [&](uint32 targetSec) {
                            return oldRemaining > targetSec && remaining <= targetSec;
                        };

                        if (checkCross(1200)) { shouldAnnounce = true; timeStr = "20 minutes"; }
                        else if (checkCross(900)) { shouldAnnounce = true; timeStr = "15 minutes"; }
                        else if (checkCross(600)) { shouldAnnounce = true; timeStr = "10 minutes"; }
                        else if (checkCross(300)) { shouldAnnounce = true; timeStr = "5 minutes"; }
                        else if (checkCross(180)) { shouldAnnounce = true; timeStr = "3 minutes"; }
                        else if (checkCross(60))  { shouldAnnounce = true; timeStr = "1 minute"; }
                        else if (checkCross(30))  { shouldAnnounce = true; timeStr = "30 seconds"; }
                        else if (checkCross(10))  { shouldAnnounce = true; timeStr = "10 seconds"; }

                        if (shouldAnnounce)
                        {
                            std::ostringstream oss;
                            oss << "Mythic Plus: " << timeStr << " remaining!";
                            MythicPlus::BroadcastToMap(map, MythicPlus::Utils::Colored(oss.str(), "ffff00"));
                        }
                    }
                }

                if (mapData->updateTimer / 1000 + mapData->GetPenaltyTime() > mapData->timeLimit)
                {
                    MythicPlus::AnnounceToMap(map, "Time's up! You will not receive any Mythic Plus loot anymore.");
                    mapData->receiveLoot = false;
                }
            }

            for (auto* affix : mapData->mythicLevel->affixes)
                affix->HandlePeriodicEffectMap(map, diff);
        }
        else
        {
            if (sMythicPlus->CanMapBeMythicPlus(map))
            {
                // check if keystone was used
                MythicPlus::MapData* mapData = sMythicPlus->GetMapData(map, false);
                if (mapData != nullptr && mapData->keystoneTimer > 0 && mapData->mythicPlusStartTimer == 0)
                {
                    uint32 instanceId = map->GetInstanceId();
                    
                    if (instanceTimer[instanceId] > MythicPlus::KEYSTONE_START_TIMER)
                    {
                        const MythicPlus::MythicPlusDungeonInfo* dsave = sMythicPlus->GetSavedDungeonInfo(instanceId);
                        if (dsave != nullptr)
                        {
                            // at this point there shouldn't be any dungeon save available, it pretty much means the dungeon is saved as non M+
                            return;
                        }

                        mapData->mythicPlusStartTimer = MythicPlus::Utils::GameTimeCount();

                        const MythicLevel* mythicLevel = sMythicPlus->GetMythicLevel(mapData->keystoneLevel);
                        ASSERT(mythicLevel);

                        std::ostringstream oss;
                        oss << "Mythic Plus timer started! ";
                        oss << "Beat this timer to get loot: " << secsToTimeString(mythicLevel->timeLimit) << ". ";
                        oss << "Have fun!";
                        MythicPlus::AnnounceToMap(map, oss.str());

                        uint32 timeLimit = mythicLevel->timeLimit;
                        uint32 mlevel = mythicLevel->level;
                        sMythicPlus->SaveDungeonInfo(instanceId, map->GetId(), timeLimit, mapData->mythicPlusStartTimer, mlevel, sMythicPlus->GetPenaltyOnDeath(), 0, false);

                        mapData->mythicLevel = mythicLevel;
                        mapData->timeLimit = timeLimit;
                        mapData->deaths = 0;
                        mapData->penaltyOnDeath = sMythicPlus->GetPenaltyOnDeath();

                        // ── Retail: Count trash creatures for objective tracking ──
                        // Enumerate all map creatures; count non-boss, eligible ones.
                        {
                            uint32 trashCount = 0;
                            map->DoForAllCreatures([&](Creature* c) {
                                if (c && !c->IsDungeonBoss() && !sMythicPlus->IsFinalBoss(c->GetEntry())
                                    && sMythicPlus->CanProcessCreature(c) && c->IsAlive())
                                {
                                    ++trashCount;
                                }
                            });
                            mapData->totalTrashCount    = trashCount;
                            // Require 80% of trash to be killed (retail standard)
                            mapData->requiredTrashKills = (trashCount * 80) / 100;
                            mapData->killedTrashCount   = 0;

                            LOG_INFO("module", "Mythic Plus: Instance {} trash count={}, required={}",
                                     instanceId, trashCount, mapData->requiredTrashKills);
                        }

                        // --- Broadcast M+ START to all players on the map ---
                        Map::PlayerList const& players = map->GetPlayers();
                        for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
                        {
                            if (Player* pl = itr->GetSource())
                            {
                                MythicPlus::Utils::SendAddonMessage(pl, "MODERNWOW", "M+:START:" + Acore::ToString(map->GetId()) + ":" + Acore::ToString(timeLimit) + ":" + Acore::ToString(mlevel) + ":0:" + Acore::ToString(sMythicPlus->GetPenaltyOnDeath()));
                                MythicPlus::Utils::SendAddonMessage(pl, "MODERNWOW", "M+:TRASH:0");
                            }
                        }

                        if (mapData->penaltyOnDeath > 0)
                            sMythicPlus->BroadcastToMap(map, MythicPlus::Utils::RedColored("Dying will give a penalty of " + secsToTimeString(mapData->penaltyOnDeath)));

                    }
                    else
                        instanceTimer[instanceId] += diff;
                }
            }
        }
    }

    void OnDestroyInstance(MapInstanced* /*mapInstanced*/, Map* map) override
    {
        sMythicPlus->RemoveDungeonInfo(map->GetInstanceId());
    }
};

void AddSC_mythic_plus_all_mapscript()
{
    new mythic_plus_all_mapscript();
}
