#include "Lode/Module.hpp"

LODE_EXPORT int LodeModuleInit(lua_State* L)
{
    (void)L;
    return 0;
}

LODE_EXPORT const char* LodeModuleConfig()
{
    return LodeBuildConfigName();
}

LODE_EXPORT const char* LodeModuleABI()
{
    return "lode-abi-mismatch-fixture";
}
