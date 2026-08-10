--!strict
--!native

local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local socket = require("../../socket")

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
