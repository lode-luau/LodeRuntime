--!strict
-- @pipe integration test (canonical). Validates named-pipe IPC via the real
-- runtime path (lode_runtime + .luau), not standalone probes.

local pipe = require("@pipe")
local task = require("@task")

local function assertTrue(v: boolean, msg: string)
    if not v then
        error(("assertTrue failed: %s"):format(msg), 2)
    end
end

-- Unique pipe name to avoid collisions between runs.
local pipeName = "lode_test_pipe_" .. tostring(os.time())

print("[pipe] testing server + client IPC")

-- Server (signal-based)
local server = pipe.PipeServer:Create()
assertTrue(not server:IsListening(), "server should not be listening yet")

local received: { string } = {}
local serverConn = server.ConnectionReceived:Connect(function(stream: any)
    print("[pipe] server accepted a client")
    stream.DataReceived:Connect(function(data: any)
        table.insert(received, tostring(data))
    end)
    stream:WriteLine("hello from server")
end)

server:Listen(pipeName)
assertTrue(server:IsListening(), "server should be listening after Listen")

-- Client
local client = pipe.PipeStream:Create()
assertTrue(not client:IsOpen(), "client should not be open yet")

local clientGot: { string } = {}
client.DataReceived:Connect(function(data: any)
    table.insert(clientGot, tostring(data))
end)

client:Connect(pipeName)
assertTrue(client:IsOpen(), "client should be open after Connect")

-- Client writes to server
client:WriteLine("hello from client")

-- Give the event loop time to deliver data.
task.wait(0.2)

print("[pipe] server received:", #received)
print("[pipe] client received:", #clientGot)

assertTrue(#received >= 1, "server should have received data from client")
assertTrue(#clientGot >= 1, "client should have received data from server")

-- Yield-based Accept on a second server
local server2 = pipe.PipeServer:Create()
server2:Listen(pipeName .. "_2")

local client2: any
task.spawn(function()
    client2 = pipe.PipeStream:Create()
    client2:Connect(pipeName .. "_2")
    client2:WriteLine("accept me")
end)

local accepted = server2:Accept()
print("[pipe] accepted via Accept():", tostring(accepted))
assertTrue(accepted ~= nil, "Accept should return a stream")

-- ReadLine on the accepted stream
local line = accepted:ReadLine()
print("[pipe] accepted ReadLine:", tostring(line))
assertTrue(line ~= nil, "accepted stream should read a line")

-- Cleanup: close every stream so the event loop can drain.
client:Close()
server:Close()
accepted:Close()
if client2 ~= nil then
    client2:Close()
end
server2:Close()

print("[pipe] all tests passed")