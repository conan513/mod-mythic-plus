/*
 * Credits: silviu20092
 */

#include "ScriptMgr.h"
#include "GameTime.h"
#include "mythic_plus.h"
#include "mythic_affix.h"

class mythic_plus_playerscript : public PlayerScript
{
public:
    mythic_plus_playerscript() : PlayerScript("mythic_plus_playerscript",
        {
            PLAYERHOOK_ON_LOGIN,
            PLAYERHOOK_ON_PLAYER_JUST_DIED,
            PLAYERHOOK_ON_BEFORE_SEND_CHAT_MESSAGE
        }
    )
    {
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!sMythicPlus->IsEnabled())
            return;

        // check if the saved M+ level is still present (maybe it was removed from DB in the meantime)
        uint32 playerMplusLevel = sMythicPlus->GetCurrentMythicPlusLevel(player);
        if (playerMplusLevel > 0 && !sMythicPlus->GetMythicLevel(playerMplusLevel))
            sMythicPlus->SetCurrentMythicPlusLevel(player, 0, true); // force reset the level, this is an edge case anyway

        // Great Vault notification
        if (sMythicPlus->GetGreatVaultEnabled())
        {
            const MythicPlus::GreatVaultEntry* entry = sMythicPlus->GetGreatVault(player);
            if (entry && !entry->claimed)
            {
                uint16 currentWeek = static_cast<uint16>(MythicPlus::GetCurrentISOWeek());
                uint16 currentYear = static_cast<uint16>(MythicPlus::GetCurrentISOYear());
                
                bool isPastWeek = false;
                if (entry->year < currentYear)
                    isPastWeek = true;
                else if (entry->year == currentYear && entry->weekNumber < currentWeek)
                    isPastWeek = true;

                if (isPastWeek && entry->bestLevel > 0)
                {
                    MythicPlus::BroadcastToPlayer(player, MythicPlus::Utils::GreenColored("You have unclaimed Great Vault rewards from a previous week! Use '/mythic vault claim' or visit the Mythic NPC to claim them."));
                }
            }
        }

        Group* group = player->GetGroup();
        if (group != nullptr)
        {
            ObjectGuid leaderGuid = group->GetLeaderGUID();
            uint32 mplusLevel = sMythicPlus->GetCurrentMythicPlusLevelForGUID(leaderGuid.GetCounter());
            if (mplusLevel > 0)
                MythicPlus::BroadcastToPlayer(player, "Your group's leader (can even be you) has a Mythic Plus level set. The leader can use a Mythic Keystone to start a level " + Acore::ToString(mplusLevel) + " Mythic Plus dungeon!");
        }

        // ── Retail: Send keystone info to client UI on login ─────────────
        // Slight delay via a short-circuit: just sync immediately; the
        // addon script frame will handle it on PLAYER_ENTERING_WORLD.
        sMythicPlus->SyncKeystoneToClient(player);
    }

    void OnPlayerJustDied(Player* player) override
    {
        if (player && sMythicPlus->IsInMythicPlus(player))
        {
            MythicPlus::MapData* mapData = sMythicPlus->GetMapData(player->GetMap(), false);
            ASSERT(mapData);

            if (mapData->penaltyOnDeath > 0 && !mapData->done)
            {
                std::ostringstream oss;
                oss << player->GetName() << " just died, a penalty of ";
                oss << secsToTimeString(mapData->penaltyOnDeath);
                oss << " was applied.";

                Map* map = player->GetMap();
                sMythicPlus->BroadcastToMap(player->GetMap(), MythicPlus::Utils::RedColored(oss.str()));

                mapData->deaths++;
                
                sMythicPlus->SaveDungeonInfo(map->GetInstanceId(), map->GetId(), mapData->timeLimit, mapData->mythicPlusStartTimer, mapData->mythicLevel->level, mapData->penaltyOnDeath, mapData->deaths, mapData->done);

                // --- Broadcast M+ TIME update to all players on the map ---
                long long diff = GameTime::GetGameTime().count() - mapData->mythicPlusStartTimer;
                Map::PlayerList const& players = map->GetPlayers();
                for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
                {
                    if (Player* pl = itr->GetSource())
                    {
                        MythicPlus::Utils::SendAddonMessage(pl, "MODERNWOW", "M+:TIME:" + Acore::ToString(diff) + ":" + Acore::ToString(mapData->deaths));
                    }
                }
            }
        }
    }

    void OnPlayerBeforeSendChatMessage(Player* player, uint32& /*type*/, uint32& lang, std::string& msg) override
    {
        if (lang == LANG_ADDON && player)
        {
            size_t tabPos = msg.find('\t');
            if (tabPos != std::string::npos)
            {
                std::string prefix = msg.substr(0, tabPos);
                std::string text = msg.substr(tabPos + 1);

                if (prefix == "MODERNWOW")
                {
                    size_t colonPos = text.find(':');
                    if (colonPos != std::string::npos)
                    {
                        std::string cmd = text.substr(0, colonPos);
                        std::string args = text.substr(colonPos + 1);

                        if (cmd == "M+")
                        {
                            HandleClientAddonMessage(player, args);
                        }
                    }
                }
            }
        }
    }
private:
    void HandleClientAddonMessage(Player* player, std::string const& message)
    {
        size_t colonPos = message.find(':');
        std::string sub = (colonPos != std::string::npos) ? message.substr(0, colonPos) : message;
        std::string args = (colonPos != std::string::npos) ? message.substr(colonPos + 1) : "";

        if (sub == "REQ_VAULT")
        {
            const MythicPlus::GreatVaultEntry* entry = sMythicPlus->GetGreatVault(player);
            uint16 currentWeek = MythicPlus::GetCurrentISOWeek();
            uint16 currentYear = MythicPlus::GetCurrentISOYear();
            
            uint8 bestLevel = 0;
            bool claimed = false;
            uint16 vaultWeek = 0;
            uint16 vaultYear = 0;
            if (entry)
            {
                bestLevel = entry->bestLevel;
                claimed = entry->claimed;
                vaultWeek = entry->weekNumber;
                vaultYear = entry->year;
            }
            
            std::string affixesStr = "";
            const MythicLevel* mLevel = sMythicPlus->GetMythicLevel(5); // use level 5 to get affixes
            if (mLevel)
            {
                std::vector<std::string> affNames;
                for (auto* a : mLevel->affixes)
                    affNames.push_back(a->ToString());
                for (size_t i = 0; i < affNames.size(); ++i)
                {
                    if (i > 0) affixesStr += ", ";
                    affixesStr += affNames[i];
                }
            }
            
            std::string reply = "M+:VAULT_INFO:" + Acore::ToString(bestLevel) + ":" + Acore::ToString(claimed ? 1 : 0) + ":" + 
                                Acore::ToString(vaultWeek) + ":" + Acore::ToString(vaultYear) + ":" + 
                                Acore::ToString(currentWeek) + ":" + Acore::ToString(currentYear) + ":" + affixesStr;
            
            MythicPlus::Utils::SendAddonMessage(player, "MODERNWOW", reply);
        }
        else if (sub == "CLAIM_VAULT")
        {
            bool success = sMythicPlus->ClaimGreatVault(player);
            if (success)
            {
                MythicPlus::Utils::SendAddonMessage(player, "MODERNWOW", "M+:CLAIM_OK");
            }
            else
            {
                MythicPlus::Utils::SendAddonMessage(player, "MODERNWOW", "M+:CLAIM_FAIL");
            }
        }
    }
};

void AddSC_mythic_plus_playerscript()
{
    new mythic_plus_playerscript();
}
