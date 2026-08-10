local ws = require("@websocket")
local task = require("@task")

local server = ws.WebSocketServer:Create()
server.ClientConnected:Connect(function(client)
    print("Server accepted client")
    client.ErrorOccurred:Connect(function(err)
        print("Server client error:", err)
    end)
end)

server.ErrorOccurred:Connect(function(err)
    print("Server error:", err)
end)

server:Listen(0, "127.0.0.1")
local serverAddr = server:LocalAddress()

local client = ws.WebSocket:Create()
client.ErrorOccurred:Connect(function(err)
    print("Client error:", err)
end)

client:Connect("ws://127.0.0.1:" .. tostring(serverAddr.port))

task.wait(1)
print("Done")
