/*
 * Credits: silviu20092
 */

#include "ScriptMgr.h"
#include "mythic_plus.h"

class mythic_plus_keystone : public ItemScript
{
public:
    mythic_plus_keystone() : ItemScript("mythic_plus_keystone") {}

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        Map* map = player->GetMap();
        if (!sMythicPlus->CanMapBeMythicPlus(map))
        {
            MythicPlus::BroadcastToPlayer(player, "Need to be in a Mythic Plus capable dungeon in order to use the keystone.");
            return true;
        }

        // only group's leader can use the keystone
        if (!MythicPlus::Utils::IsGroupLeader(player))
        {
            MythicPlus::BroadcastToPlayer(player, "Only the group's leader can use the keystone.");
            return true;
        }

        // check if the group's leader actually has a set m+ level
        if (sMythicPlus->GetCurrentMythicPlusLevel(player) == 0)
        {
            MythicPlus::BroadcastToPlayer(player, "You don't have a Mythic Plus level chosen.");
            return true;
        }

        // ── Retail: Dungeon-Bound Keystone Validation ──────────────────────
        // If the player has a bound keystone, verify it matches the current map.
        MythicPlus::PlayerKeystone ks;
        bool hasKeystone = sMythicPlus->GetPlayerKeystone(player, ks);

        if (hasKeystone && ks.mapId != 0 && ks.mapId != map->GetId())
        {
            // Player's keystone is for a different dungeon
            MapEntry const* requiredMap = sMapStore.LookupEntry(ks.mapId);
            std::string requiredName = requiredMap ? requiredMap->name[0] : "Unknown Dungeon";
            MythicPlus::BroadcastToPlayer(player,
                "Your keystone is for |cffFFD700" + requiredName + " +" +
                Acore::ToString(ks.level) + "|r. You need to enter that dungeon to use it.");
            return true;
        }

        // If no bound keystone yet, auto-assign one to this dungeon at current level
        if (!hasKeystone || ks.mapId == 0)
        {
            uint32 lvl = sMythicPlus->GetCurrentMythicPlusLevel(player);
            sMythicPlus->SetPlayerKeystone(player, map->GetId(), lvl > 0 ? lvl : 2);
        }

        MythicPlus::MapData* mapData = sMythicPlus->GetMapData(map);
        if (mapData->mythicPlusStartTimer > 0)
        {
            MythicPlus::BroadcastToPlayer(player, "Mythic Plus dungeon already in progress or completed.");
            return true;
        }
        if (mapData->keystoneTimer > 0)
        {
            MythicPlus::BroadcastToPlayer(player, "Keystone was already used, waiting for Mythic Plus to start...");
            return true;
        }

        // lets check if a dungeon save was performed, in which case it must be saved as M+
        const MythicPlus::MythicPlusDungeonInfo* dsave = sMythicPlus->GetSavedDungeonInfo(map->GetInstanceId());
        if (dsave != nullptr && !dsave->isMythic)
        {
            MythicPlus::BroadcastToPlayer(player, "This dungeon was marked as non Mythic Plus, keystone can't be used anymore.");
            return true;
        }

        if (player->IsInCombat())
        {
            MythicPlus::BroadcastToPlayer(player, "Can't use the Keystone while in combat.");
            return true;
        }

        // every player in the group must be online and at max level
        if (!sMythicPlus->CheckGroupLevelForKeystone(player))
        {
            MythicPlus::BroadcastToPlayer(player, "All players in the group must be online and at max level.");
            return true;
        }

        // Use the bound keystone level (not the legacy charMythicLevels)
        MythicPlus::PlayerKeystone boundKs;
        sMythicPlus->GetPlayerKeystone(player, boundKs);

        mapData->keystoneTimer = MythicPlus::Utils::GameTimeCount();
        mapData->keystoneLevel = boundKs.level > 0 ? boundKs.level : sMythicPlus->GetCurrentMythicPlusLevel(player);

        std::ostringstream oss;
        oss << "Mythic Plus will start in " << secsToTimeString(MythicPlus::KEYSTONE_START_TIMER / 1000);
        oss << ". Mythic level: |cffFFD700+" << Acore::ToString(mapData->keystoneLevel) << "|r";
        MythicPlus::AnnounceToGroup(player, oss.str());

        // Sync keystone details to the client UI
        sMythicPlus->SyncKeystoneToClient(player);

        sMythicPlus->RemoveKeystone(player);
        return true;
    }

};

void AddSC_mythic_plus_keystone()
{
    new mythic_plus_keystone();
}
