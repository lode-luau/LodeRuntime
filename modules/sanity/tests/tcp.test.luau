
local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local task = require("../../../modules/task")
local tcp = require("../../tcp")

describe("tcp", function()
    it("exposes the TcpSocket and Server classes", function()
        expect(typeof(tcp.TcpSocket)).toBe("table")
        expect(typeof(tcp.Server)).toBe("table")
        expect(typeof(tcp.TcpSocket.Create)).toBe("function")
        expect(typeof(tcp.Server.Create)).toBe("function")
    end)

    it("rejects an invalid host", function()
        local ok, err = pcall(function()
            tcp.TcpSocket:Create():Connect("not an ip address", 12345)
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("dns:", 1, true) ~= nil).toBe(true)
    end)

    it("rejects an invalid port", function()
        local ok, err = pcall(function()
            tcp.TcpSocket:Create():Connect("127.0.0.1", -1)
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("port must be an integer", 1, true) ~= nil).toBe(true)
    end)

    it("rejects non-string host", function()
        local ok, err = pcall(function()
            tcp.TcpSocket:Create():Connect(123 :: any, 12345)
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("host must be a string", 1, true) ~= nil).toBe(true)
    end)

    it("fails with connection refused on unreachable port", function()
        local ok, err = pcall(function()
            tcp.TcpSocket:Create():Connect("127.0.0.1", 1)
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("connection refused", 1, true) ~= nil).toBe(true)
    end)

    it("supports TLS client option on socket connection", function()
        local ok, err = pcall(function()
            local socket = tcp.TcpSocket.Create()
            socket:Connect("127.0.0.1", 1, { tls = true })
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("connect:", 1, true) ~= nil).toBe(true)
    end)

    it("rejects a server on an invalid host", function()
        local ok, err = pcall(function()
            tcp.Server:Create():Listen(0, "not an ip address")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("IPv4 or IPv6 address", 1, true) ~= nil).toBe(true)
    end)
    
    it("rejects sending on unconnected socket", function()
        local ok, err = pcall(function()
            tcp.TcpSocket:Create():Send("hello")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("not connected", 1, true) ~= nil).toBe(true)
    end)

    it("rejects invalid backlog type in Server:Create", function()
        local ok, err = pcall(function()
            tcp.Server:Create({ backlog = "invalid" })
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("backlog must be a number", 1, true) ~= nil).toBe(true)
    end)

    it("rejects missing port in Connect", function()
        local ok, err = pcall(function()
            (tcp.TcpSocket:Create() :: any):Connect("127.0.0.1")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("port must be a number", 1, true) ~= nil).toBe(true)
    end)

    it("times out on unreachable routable destination with timeout", function()
        local ok, err = pcall(function()
            local s = tcp.TcpSocket:Create()
            s:Connect("192.0.2.1", 80, 0.05)
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("timed out", 1, true) ~= nil or msg:find("timeout", 1, true) ~= nil).toBe(true)
    end)
end)

describe("tcp connect/send/receive", function()
    it("exchanges messages between a client and an accepted server connection", function()
        local server = tcp.Server:Create()
        local serverGot: any = nil
        local serverDisconnected = false

        server.ClientConnected:Connect(function(client)
            client.MessageReceived:Connect(function(msg)
                serverGot = msg
                client:Send("hello from server")
            end)
            client.Disconnected:Connect(function()
                serverDisconnected = true
            end)
        end)

        server:Listen(0, "127.0.0.1")
        local serverAddr = server:LocalAddress()
        expect(serverAddr.port > 0).toBe(true)

        local client = tcp.TcpSocket:Create()
        local clientGot: any = nil
        local clientDisconnected = false

        client.MessageReceived:Connect(function(msg)
            clientGot = msg
        end)
        client.Disconnected:Connect(function()
            clientDisconnected = true
        end)

        client:Connect(serverAddr.host, serverAddr.port)
        expect(client:IsConnected()).toBe(true)

        client:Send("hello from client")
        local reply = { client.MessageReceived:Wait(5) }
        expect(#reply > 0).toBe(true)
        expect(reply[1]).toBe("hello from server")
        expect(serverGot).toBe("hello from client")

        client:Close()
        task.wait(0.1)
        expect(clientDisconnected or not client:IsConnected()).toBe(true)
        expect(serverDisconnected).toBe(true)

        server:Close()
    end)

    it("rejects socket options before connecting", function()
        local client = tcp.TcpSocket:Create()
        local ok, err = pcall(function()
            client:SetNoDelay(true)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("not connected", 1, true) ~= nil).toBe(true)

        ok, err = pcall(function()
            client:SetKeepAlive(true, 5)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("not connected", 1, true) ~= nil).toBe(true)
    end)

    it("applies socket options to a connected socket", function()
        local server = tcp.Server:Create()
        local serverGot = false
        server.ClientConnected:Connect(function(_client)
            serverGot = true
            _client:Close()
        end)
        server:Listen(0, "127.0.0.1")
        local serverAddr = server:LocalAddress()

        local client = tcp.TcpSocket:Create()
        client:Connect(serverAddr.host, serverAddr.port)
        expect(client:IsConnected()).toBe(true)

        client:SetNoDelay(true)
        client:SetNoDelay(false)
        client:SetKeepAlive(true)
        client:SetKeepAlive(true, 5)
        client:SetKeepAlive(false)

        local ok, err = pcall(function()
            client:SetNoDelay("yes" :: any)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("enabled must be a boolean", 1, true) ~= nil).toBe(true)

        ok, err = pcall(function()
            client:SetKeepAlive(true, -1)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("non-negative number", 1, true) ~= nil).toBe(true)

        client:Close()
        server:Close()
    end)
end)

describe("tcp remote address", function()
    it("exposes the peer address of an accepted connection", function()
        local server = tcp.Server:Create()
        local seen = nil
        server.ClientConnected:Connect(function(client)
            seen = client
            task.wait(0.05)
            client:Close()
        end)
        server:Listen(0, "127.0.0.1")
        local addr = server:LocalAddress()

        local client = tcp.TcpSocket:Create()
        client.Connected:Connect(function()
            local remote = client:RemoteAddress()
            expect(remote.host).toBe("127.0.0.1")
            expect(remote.port).toBe(addr.port)
            client:Close()
        end)
        client:Connect(addr.host, addr.port)
        task.wait(0.2)
        server:Close()
        expect(seen).never.toBe(nil)
    end)
end)
