--!strict
--!native

-- Socket module integration test (runs through the real runtime).
-- Spins up an echo TCP server on 127.0.0.1 and exercises the Roblox-style
-- OOP API: Server:Create/Listen, TcpSocket:Create/Connect/Send, events via
-- Lode::Signal proxies (Connect + Wait), address getters, resolve, and errors.

local socket = require("@socket")
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

print("=== Socket Module Integration Test ===")

-- resolve (yield variant)
local addr = socket.resolve("127.0.0.1")
assertEq(addr, "127.0.0.1", "resolve 127.0.0.1")

-- resolveAsync (callback variant)
local resolvedAsync = false
socket.resolveAsync("127.0.0.1", function(address, error)
    assertTrue(address == "127.0.0.1", "resolveAsync address")
    assertEq(error, nil, "resolveAsync error")
    resolvedAsync = true
end)
for _ = 1, 200 do
    if resolvedAsync then
        break
    end
    task.wait(0.01)
end
assertTrue(resolvedAsync, "resolveAsync completed")

-- Server
local server = socket.Server:Create()
server:Listen(0) -- ephemeral port
assertTrue(server:IsListening(), "server is listening")
local serverAddr = server:LocalAddress()
print("server bound to", serverAddr.host, serverAddr.port)
assertTrue(serverAddr.port > 0, "server ephemeral port assigned")

local serverEchoed = false
local serverDisconnected = false

server.ClientConnected:Connect(function(client)
    print("server accepted", client:RemoteAddress().host, client:RemoteAddress().port)
    client.MessageReceived:Connect(function(data)
        serverEchoed = true
        client:Send("echo: " .. data)
    end)
    client.Disconnected:Connect(function()
        serverDisconnected = true
    end)
end)

-- Client
local sock = socket.TcpSocket:Create()
sock:Connect("127.0.0.1", serverAddr.port)
assertTrue(sock:IsConnected(), "client connected")
local clientAddr = sock:LocalAddress()
local peerAddr = sock:RemoteAddress()
assertEq(peerAddr.host, "127.0.0.1", "client remote host")
assertEq(peerAddr.port, serverAddr.port, "client remote port")
print("client connected", clientAddr.host, clientAddr.port, "->", peerAddr.host, peerAddr.port)

local clientDisconnected = false
sock.Disconnected:Connect(function()
    clientDisconnected = true
end)

-- Give the server a moment to register its echo handler before we send.
task.wait(0.05)

sock:Send("hello")
local reply = { sock.MessageReceived:Wait(5) }
assertTrue(#reply > 0, "client received echo")
assertEq(reply[1], "echo: hello", "echo content")
assertTrue(serverEchoed, "server echoed the message")

-- Client Close fires Disconnected on both ends.
sock:Close()
assertTrue(not sock:IsConnected(), "client closed")
sock.Disconnected:Wait(5)
assertTrue(clientDisconnected, "client Disconnected fired")
for _ = 1, 200 do
    if serverDisconnected then
        break
    end
    task.wait(0.01)
end
assertTrue(serverDisconnected, "server saw client disconnect")

-- Server Close.
server:Close()
assertTrue(not server:IsListening(), "server closed")

-- Error cases.
local invalid = socket.TcpSocket:Create()
local okPort = pcall(function()
    invalid:Connect("127.0.0.1", 99999)
end)
assertTrue(not okPort, "invalid port raises")

local refused = socket.TcpSocket:Create()
local okRefused, refusedError = pcall(function()
    refused:Connect("127.0.0.1", 1)
end)
print("refused connect ok =", okRefused, "error =", tostring(refusedError))
assertTrue(not okRefused, "connection refused raises")

local okEmpty = pcall(function()
    socket.TcpSocket:Create():Connect("", 8080)
end)
assertTrue(not okEmpty, "empty host raises")

print("=== ALL SOCKET TESTS PASSED ===")
