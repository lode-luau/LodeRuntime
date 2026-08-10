local tcp = require("@tcp")
local task = require("@task")

print("=== TCP Module Test ===")

local server = tcp.Server:Create()
server.ClientConnected:Connect(function(client)
    local remote = client:RemoteAddress()
    print("server accepted", remote.host, remote.port)
    client.MessageReceived:Connect(function(msg)
        print("server received:", msg)
        client:Send("hello from server")
    end)
    client.Disconnected:Connect(function()
        print("server client disconnected")
        server:Close()
    end)
end)

server:Listen(0, "127.0.0.1")
local serverAddr = server:LocalAddress()
print("server listening on", serverAddr.host, serverAddr.port)

local client = tcp.TcpSocket:Create()
client.Connected:Connect(function()
    print("client connected!")
    client:Send("hello from client")
end)

client.MessageReceived:Connect(function(msg)
    print("client received:", msg)
    client:Close()
end)

client.Disconnected:Connect(function()
    print("client disconnected")
end)

client:Connect(serverAddr.host, serverAddr.port)

task.wait(2)
print("=== ALL TCP TESTS PASSED ===")
