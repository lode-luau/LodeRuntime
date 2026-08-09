--!strict
--!native

-- Regression tests for the native websocket module.
-- These tests only exercise deterministic error paths (no network or live
-- server required); positive-path coverage is done against a local loopback
-- server via temp/test_websocket.luau.

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
end)
