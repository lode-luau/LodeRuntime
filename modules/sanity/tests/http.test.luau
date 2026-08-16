--!strict
--!native

-- Regression tests for the native http module (new API)
local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local task = require("../../../modules/task")
local http = require("../../http")

describe("http", function()
    it("exposes common status codes as numbers", function()
        expect(http.StatusCodes.Ok).toBe(200)
        expect(http.StatusCodes.NotFound).toBe(404)
        expect(http.StatusCodes.InternalServerError).toBe(500)
    end)

    it("rejects URLs without a scheme", function()
        local ok, err = pcall(function()
            http.fetch("not a url")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("invalid-url", 1, true) ~= nil or msg:find("URL must include a scheme", 1, true) ~= nil).toBe(true)
    end)

    it("rejects URLs with an empty host", function()
        local ok, err = pcall(function()
            http.fetch("http:///path")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("invalid-url", 1, true) ~= nil or msg:find("URL must include a host", 1, true) ~= nil).toBe(true)
    end)

    it("fails with connection-refused on an unreachable port", function()
        local ok, err = pcall(function()
            http.fetch("http://127.0.0.1:1/")
        end)
        expect(ok).toBe(false)
        local msg = tostring(err)
        expect(msg:find("connection%-refused", 1, false) ~= nil).toBe(true)
    end)

    it("HttpClient:RequestAsync reports connection errors", function()
        local client = http.HttpClient.Create()
        local done = false
        local response: any = nil
        local errorMsg: any = nil
        
        client.ResponseReceived:Connect(function(res)
            response = res
            done = true
        end)
        client.ErrorOccurred:Connect(function(err)
            errorMsg = err
            done = true
        end)
        
        client:RequestAsync("http://127.0.0.1:1/")
        task.wait(0.5)
        
        expect(done).toBe(true)
        expect(response).toBe(nil)
        expect(typeof(errorMsg)).toBe("string")
        expect(tostring(errorMsg):find("connection%-refused", 1, false) ~= nil).toBe(true)
        client:Close()
    end)

    it("HttpClient:Request reports invalid URLs", function()
        local client = http.HttpClient.Create()
        local ok, err = pcall(function()
            client:Request("bad url")
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("URL must include a scheme", 1, true) ~= nil).toBe(true)
        client:Close()
    end)
    
    it("HttpServer throws when starting twice", function()
        local server = http.Server.Create()
        server:Listen(0)
        local ok, err = pcall(function()
            server:Listen(0)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("already listening", 1, true) ~= nil).toBe(true)
        server:Close()
    end)
    it("fetch rejects invalid options type", function()
        local ok, err = pcall(function()
            http.fetch("http://127.0.0.1:1/", "not a table")
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("fetch options must be a table", 1, true) ~= nil).toBe(true)
    end)

    it("fetch rejects invalid option fields", function()
        local ok, err = pcall(function()
            http.fetch("http://127.0.0.1:1/", { timeout = "inf" })
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("fetch options 'timeout' must be a number", 1, true) ~= nil).toBe(true)
    end)

    it("accepts buffer request bodies", function()
        local body = buffer.create(4)
        buffer.writeu8(body, 0, 0)
        buffer.writeu8(body, 1, 255)
        buffer.writeu8(body, 2, 65)
        buffer.writeu8(body, 3, 0)
        local ok, err = pcall(function()
            http.fetch("http://127.0.0.1:1/", { method = "POST", body = body })
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("connection%-refused", 1, false) ~= nil).toBe(true)
        expect(tostring(err):find("body.*string", 1, false) == nil).toBe(true)
    end)
    
    it("HttpServer:Listen rejects invalid host type", function()
        local server = http.Server.Create()
        local ok, err = pcall(function()
            server:Listen(8080, true)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("expected host as string", 1, true) ~= nil).toBe(true)
        server:Close()
    end)
end)
