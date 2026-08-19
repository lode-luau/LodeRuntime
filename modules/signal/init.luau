--!nonstrict
--!native
--!optimize 2

local task = require("../task")

-- Public interface types
export type Connection = {
    --[[
        Disconnects this connection, preventing the callback from being fired.
        @param self The connection instance
    ]]
    Disconnect: (self: Connection) -> (),
}

export type Signal<T...> = {
    Connect: (self: Signal<T...>, callback: (T...) -> ()) -> Connection,
    Once: (self: Signal<T...>, callback: (T...) -> ()) -> Connection,
    Wait: (self: Signal<T...>, timeout: number?) -> T...,
}

-- Forward declarations for circular type references
type ConnectionInternal<T...> = {
    --[[
        Whether this connection is still active.
        @param self The connection instance
    ]]
    Connected: boolean,
    --[[
        Disconnects this connection, preventing the callback from being fired.
        @param self The connection instance
    ]]
    Disconnect: (self: ConnectionInternal<T...>) -> (),
    _signal: SignalInternal<T...>,
    _next: ConnectionInternal<T...>?,
    _callback: (T...) -> (),
}

export type SignalInternal<T...> = {
    _head: ConnectionInternal<T...>?,
    Connect: (self: SignalInternal<T...>, callback: (T...) -> ()) -> ConnectionInternal<T...>,
    Once: (self: SignalInternal<T...>, callback: (T...) -> ()) -> ConnectionInternal<T...>,
    Wait: (self: SignalInternal<T...>, timeout: number?) -> T...,
    Fire: (self: SignalInternal<T...>, T...) -> (),
    DisconnectAll: (self: SignalInternal<T...>) -> (),
    Destroy: (self: SignalInternal<T...>) -> (),
    Public: (self: SignalInternal<T...>) -> Signal<T...>,
}

-- Connection metatable
local ConnectionMT = {}
ConnectionMT.__index = ConnectionMT

-- Signal metatable
local SignalMT = {}
SignalMT.__index = SignalMT

--[[
    Creates a new connection object.
    @param signal The signal this connection belongs to
    @param callback The function to call when the signal fires
    @return A new ConnectionInternal instance
]]
local function createConnection<T...>(
    signal: SignalInternal<T...>,
    callback: (T...) -> ()
): ConnectionInternal<T...>
    local connection = {
        Connected = true,
        _signal = signal,
        _callback = callback,
        _next = nil :: ConnectionInternal<T...>?,
    }
    setmetatable(connection, ConnectionMT)
    return (connection :: any) :: ConnectionInternal<T...>
end

--[[
    Connects a callback to the signal.
    @param self The signal instance
    @param callback The function to call when the signal fires
    @return A connection object that can be disconnected
]]
function SignalMT:Connect<T...>(callback: (T...) -> ()): ConnectionInternal<T...>
    local signal = self :: SignalInternal<T...>
    local connection = createConnection(signal, callback)
    connection._next = signal._head
    signal._head = connection
    return connection
end

--[[
    Creates a connection that will only fire once.
    @param self The signal instance
    @param callback The function to call when the signal fires
    @return A connection object that can be disconnected
]]
function SignalMT:Once<T...>(callback: (T...) -> ()): ConnectionInternal<T...>
    local signal = self :: SignalInternal<T...>
    local connection: ConnectionInternal<T...>
    connection = signal:Connect(function(...)
        (connection :: any):Disconnect()
        callback(...)
    end)
    return connection
end

--[[
    Yields the current thread until the signal fires.
    When timeout is provided, returns fire args if fired within time, or nil on timeout.
    @param self The signal instance
    @param timeout Optional max seconds to wait (nil = wait forever)
    @return The arguments passed to Fire, or nil on timeout
]]
function SignalMT:Wait<T...>(timeout: number?): T...
    local thread = coroutine.running()
    local connection: ConnectionInternal<T...>?

    if timeout then
        task.delay(timeout, function()
            if connection then
                (connection :: any):Disconnect()
                task.defer(thread)
            end
        end)
    end

    -- Connect synchronously; connection is guaranteed assigned before any callback fires
    connection = self:Connect(function(...)
        -- connection is guaranteed assigned by synchronous Connect() call above
        (connection :: any):Disconnect()
        task.defer(thread, ...)
    end)

    return coroutine.yield()
end

--[[
    Fires the signal, calling all connected callbacks.
    @param self The signal instance
    @param ... The arguments to pass to callbacks
]]
function SignalMT:Fire<T...>(...: T...)
    local current = self._head
    while current do
        if current.Connected then
            task.spawn(current._callback, ...)
        end
        current = current._next
    end
end

--[[
    Disconnects all connections from this signal.
    @param self The signal instance
]]
function SignalMT:DisconnectAll()
    local current = self._head
    while current do
        current.Connected = false
        current = current._next
    end
    self._head = (nil :: any)
end

--[[
    Destroys the signal and disconnects all connections.
    @param self The signal instance
]]
function SignalMT:Destroy()
    self:DisconnectAll()
end

--[[
    Disconnects this connection from its signal.
    @param self The connection instance
]]
function ConnectionMT:Disconnect()
    if not self.Connected then
        return
    end
    self.Connected = false

    local signal = self._signal
    if signal._head == self then
        signal._head = (self._next::any)
    else
        local current = signal._head
        while current and current._next and current._next ~= self do
            current = current._next
        end
        if current then
            current._next = self._next
        end
    end
end

--[[
    Creates a public interface for the signal.
    @param self The signal instance
    @return A frozen table with Connect, Once, and Wait methods
]]
function SignalMT:Public<T...>(): Signal<T...>
    local internal = self :: SignalInternal<T...>
    local proxy = {} :: Signal<T...>

    --[[ Connects a callback to the signal. ]]
    function proxy:Connect(callback: (T...) -> ()): Connection
        return internal:Connect(callback) :: any
    end

    --[[ Creates a connection that will only fire once. ]]
    function proxy:Once(callback: (T...) -> ()): Connection
        return internal:Once(callback) :: any
    end

    --[[ Waits for the signal to fire and returns the arguments. ]]
    function proxy:Wait(timeout: number?): T...
        return internal:Wait(timeout)
    end

    table.freeze(proxy)
    return proxy :: any
end

--[[
    Signal Module - A type-safe implementation of the observer pattern.
    Provides a robust event system for decoupled communication between components.
    Supports Connect (continuous), Once (auto-disconnect after first fire), and
    Wait (yield until fire, with optional timeout).

    Usage:
    ```lua
    local Signal = require("@signal")
    local task = require("@task")

    -- Subscribe to a signal
    local sig = Signal.new()
    sig:Connect(function(msg)
        print("received:", msg)
    end)

    -- Fire the signal
    sig:Fire("hello")

    -- Wait for the signal with a timeout
    local ok = {sig:Wait(5)}
    if #ok > 0 then
        print("fired within 5s")
    else
        print("timed out")
    end
    ```
]]
local Signal = {}

--[[
    Creates a new signal instance.
    @return A new SignalInternal instance
]]
function Signal.new<T...>(): SignalInternal<T...>
    local signal = {
        _head = nil :: ConnectionInternal<T...>?,
    }
    setmetatable(signal, SignalMT)
    return (signal :: any) :: SignalInternal<T...>
end

return table.freeze(Signal)