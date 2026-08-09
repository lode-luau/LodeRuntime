--!strict
--!native

-- WebSocket module integration test (runs through the real runtime).
-- Spins up a ws:// echo server on 127.0.0.1 and exercises the Roblox-style
-- OOP API: WebSocketServer:Create/Listen, WebSocket:Create/Connect/Send/Ping,
-- events via Lode::Signal proxies (Connect + Wait), ReadyState, close codes,
-- and error paths.

local websocket = require("@websocket")
local task = require("@task")

local function assertEq(actual: any, expected: any, label: string)
    if actual ~= expected then
        error((label or "assertEq") .. ": expected " .. tostring(expected) .. ", got " .. tostring(actual), 2)
    end
end

local function assertTrue(cond: boolean, label: string)
    if not cond then
        print("TRACEBACK FROM LUA:")
        print(debug.traceback())
        error(label or "assertTrue", 2)
    end
end

print("=== WebSocket Module Integration Test ===")

-- Server
local server = websocket.WebSocketServer:Create()
server:Listen(0) -- ephemeral port
assertTrue(server:IsListening(), "server is listening")
local serverAddr = server:LocalAddress()
print("server bound to", serverAddr.host, serverAddr.port)
assertTrue(serverAddr.port > 0, "server ephemeral port assigned")

local serverAccepted = false
local serverEchoed = false
local serverPinged = false
local serverDisconnected = false
local serverCloseCode = -1

server.ClientConnected:Connect(function(client)
    serverAccepted = true
    assertEq(client:ReadyState(), "open", "server client readyState open")
    print("server accepted client")
    client.MessageReceived:Connect(function(data)
        if data == "ping-me" then
            serverPinged = true
            client:Ping("server-ping")
        else
            serverEchoed = true
            client:Send(data)
        end
    end)
    client.Disconnected:Connect(function(code, reason)
        serverCloseCode = code or -1
        serverDisconnected = true
        print("server saw disconnect, code =", tostring(code), "reason =", tostring(reason))
    end)
end)

-- Client
local sock = websocket.WebSocket:Create()
sock:Connect("ws://127.0.0.1:" .. tostring(serverAddr.port) .. "/chat?room=1")
assertTrue(sock:IsConnected(), "client connected")
assertEq(sock:ReadyState(), "open", "client readyState open")
print("client readyState =", sock:ReadyState())

local clientDisconnected = false
local clientCloseCode = -1
sock.Disconnected:Connect(function(code, reason)
    clientCloseCode = code or -1
    clientDisconnected = true
    print("client disconnected, code =", tostring(code), "reason =", tostring(reason))
end)

-- Give the server a moment to register its echo handler.
task.wait(0.05)

-- Text echo
sock:Send("hello websocket")
local reply = { sock.MessageReceived:Wait(5) }
assertTrue(#reply > 0, "client received text echo")
assertEq(reply[1], "hello websocket", "text echo content")
assertTrue(serverAccepted, "server accepted the client")
assertTrue(serverEchoed, "server echoed the message")

-- Binary echo (buffer -> binary opcode, delivered as string)
local buf = buffer.create(5)
buffer.writeu8(buf, 0, 1)
buffer.writeu8(buf, 1, 2)
buffer.writeu8(buf, 2, 3)
buffer.writeu8(buf, 3, 4)
buffer.writeu8(buf, 4, 5)
sock:Send(buf)
local breply = { sock.MessageReceived:Wait(5) }
assertTrue(#breply > 0, "client received binary echo")
assertTrue(typeof(breply[1]) == "string", "binary delivered as string")
assertEq(#breply[1], 5, "binary payload length")
assertEq(string.byte(breply[1], 1), 1, "binary byte 0")
assertEq(string.byte(breply[1], 5), 5, "binary byte 4")

-- Client ping: auto pong is handled internally, connection stays open.
sock:Ping("client-ping")
assertTrue(sock:IsConnected(), "still connected after ping")

-- Server ping (triggered by the "ping-me" message).
sock:Send("ping-me")
task.wait(0.1)
assertTrue(serverPinged, "server sent a ping")

-- Close handshake with code + reason.
sock:Close(1000, "done")
sock.Disconnected:Wait(5)
assertTrue(clientDisconnected, "client Disconnected fired")
assertEq(clientCloseCode, 1000, "client close code")
for _ = 1, 200 do
    if serverDisconnected then
        break
    end
    task.wait(0.01)
end
assertTrue(serverDisconnected, "server saw disconnect")
assertEq(serverCloseCode, 1000, "server close code")
assertTrue(not sock:IsConnected(), "client no longer connected")
assertEq(sock:ReadyState(), "closed", "client readyState closed")

-- Server Close.
server:Close()
assertTrue(not server:IsListening(), "server closed")

-- Error cases.
local okWss = pcall(function()
    websocket.WebSocket:Create():Connect("wss://example.com/")
end)
assertTrue(not okWss, "wss:// raises")

local okBadUrl = pcall(function()
    websocket.WebSocket:Create():Connect("not a url")
end)
assertTrue(not okBadUrl, "invalid url raises")

local okRefused = pcall(function()
    websocket.WebSocket:Create():Connect("ws://127.0.0.1:1/")
end)
assertTrue(not okRefused, "connection refused raises")

local okEmptyHost = pcall(function()
    websocket.WebSocket:Create():Connect("ws:///nohost")
end)
assertTrue(not okEmptyHost, "empty host raises")

print("=== ALL WEBSOCKET TESTS PASSED ===")
