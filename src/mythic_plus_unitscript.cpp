/*
 * Credits: silviu20092
 */

#include "ScriptMgr.h"
#include "GameTime.h"
#include "mythic_plus.h"
#include "mythic_affix.h"

class mythic_plus_unitscript : public UnitScript
{
public:
    mythic_plus_unitscript() : UnitScript("mythic_plus_unitscript", true,
        {
            UNITHOOK_ON_DAMAGE,
            UNITHOOK_ON_UNIT_DEATH,
            UNITHOOK_ON_UNIT_ENTER_EVADE_MODE
        })
    {
    }

    void OnDamage(Unit* attacker, Unit* victim, uint32& /*damage*/) override
    {
        if (attacker && victim && attacker->GetMap() == victim->GetMap())
        {
            Creature* creature = victim->ToCreature();
            if (!creature || (!creature->IsDungeonBoss() && !sMythicPlus->IsFinalBoss(creature->GetEntry())))
                return;

            Map* map = victim->GetMap();
            if (!sMythicPlus->IsMapInMythicPlus(map))
                return;

            MythicPlus::CreatureData* creatureData = sMythicPlus->GetCreatureData(creature);
            if (creatureData->engageTimer > 0)
                return;

            if (!attacker->ToPlayer() && !attacker->IsControlledByPlayer())
                return;

            creatureData->engageTimer = MythicPlus::Utils::GameTimeCount();

            const Map::PlayerList& playerList = map->GetPlayers();
            for (Map::PlayerList::const_iterator itr = playerList.begin(); itr != playerList.end(); ++itr)
            {
                if (Player* player = itr->GetSource())
                {
                    const std::string& cname = MythicPlus::Utils::GetCreatureName(player, creature);
                    std::ostringstream oss;
                    oss << "Engaged " << cname << ". Good luck!";
                    MythicPlus::AnnounceToPlayer(player, oss.str());
                    MythicPlus::BroadcastToPlayer(player, oss.str());
                }
            }
        }
    }

    void OnUnitDeath(Unit* unit, Unit* killer) override
    {
        if (unit && killer && unit->GetMap() == killer->GetMap())
        {
            Creature* creature = unit->ToCreature();
            if (!creature)
                return;

            MythicPlus::CreatureData* creatureData = sMythicPlus->GetCreatureData(creature, false);
            if (creatureData == nullptr || creatureData->engageTimer == 0)
                return;

            Map* map = creature->GetMap();

            uint64 gameTime = GameTime::GetGameTime().count();
            uint64 diff = gameTime - creatureData->engageTimer;
            std::string downAfterStr = secsToTimeString(diff);

            MythicPlus::MythicPlusDungeonInfo* savedDungeon = sMythicPlus->GetSavedDungeonInfo(map->GetInstanceId());
            ASSERT(savedDungeon);

            creatureData->engageTimer = 0;

            bool finalBoss = sMythicPlus->IsFinalBoss(creature->GetEntry());
            bool isBoss    = creature->IsDungeonBoss() || finalBoss;
            MythicPlus::MapData* mapData = sMythicPlus->GetMapData(map, false);
            ASSERT(mapData);

            // ── Retail: Trash kill counting ─────────────────────────────────
            // Count non-boss kills towards the trash objective.
            if (!isBoss && !mapData->done)
            {
                mapData->killedTrashCount++;
                // Broadcast trash progress to all players on the map
                BroadcastTrashProgress(map, mapData);
            }

            const Map::PlayerList& playerList = map->GetPlayers();
            for (Map::PlayerList::const_iterator itr = playerList.begin(); itr != playerList.end(); ++itr)
            {
                if (Player* player = itr->GetSource())
                {
                    const std::string& cname = MythicPlus::Utils::GetCreatureName(player, creature);
                    std::ostringstream oss;
                    oss << cname << " was bested in " << downAfterStr;
                    oss << ". Congratulations!";
                    MythicPlus::AnnounceToPlayer(player, oss.str());
                    MythicPlus::BroadcastToPlayer(player, oss.str());

                    // --- Sync boss kill with client GUI ---
                    if (isBoss)
                        MythicPlus::Utils::SendAddonMessage(player, "MODERNWOW", "M+:BOSS:" + Acore::ToString(creature->GetEntry()));

                    if (finalBoss)
                    {
                        std::ostringstream oss2;
                        oss2 << "All bosses defeated! Timer: " << secsToTimeString(gameTime - savedDungeon->startTime);

                        // ── Retail: Only complete if trash objective also met ─
                        bool trashComplete = (mapData->requiredTrashKills == 0 ||
                                             mapData->killedTrashCount >= mapData->requiredTrashKills);
                        if (!trashComplete)
                        {
                            uint32 pct = mapData->totalTrashCount > 0
                                ? (mapData->killedTrashCount * 100 / mapData->totalTrashCount)
                                : 100;
                            oss2 << " Kill more enemies! (" << pct << "% / 100%)";
                        }

                        MythicPlus::AnnounceToPlayer(player, oss2.str());
                        MythicPlus::BroadcastToPlayer(player, oss2.str());
                    }
                }
            }

            sMythicPlus->AddDungeonSnapshot(map->GetInstanceId(),
                map->GetId(),
                map->GetDifficulty(),
                savedDungeon->startTime,
                gameTime,
                diff,
                savedDungeon->timeLimit,
                0, "",   // player guid/name not per-player here
                savedDungeon->mythicLevel,
                creature->GetEntry(),
                finalBoss,
                false,   // rewarded updated below
                mapData->penaltyOnDeath,
                mapData->deaths,
                mapData->mythicLevel ? mapData->mythicLevel->randomAffixCount : 0
            );

            // ── Retail: Dungeon completion — requires all bosses + trash % ──
            if (finalBoss && !mapData->done)
            {
                bool trashComplete = (mapData->requiredTrashKills == 0 ||
                                     mapData->killedTrashCount >= mapData->requiredTrashKills);
                if (trashComplete)
                {
                    CompleteDungeon(map, mapData, savedDungeon, gameTime);
                }
                else
                {
                    // Boss dead but trash not done — mark final boss as killed so we
                    // can finalise once trash reaches 100% (checked below in trash kills)
                    mapData->done = false;  // not yet
                    // Store that final boss is already defeated using a sentinel
                    // We reuse killedTrashCount: if killedTrashCount reaches required
                    // the next trash kill will trigger CompleteDungeon.
                }
            }
            // If final boss was already killed (done still false = waiting for trash)
            // check again now that a trash mob died
            else if (!finalBoss && !isBoss && !mapData->done)
            {
                // Check if the final boss entry appears in dungeon snapshots (final boss already dead)
                bool finalBossAlreadyDead = false;
                QueryResult bResult = CharacterDatabase.Query(
                    "SELECT 1 FROM mythic_plus_dungeon_snapshot WHERE id = {} AND starttime = {} AND is_final_boss = 1",
                    map->GetInstanceId(), savedDungeon->startTime);
                if (bResult)
                    finalBossAlreadyDead = true;

                if (finalBossAlreadyDead)
                {
                    bool trashComplete = (mapData->requiredTrashKills == 0 ||
                                         mapData->killedTrashCount >= mapData->requiredTrashKills);
                    if (trashComplete)
                        CompleteDungeon(map, mapData, savedDungeon, gameTime);
                }
            }
        }
    }

private:
    // Broadcasts current trash kill percentage to all players on the map.
    void BroadcastTrashProgress(Map* map, MythicPlus::MapData* mapData) const
    {
        if (mapData->totalTrashCount == 0)
            return;

        uint32 pct = mapData->killedTrashCount * 100 / mapData->totalTrashCount;
        pct = std::min(pct, 100u);

        Map::PlayerList const& players = map->GetPlayers();
        for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
        {
            if (Player* pl = itr->GetSource())
                MythicPlus::Utils::SendAddonMessage(pl, "MODERNWOW", "M+:TRASH:" + Acore::ToString(pct));
        }
    }

    // Finalises a Mythic+ dungeon run — gives rewards, updates vault, triggers keystone logic.
    void CompleteDungeon(Map* map, MythicPlus::MapData* mapData,
                         MythicPlus::MythicPlusDungeonInfo* savedDungeon, uint64 gameTime)
    {
        mapData->done = true;

        sMythicPlus->SaveDungeonInfo(map->GetInstanceId(), map->GetId(), mapData->timeLimit,
            mapData->mythicPlusStartTimer, mapData->mythicLevel->level,
            mapData->penaltyOnDeath, mapData->deaths, true);

        Map::PlayerList const& players = map->GetPlayers();
        for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
        {
            if (Player* player = itr->GetSource())
            {
                if (mapData->receiveLoot)
                    sMythicPlus->Reward(player, mapData->mythicLevel->reward);

                sMythicPlus->UpdateGreatVault(player, savedDungeon->mythicLevel);

                MythicPlus::Utils::SendAddonMessage(player, "MODERNWOW",
                    "M+:END:" + Acore::ToString(gameTime - savedDungeon->startTime) +
                    ":" + Acore::ToString(mapData->receiveLoot ? 1 : 0));

                std::ostringstream oss;
                oss << "Mythic Plus complete! Time: " << secsToTimeString(gameTime - savedDungeon->startTime);
                MythicPlus::AnnounceToPlayer(player, oss.str());
                MythicPlus::BroadcastToPlayer(player, MythicPlus::Utils::GreenColored(oss.str()));
            }
        }

        sMythicPlus->AutoUpgradeKeystoneForMap(map, gameTime - savedDungeon->startTime,
                                                savedDungeon->timeLimit, mapData->receiveLoot);
    }


    void OnUnitEnterEvadeMode(Unit* unit, uint8 /*evadeReason*/) override
    {
        if (unit && unit->ToCreature())
        {
            Creature* creature = unit->ToCreature();
            MythicPlus::CreatureData* creatureData = sMythicPlus->GetCreatureData(creature, false);
            if (creatureData == nullptr)
                return;

            creatureData->engageTimer = 0;
        }
    }
};

class mythic_plus_unitscript_start_timer : public UnitScript
{
public:
    mythic_plus_unitscript_start_timer() : UnitScript("mythic_plus_unitscript_start_timer", true,
        {
            UNITHOOK_ON_UNIT_DEATH,
        })
    {
    }

    void OnUnitDeath(Unit* unit, Unit* killer) override
    {
        if (unit && killer && unit->GetMap() == killer->GetMap())
        {
            Map* map = unit->GetMap();
            if (!sMythicPlus->CanMapBeMythicPlus(map))
                return;

            Creature* creature = unit->ToCreature();
            if (!creature || !sMythicPlus->CanProcessCreature(creature))
                return;

            // prevent cases like Drak'Tharon Keep where dungeon mobs kill each other at the start
            if (killer->ToCreature() != nullptr && !killer->IsControlledByPlayer())
                return;
        
            MythicPlus::MapData* mapData = sMythicPlus->GetMapData(map);
            // check if a creature was killed by a player and we can be in a M+ dungeon but it was not yet started,
            // in which case we mark this dungeon as non M+
            if (mapData->mythicPlusStartTimer == 0)
            {
                const MythicPlus::MythicPlusDungeonInfo* dsave = sMythicPlus->GetSavedDungeonInfo(map->GetInstanceId());
                if (dsave != nullptr && !dsave->isMythic)
                    return;

                sMythicPlus->SaveDungeonInfo(map->GetInstanceId(), map->GetId(), 0, 0L, 0, 0, 0, false, false);
                MythicPlus::AnnounceToMap(map, "This dungeon is now saved as non Mythic Plus!");
            }
        }
    }
};

class mythic_plus_damage_affix_unitscript : public UnitScript
{
private:
    void HandleScaleDamage(Unit* attacker, uint32& damage, SpellInfo const* spellInfo, bool dotDmg)
    {
        if (attacker && attacker->ToCreature())
        {
            MythicPlus::CreatureData* creatureData = sMythicPlus->GetCreatureData(attacker->ToCreature(), false);
            if (creatureData != nullptr)
                damage *= creatureData->extraDamageMultiplier;

            // spell damage override is added on top of the global multiplier
            if (spellInfo != nullptr)
            {
                const MythicPlus::SpellOverride* spellOverride = sMythicPlus->GetSpellOverride(attacker->GetMap(), spellInfo->Id);
                if (spellOverride != nullptr)
                {
                    float pct = dotDmg ? spellOverride->dotModPct : spellOverride->modPct;
                    if (pct >= 0.0f) // lets allow 0 aswell, will make the spell effect deal no damage
                        damage *= pct;
                }
            }
        }
    }

    void HandleDamageEffect(Unit* attacker, Unit* victim, uint32& damage, SpellInfo const* spellInfo = nullptr, bool dotDmg = false)
    {
        if (attacker && victim && attacker->GetMap() == victim->GetMap())
        {
            if (!sMythicPlus->IsMapInMythicPlus(attacker->GetMap()))
                return;

            Map* map = attacker->GetMap();
            MythicPlus::MapData* mapData = sMythicPlus->GetMapData(map, false);
            ASSERT(mapData);

            HandleScaleDamage(attacker, damage, spellInfo, dotDmg);

            for (auto* affix : mapData->mythicLevel->affixes)
                affix->HandleOnDamageEffect(attacker, victim, damage);
        }
    }
public:
    mythic_plus_damage_affix_unitscript() : UnitScript("mythic_plus_damage_affix_unitscript", true,
        {
            UNITHOOK_MODIFY_MELEE_DAMAGE,
            UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN,
            UNITHOOK_MODIFY_PERIODIC_DAMAGE_AURAS_TICK
        })
    {
    }

    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        HandleDamageEffect(attacker, target, damage);
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo) override
    {
        // don't do anything for healing
        if (spellInfo->IsPositive() || damage <= 0)
            return;

        uint32 adjustedDamage = (uint32)damage;
        HandleDamageEffect(attacker, target, adjustedDamage, spellInfo, false);
        damage = (int32)adjustedDamage;
    }

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* spellInfo) override
    {
        // don't do anything for healing
        if (spellInfo->IsPositive())
            return;

        // skip entangling roots from affix
        if (spellInfo->Id == EntanglingRootsAffix::ENTANGLING_ROOTS_SPELL_ID)
            return;

        HandleDamageEffect(attacker, target, damage, spellInfo, true);
    }
};

class mythic_plus_periodic_affix_unitscript : public UnitScript
{
public:
    mythic_plus_periodic_affix_unitscript() : UnitScript("mythic_plus_periodic_affix_unitscript", true,
        {
            UNITHOOK_ON_UNIT_UPDATE
        })
    {
    }

    void OnUnitUpdate(Unit* unit, uint32 diff) override
    {
        if (unit && sMythicPlus->IsInMythicPlus(unit))
        {
            Map* map = unit->GetMap();
            MythicPlus::MapData* mapData = sMythicPlus->GetMapData(map, false);
            ASSERT(mapData);

            for (auto* affix : mapData->mythicLevel->affixes)
                affix->HandlePeriodicEffect(unit, diff);
        }
    }
};

void AddSC_mythic_plus_unitscript()
{
    new mythic_plus_unitscript();
    new mythic_plus_unitscript_start_timer();
    new mythic_plus_damage_affix_unitscript();
    new mythic_plus_periodic_affix_unitscript();
}
