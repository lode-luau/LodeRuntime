#pragma once

#include "Lode/Export.hpp"
#include "Lode/Result.hpp"
#include "Lode/Error.hpp"
#include "Lode/Value.hpp"
#include <vector>
#include <memory>

struct lua_State;

namespace Lode
{

enum class CoroutineStatus
{
    Running,
    Suspended,
    Normal,
    Dead,
    Error
};

class LODE_API Coroutine
{
public:
    Coroutine();
    Coroutine(lua_State* L, int fnRef);
    ~Coroutine();

    Coroutine(const Coroutine& other);
    Coroutine(Coroutine&& other) noexcept;
    Coroutine& operator=(const Coroutine& other);
    Coroutine& operator=(Coroutine&& other) noexcept;

    Result<std::vector<Value>> Resume(const std::vector<Value>& args = {});
    [[nodiscard]] CoroutineStatus GetStatus() const;

    [[nodiscard]] lua_State* GetThreadState() const;

private:
    struct RefData;
    std::shared_ptr<RefData> refData_;
};

} // namespace Lode
