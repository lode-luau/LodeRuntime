#pragma once

#include <vector>

struct lua_State;

namespace Lode
{
struct Value;

namespace Detail
{
// Capture channel used while loading a required module whose top-level
// yields into async operations: the generic resume helper hands the module's
// return values to this sink instead of discarding them.
extern thread_local lua_State* g_moduleLoadCo;
extern thread_local std::vector<Value>* g_moduleLoadSink;
}
}