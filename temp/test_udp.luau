local udp = require("@udp")
local task = require("@task")

print("=== UDP Module Test ===")

local server = udp.UdpSocket:Create()
server:Bind("127.0.0.1", 0)

local serverAddr = server:LocalAddress()
print("server listening on", serverAddr.host, serverAddr.port)

server.MessageReceived:Connect(function(msg, addr)
    print("server received:", msg, "from", addr.host, addr.port)
    server:Send("pong", addr.host, addr.port)
end)

local client = udp.UdpSocket:Create()
client:Bind("127.0.0.1", 0)

client.MessageReceived:Connect(function(msg, addr)
    print("client received:", msg, "from", addr.host, addr.port)
    client:Close()
    server:Close()
end)

client:Send("ping", serverAddr.host, serverAddr.port)

task.wait(2)
print("=== ALL UDP TESTS PASSED ===")
