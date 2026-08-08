--!strict
--!native

-- Regression tests for the native http client module.
-- These tests only exercise deterministic error paths (no network or live
-- server required); positive-path coverage is done manually against a local
-- server via temp/http_test.luau.

local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local task = require("../../../modules/task")
local http = require("../../http")

describe("http", function()
    it("exposes common status codes as numbers", function()
        expect(http.statusCode.OK).toBe(200)
        expect(http.statusCode.NOT_FOUND).toBe(404)
        expect(http.statusCode.INTERNAL_SERVER_ERROR).toBe(500)
    end)

    it("rejects URLs without a scheme", function()
        local ok, err = pcall(function()
            http.get("not a url")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("invalid-url", 1, true) ~= nil).toBe(true)
    end)

    it("rejects URLs with an empty host", function()
        local ok, err = pcall(function()
            http.request("http:///path")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("invalid-url", 1, true) ~= nil).toBe(true)
    end)

    it("rejects https until TLS support lands", function()
        local ok, err = pcall(function()
            http.get("https://example.com/")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("tls", 1, true) ~= nil).toBe(true)
    end)

    it("fails with connection-refused on an unreachable port", function()
        local ok, err = pcall(function()
            http.get("http://127.0.0.1:1/")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("connection-refused", 1, true) ~= nil).toBe(true)
    end)

    it("getWithCallback reports connection errors", function()
        local done = false
        local response: any = nil
        local errorMsg: any = nil
        http.getWithCallback("http://127.0.0.1:1/", {}, function(resp, err)
            response = resp
            errorMsg = err
            done = true
        end)
        task.wait(0.5)
        expect(done).toBe(true)
        expect(response).toBe(nil)
        expect(typeof(errorMsg)).toBe("string")
        expect(tostring(errorMsg):find("connection-refused", 1, true) ~= nil).toBe(true)
    end)

    it("requestWithCallback reports invalid URLs", function()
        local done = false
        local errorMsg: any = nil
        http.requestWithCallback("bad url", {}, function(resp, err)
            errorMsg = err
            done = true
        end)
        task.wait(0.5)
        expect(done).toBe(true)
        expect(tostring(errorMsg):find("invalid-url", 1, true) ~= nil).toBe(true)
    end)

    it("postWithCallback reports invalid options", function()
        local ok, err = pcall(function()
            http.postWithCallback("http://127.0.0.1:1/", "data", {}, "not a function")
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("callback must be a function", 1, true) ~= nil).toBe(true)
    end)
end)
