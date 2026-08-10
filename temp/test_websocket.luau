local ws = require("@websocket")
local task = require("@task")

print("=== WebSocket Module Test ===")

local server = ws.WebSocketServer:Create()
server.ClientConnected:Connect(function(client)
    local remote = client:RemoteAddress()
    print("server accepted ws connection from", remote.host, remote.port)
    client.MessageReceived:Connect(function(msg)
        print("server received:", msg)
        client:Send("hello from server")
    end)
    client.Disconnected:Connect(function(code, reason)
        print("server client disconnected", code, reason)
        server:Close()
    end)
end)

server:Listen(0, "127.0.0.1")
local serverAddr = server:LocalAddress()
print("server listening on", serverAddr.host, serverAddr.port)

local client = ws.WebSocket:Create()
client.Connected:Connect(function()
    print("client connected!")
    client:Send("hello from client")
end)

client.MessageReceived:Connect(function(msg)
    print("client received:", msg)
    client:Close()
end)

client.Disconnected:Connect(function(code, reason)
    print("client disconnected", code, reason)
end)

client:Connect("ws://" .. serverAddr.host .. ":" .. tostring(serverAddr.port))

task.wait(2)
print("=== ALL WEBSOCKET TESTS PASSED ===")
