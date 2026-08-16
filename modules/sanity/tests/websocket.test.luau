--!strict
--!native

-- Regression tests for the native websocket module: deterministic error
-- paths plus a full positive-path round trip (connect, send/receive,
-- close) against a local loopback server.

local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local task = require("../../../modules/task")
local websocket = require("../../websocket")

describe("websocket", function()
    it("exposes the WebSocket and WebSocketServer classes", function()
        expect(typeof(websocket.WebSocket)).toBe("table")
        expect(typeof(websocket.WebSocketServer)).toBe("table")
        expect(typeof(websocket.WebSocket.Create)).toBe("function")
        expect(typeof(websocket.WebSocketServer.Create)).toBe("function")
    end)

    it("rejects wss:// until TLS support lands", function()
        local ok, err = pcall(function()
            websocket.WebSocket:Create():Connect("wss://example.com/")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("tls", 1, true) ~= nil).toBe(true)
    end)

    it("rejects URLs without a scheme", function()
        local ok, err = pcall(function()
            websocket.WebSocket:Create():Connect("not a url")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("invalid-url", 1, true) ~= nil).toBe(true)
    end)

    it("rejects URLs with an empty host", function()
        local ok, err = pcall(function()
            websocket.WebSocket:Create():Connect("ws:///path")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("invalid-url", 1, true) ~= nil).toBe(true)
    end)

    it("rejects URLs with an out-of-range port", function()
        local ok, err = pcall(function()
            websocket.WebSocket:Create():Connect("ws://127.0.0.1:99999/")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("invalid-url", 1, true) ~= nil).toBe(true)
    end)

    it("fails with connection-refused on an unreachable port", function()
        local ok, err = pcall(function()
            websocket.WebSocket:Create():Connect("ws://127.0.0.1:1/")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("connection refused", 1, true) ~= nil).toBe(true)
    end)

    it("rejects a non-string url", function()
        local ok, err = pcall(function()
            websocket.WebSocket:Create():Connect(123)
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("url must be a string", 1, true) ~= nil).toBe(true)
    end)

    it("rejects invalid Connect options", function()
        local ok, err = pcall(function()
            websocket.WebSocket:Create():Connect("ws://127.0.0.1:1/", "not a table")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("opts must be a table or nil", 1, true) ~= nil).toBe(true)
    end)

    it("rejects a server on an invalid host", function()
        local ok, err = pcall(function()
            websocket.WebSocketServer:Create():Listen(0, "not an ip address")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("host must be an IPv4 or IPv6 address", 1, true) ~= nil).toBe(true)
    end)

    it("can connect, send and receive messages, and close", function()
        local server = websocket.WebSocketServer:Create()
        local acceptedClient = nil
        local receivedOnServer = nil
        local receivedOnClient = nil
        local clientDisconnectedFired = false
        local serverClientDisconnectedFired = false
        
        server.ClientConnected:Connect(function(client)
            acceptedClient = client
            client.MessageReceived:Connect(function(msg)
                receivedOnServer = msg
                client:Send("hello from server")
            end)
            client.Disconnected:Connect(function(code, reason)
                serverClientDisconnectedFired = true
                server:Close()
            end)
        end)
        
        server:Listen(0, "127.0.0.1")
        local serverAddr = server:LocalAddress()
        
        local client = websocket.WebSocket:Create()
        client.Connected:Connect(function()
            client:Send("hello from client")
        end)
        
        client.MessageReceived:Connect(function(msg)
            receivedOnClient = msg
            client:Close()
        end)
        
        client.Disconnected:Connect(function(code, reason)
            clientDisconnectedFired = true
        end)
        
        client:Connect("ws://127.0.0.1:" .. tostring(serverAddr.port))
        
        task.wait(1)
        
        expect(acceptedClient ~= nil).toBe(true)
        expect(receivedOnServer).toBe("hello from client")
        expect(receivedOnClient).toBe("hello from server")
        expect(clientDisconnectedFired).toBe(true)
        expect(serverClientDisconnectedFired).toBe(true)
    end)

    it("exposes a working Ping method on open sockets and rejects pings when not connected", function()
        local server = websocket.WebSocketServer:Create()
        local acceptedClient = nil
        local serverClientDisconnectedFired = false

        server.ClientConnected:Connect(function(client)
            acceptedClient = client
            client.Disconnected:Connect(function(code, reason)
                serverClientDisconnectedFired = true
                server:Close()
            end)
        end)

        server:Listen(0, "127.0.0.1")
        local serverAddr = server:LocalAddress()

        local client = websocket.WebSocket:Create()
        local clientDisconnectedFired = false
        local clientConnected = false

        client.Connected:Connect(function()
            clientConnected = true
        end)

        client.Disconnected:Connect(function(code, reason)
            clientDisconnectedFired = true
        end)

        client:Connect("ws://127.0.0.1:" .. tostring(serverAddr.port))

        task.wait(1)

        expect(clientConnected).toBe(true)
        expect(acceptedClient ~= nil).toBe(true)
        expect(typeof(client.Ping)).toBe("function")

        client:Ping()
        client:Ping("payload")

        expect(client:IsConnected()).toBe(true)

        client:Close()
        task.wait(0.5)

        local ok, err = pcall(function()
            client:Ping()
        end)
        expect(ok).toBe(false)
        local errStr = tostring(err)
        expect((errStr:find("not connected", 1, true) ~= nil) or (errStr:find("socket is closed", 1, true) ~= nil)).toBe(true)
        expect(clientDisconnectedFired).toBe(true)
        expect(serverClientDisconnectedFired).toBe(true)
    end)
end)
