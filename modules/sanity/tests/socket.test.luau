
local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local task = require("../../../modules/task")
local socket = require("@socket")

describe("socket", function()
    it("exposes the TcpSocket and Server classes from tcp module", function()
        expect(typeof(socket.TcpSocket)).toBe("table")
        expect(typeof(socket.Server)).toBe("table")
        expect(typeof(socket.TcpSocket.Create)).toBe("function")
        expect(typeof(socket.Server.Create)).toBe("function")
    end)

    it("rejects resolve on an empty host", function()
        local ok, err = pcall(function()
            socket.resolve("")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("host must not be empty", 1, true) ~= nil).toBe(true)
    end)

    it("rejects resolve on invalid type", function()
        local ok, err = pcall(function()
            socket.resolve(123 :: any)
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("host must be a string", 1, true) ~= nil).toBe(true)
    end)

    it("rejects resolveAsync without callback", function()
        local ok, err = pcall(function()
            (socket.resolveAsync :: any)("github.com")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("expected host and callback", 1, true) ~= nil).toBe(true)
    end)

    it("rejects sending on unconnected socket", function()
        local ok, err = pcall(function()
            socket.TcpSocket:Create():Send("hello")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("not connected", 1, true) ~= nil).toBe(true)
    end)
end)

describe("socket resolve", function()
    it("resolves a literal IPv4 address synchronously", function()
        expect(socket.resolve("127.0.0.1")).toBe("127.0.0.1")
    end)

    it("resolves a literal IPv4 address via the async callback", function()
        local resolved = false
        local address, resolveErr = nil, nil
        socket.resolveAsync("127.0.0.1", function(addr, err)
            address, resolveErr = addr, err
            resolved = true
        end)
        for _ = 1, 200 do
            if resolved then break end
            task.wait(0.01)
        end
        expect(resolved).toBe(true)
        expect(address).toBe("127.0.0.1")
        expect(resolveErr).toBe(nil)
    end)
end)

describe("socket connect/send/receive", function()
    it("echoes a message over a loopback TCP connection", function()
        local server = socket.Server:Create()
        server:Listen(0)
        expect(server:IsListening()).toBe(true)
        local serverAddr = server:LocalAddress()

        local serverEchoed = false
        local serverDisconnected = false
        server.ClientConnected:Connect(function(client)
            client.MessageReceived:Connect(function(data)
                serverEchoed = true
                client:Send("echo: " .. data)
            end)
            client.Disconnected:Connect(function()
                serverDisconnected = true
            end)
        end)

        local sock = socket.TcpSocket:Create()
        sock:Connect("127.0.0.1", serverAddr.port)
        expect(sock:IsConnected()).toBe(true)

        local clientDisconnected = false
        sock.Disconnected:Connect(function()
            clientDisconnected = true
        end)

        task.wait(0.05)
        sock:Send("hello")
        local reply = { sock.MessageReceived:Wait(5) }
        expect(#reply > 0).toBe(true)
        expect(reply[1]).toBe("echo: hello")
        expect(serverEchoed).toBe(true)

        sock:Close()
        expect(sock:IsConnected()).toBe(false)
        sock.Disconnected:Wait(5)
        expect(clientDisconnected).toBe(true)

        for _ = 1, 200 do
            if serverDisconnected then break end
            task.wait(0.01)
        end
        expect(serverDisconnected).toBe(true)

        server:Close()
        expect(server:IsListening()).toBe(false)
    end)
end)
