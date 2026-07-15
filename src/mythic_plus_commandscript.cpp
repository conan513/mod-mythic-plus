/*
 * Credits: silviu20092
 */

#include "Chat.h"
#include "CommandScript.h"
#include "mythic_plus.h"

using namespace Acore::ChatCommands;

class mythic_plus_commandscript : public CommandScript
{
public:
    mythic_plus_commandscript() : CommandScript("mythic_plus_commandscript") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable mythicCommandTable =
        {
            { "info",           HandleMythicInfoCommand,                SEC_PLAYER,             Console::No  },
            { "vault",          HandleMythicVaultCommand,               SEC_PLAYER,             Console::No  },
            { "reload",         HandleMythicReloadCommand,              SEC_ADMINISTRATOR,      Console::Yes }
        };
        static ChatCommandTable commandTable =
        {
            { "mythic", mythicCommandTable }
        };
        return commandTable;
    }
private:
    static bool HandleMythicInfoCommand(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (sMythicPlus->IsInMythicPlus(player))
        {
            const MythicPlus::MapData* mapData = sMythicPlus->GetMapData(player->GetMap(), false);
            ASSERT(mapData);

            const MythicLevel* level = mapData->mythicLevel;
            ASSERT(level);

            sMythicPlus->PrintMythicLevelInfo(level, player);
        }
        else
            handler->SendSysMessage("You are not in a Mythic Plus dungeon right now.");

        return true;
    }

    static bool HandleMythicVaultCommand(ChatHandler* handler, Optional<std::string> subCommand)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        if (!sMythicPlus->GetGreatVaultEnabled())
        {
            handler->SendSysMessage("Great Vault is currently disabled.");
            return true;
        }

        std::string sub = subCommand ? *subCommand : "";
        if (sub == "claim")
        {
            sMythicPlus->ClaimGreatVault(player);
            return true;
        }

        const MythicPlus::GreatVaultEntry* entry = sMythicPlus->GetGreatVault(player);
        uint16 currentWeek = static_cast<uint16>(MythicPlus::GetCurrentISOWeek());
        uint16 currentYear = static_cast<uint16>(MythicPlus::GetCurrentISOYear());

        handler->SendSysMessage("--- Great Vault Status ---");
        if (!entry)
        {
            handler->SendSysMessage("No dungeons completed this week yet.");
        }
        else if (entry->weekNumber == currentWeek && entry->year == currentYear)
        {
            handler->SendSysMessage("Your current best run this week: Level " + Acore::ToString(entry->bestLevel));
            handler->SendSysMessage("These rewards will be claimable next week after the Monday reset.");
        }
        else
        {
            handler->SendSysMessage("Unclaimed rewards from week " + Acore::ToString(entry->weekNumber) + ": Level " + Acore::ToString(entry->bestLevel) + " run.");
            if (entry->claimed)
                handler->SendSysMessage("Status: Claimed.");
            else
                handler->SendSysMessage("Status: Unclaimed. Type '/mythic vault claim' to claim your rewards!");
        }

        return true;
    }

    static bool HandleMythicReloadCommand(ChatHandler* handler)
    {
        // only load stuff that is hot reloadable
        sMythicPlus->LoadIgnoredEntriesForMultiplyAffixFromDB();
        sMythicPlus->LoadScaleMapFromDB();
        sMythicPlus->LoadSpellOverridesFromDB();
        handler->SendGlobalGMSysMessage("Hot reloadable tables were processed: mythic_plus_ignore_multiply_affix, mythic_plus_map_scale, mythic_plus_spell_override");

        return true;
    }
};

void AddSC_mythic_plus_commandscript()
{
    new mythic_plus_commandscript();
}
