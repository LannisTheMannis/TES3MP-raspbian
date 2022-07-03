#include "ScriptFunctions.hpp"
#include "API/PublicFnAPI.hpp"
#include <cstdarg>
#include <iostream>
#include <apps/openmw-mp/Player.hpp>
#include <apps/openmw-mp/Networking.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>

template<typename... Types>
constexpr char TypeString<Types...>::value[];
ScriptFunctionData ScriptFunctions::functions[]{
            {"CreateTimer",         ScriptFunctions::CreateTimer},
            {"CreateTimerEx",       ScriptFunctions::CreateTimerEx},
            {"MakePublic",          ScriptFunctions::MakePublic},
            {"CallPublic",          ScriptFunctions::CallPublic},

            {"StartTimer",          ScriptFunctions::StartTimer},
            {"StopTimer",           ScriptFunctions::StopTimer},
            {"RestartTimer",        ScriptFunctions::RestartTimer},
            {"FreeTimer",           ScriptFunctions::FreeTimer},
            {"IsTimerElapsed",      ScriptFunctions::IsTimerElapsed},

            ACTORAPI,
            BOOKAPI,
            CELLAPI,
            CHARCLASSAPI,
            CHATAPI,
            DIALOGUEAPI,
            FACTIONAPI,
            GUIAPI,
            ITEMAPI,
            MECHANICSAPI,
            MISCELLANEOUSAPI,
            POSITIONAPI,
            QUESTAPI,
            RECORDSDYNAMICAPI,
            SHAPESHIFTAPI,
            SERVERAPI,
            SETTINGSAPI,
            SPELLAPI,
            STATAPI,
            OBJECTAPI,
            WORLDSTATEAPI
    };
constexpr ScriptCallbackData ScriptFunctions::callbacks[];

void ScriptFunctions::MakePublic(ScriptFunc _public, const char *name, char ret_type, const char *def) noexcept
{
    Public::MakePublic(_public, name, ret_type, def);
}

boost::any ScriptFunctions::CallPublic(const char *name, va_list args) noexcept
{
    std::vector<boost::any> params;

    try
    {
        std::string def = Public::GetDefinition(name);
        Utils::getArguments(params, args, def);

        return Public::Call(name, params);
    }
    catch (...) {}

    return 0;
}
