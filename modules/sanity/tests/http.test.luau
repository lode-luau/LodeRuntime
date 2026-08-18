--!strict
--!native

-- Regression tests for the native http module (new API)
local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local task = require("../../../modules/task")
local http = require("../../http")
local tcp = require("../../tcp")

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

    it("HttpServer handles HTTP request and response cycle", function()
        local server = http.Server.Create()
        server:Listen(0, "127.0.0.1")
        local addr = server:LocalAddress()
        expect(addr.port > 0).toBe(true)

        local receivedMethod = ""
        local receivedPath = ""
        local receivedQuery = ""
        local receivedHeader = ""

        server.RequestReceived:Connect(function(req, res)
            receivedMethod = req.method
            receivedPath = req.path
            receivedQuery = req.query
            receivedHeader = req.headers["x-client-test"] or ""

            res:SetHeader("X-Server-Response", "LodeHttpTest")
            res:SetStatus(200)
            res:Send("Hello from HttpServer!")
        end)

        local url = string.format("http://127.0.0.1:%d/api/greet?user=alice", addr.port)
        local response = http.fetch(url, {
            headers = {
                ["x-client-test"] = "ClientGreeting",
            }
        })

        expect(response.status).toBe(200)
        expect(tostring(response.body)).toBe("Hello from HttpServer!")
        expect(response.headers["x-server-response"]).toBe("LodeHttpTest")

        expect(receivedMethod).toBe("GET")
        expect(receivedPath).toBe("/api/greet")
        expect(receivedQuery).toBe("user=alice")
        expect(receivedHeader).toBe("ClientGreeting")

        server:Close()
    end)

    it("HttpServer handles concurrent requests cleanly", function()
        local server = http.Server.Create()
        server:Listen(0, "127.0.0.1")
        local addr = server:LocalAddress()

        local count = 0
        server.RequestReceived:Connect(function(req, res)
            count += 1
            res:SetStatus(200)
            res:Send("Echo: " .. req.path)
        end)

        local t1 = task.spawn(function()
            local r = http.fetch(string.format("http://127.0.0.1:%d/req1", addr.port))
            expect(r.status).toBe(200)
            expect(tostring(r.body)).toBe("Echo: /req1")
        end)

        local t2 = task.spawn(function()
            local r = http.fetch(string.format("http://127.0.0.1:%d/req2", addr.port))
            expect(r.status).toBe(200)
            expect(tostring(r.body)).toBe("Echo: /req2")
        end)

        local t3 = task.spawn(function()
            local r = http.fetch(string.format("http://127.0.0.1:%d/req3", addr.port))
            expect(r.status).toBe(200)
            expect(tostring(r.body)).toBe("Echo: /req3")
        end)

        local deadline = os.clock() + 2
        while count < 3 and os.clock() < deadline do
            task.wait(0.05)
        end
        expect(count).toBe(3)
        server:Close()
    end)

    it("HttpClient reuses keep-alive connections and honors server close", function()
        local server = tcp.Server:Create()
        local accepted = 0
        local requests = 0
        server.ClientConnected:Connect(function(socket)
            accepted += 1
            socket.MessageReceived:Connect(function(message)
                if message:find("\r\n\r\n", 1, true) == nil then
                    return
                end
                requests += 1
                local connection = requests == 2 and "close" or "keep-alive"
                socket:Send("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nConnection: " .. connection .. "\r\n\r\n" .. tostring(requests))
            end)
        end)
        server:Listen(0, "127.0.0.1")
        local address = server:LocalAddress()
        local client = http.HttpClient:Create({
            maxIdleConnections = 1,
            maxConnectionUses = 10,
            idleTimeout = 1000,
        })
        local url = "http://127.0.0.1:" .. tostring(address.port) .. "/"

        local first = client:Request(url)
        local second = client:Request(url)
        expect(tostring(first.body)).toBe("1")
        expect(tostring(second.body)).toBe("2")
        expect(accepted).toBe(1)

        client:Close()
        server:Close()
    end)

    it("HttpClient disables reuse per request and at the configured use limit", function()
        local function runCase(clientOptions: any, requestOptions: any): number
            local server = tcp.Server:Create()
            local accepted = 0
            server.ClientConnected:Connect(function(socket)
                accepted += 1
                socket.MessageReceived:Connect(function(message)
                    if message:find("\r\n\r\n", 1, true) ~= nil then
                        socket:Send("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: keep-alive\r\n\r\nok")
                    end
                end)
            end)
            server:Listen(0, "127.0.0.1")
            local address = server:LocalAddress()
            local client = http.HttpClient:Create(clientOptions)
            local url = "http://127.0.0.1:" .. tostring(address.port) .. "/"
            expect(client:Request(url, requestOptions).status).toBe(200)
            expect(client:Request(url, requestOptions).status).toBe(200)
            client:Close()
            server:Close()
            return accepted
        end

        expect(runCase({}, { keepAlive = false })).toBe(2)
        expect(runCase({ maxConnectionUses = 1 }, nil)).toBe(2)
    end)

    it("HttpClient expires idle connections", function()
        local server = tcp.Server:Create()
        local accepted = 0
        server.ClientConnected:Connect(function(socket)
            accepted += 1
            socket.MessageReceived:Connect(function(message)
                if message:find("\r\n\r\n", 1, true) ~= nil then
                    socket:Send("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: keep-alive\r\n\r\nok")
                end
            end)
        end)
        server:Listen(0, "127.0.0.1")
        local address = server:LocalAddress()
        local client = http.HttpClient:Create({ idleTimeout = 20 })
        local url = "http://127.0.0.1:" .. tostring(address.port) .. "/"
        expect(client:Request(url).status).toBe(200)
        task.wait(0.05)
        expect(client:Request(url).status).toBe(200)
        expect(accepted).toBe(2)
        client:Close()
        server:Close()
    end)

    it("HttpClient evicts the least recently used idle origin at the pool limit", function()
        local function makeServer()
            local server = tcp.Server:Create()
            local accepted = 0
            server.ClientConnected:Connect(function(socket)
                accepted += 1
                socket.MessageReceived:Connect(function(message)
                    if message:find("\r\n\r\n", 1, true) ~= nil then
                        socket:Send("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: keep-alive\r\n\r\nok")
                    end
                end)
            end)
            server:Listen(0, "127.0.0.1")
            return server, function() return accepted end
        end

        local first, firstAccepted = makeServer()
        local second, secondAccepted = makeServer()
        local client = http.HttpClient:Create({ maxIdleConnections = 1 })
        local firstUrl = "http://127.0.0.1:" .. tostring(first:LocalAddress().port) .. "/"
        local secondUrl = "http://127.0.0.1:" .. tostring(second:LocalAddress().port) .. "/"
        expect(client:Request(firstUrl).status).toBe(200)
        expect(client:Request(secondUrl).status).toBe(200)
        expect(client:Request(firstUrl).status).toBe(200)
        expect(firstAccepted()).toBe(2)
        expect(secondAccepted()).toBe(1)
        client:Close()
        first:Close()
        second:Close()
    end)

    it("HttpClient discards malformed connections before another request", function()
        local server = tcp.Server:Create()
        local accepted = 0
        local requests = 0
        server.ClientConnected:Connect(function(socket)
            accepted += 1
            socket.MessageReceived:Connect(function(message)
                if message:find("\r\n\r\n", 1, true) == nil then
                    return
                end
                requests += 1
                if requests == 1 then
                    socket:Send("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 3\r\n\r\nok")
                else
                    socket:Send("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok")
                end
            end)
        end)
        server:Listen(0, "127.0.0.1")
        local address = server:LocalAddress()
        local client = http.HttpClient:Create()
        local url = "http://127.0.0.1:" .. tostring(address.port) .. "/"
        local ok = pcall(function()
            client:Request(url)
        end)
        expect(ok).toBe(false)
        expect(client:Request(url).status).toBe(200)
        expect(accepted).toBe(2)
        client:Close()
        server:Close()
    end)

    it("http.fetch completes a redirect chain exactly once", function()
        local server = http.Server.Create()
        server:Listen(0, "127.0.0.1")
        local addr = server:LocalAddress()
        local baseUrl = string.format("http://127.0.0.1:%d", addr.port)
        local requests = 0

        server.RequestReceived:Connect(function(req, res)
            requests += 1
            if req.path == "/start" then
                res:SetStatus(302)
                res:SetHeader("Location", "/middle")
                res:Send("")
            elseif req.path == "/middle" then
                res:SetStatus(307)
                res:SetHeader("Location", "/final")
                res:Send("")
            else
                res:SetStatus(200)
                res:Send("redirect complete")
            end
        end)

        local resumes = 0
        local response = http.fetch(baseUrl .. "/start")
        resumes += 1

        expect(resumes).toBe(1)
        expect(requests).toBe(3)
        expect(response.status).toBe(200)
        expect(tostring(response.body)).toBe("redirect complete")
        server:Close()
    end)

    it("http.fetch strips credentials and applies 303 semantics across origins", function()
        local redirector = http.Server.Create()
        local destination = http.Server.Create()
        redirector:Listen(0, "127.0.0.1")
        destination:Listen(0, "127.0.0.1")
        local redirectAddr = redirector:LocalAddress()
        local destinationAddr = destination:LocalAddress()

        local receivedMethod = ""
        local receivedBody = ""
        local receivedAuthorization: any = nil
        local receivedCookie: any = nil

        destination.RequestReceived:Connect(function(req, res)
            receivedMethod = req.method
            receivedBody = req.body
            receivedAuthorization = req.headers["authorization"]
            receivedCookie = req.headers["cookie"]
            res:SetStatus(200)
            res:Send("safe")
        end)

        redirector.RequestReceived:Connect(function(_, res)
            res:SetStatus(303)
            res:SetHeader("Location", string.format("http://127.0.0.1:%d/final", destinationAddr.port))
            res:Send("")
        end)

        local response = http.fetch(string.format("http://127.0.0.1:%d/start", redirectAddr.port), {
            method = "POST",
            body = "secret body",
            headers = {
                Authorization = "Bearer secret",
                Cookie = "session=secret",
            },
        })

        expect(response.status).toBe(200)
        expect(receivedMethod).toBe("GET")
        expect(receivedBody).toBe("")
        expect(receivedAuthorization).toBe(nil)
        expect(receivedCookie).toBe(nil)
        redirector:Close()
        destination:Close()
    end)

    it("http.fetch rejects ambiguous response framing", function()
        local function fetchRaw(responseText: string): (boolean, string)
            local server = tcp.Server:Create()
            server.ClientConnected:Connect(function(client)
                client.MessageReceived:Once(function()
                    client:Send(responseText)
                    client:Close()
                end)
            end)
            server:Listen(0, "127.0.0.1")
            local addr = server:LocalAddress()
            local ok, err = pcall(function()
                http.fetch(string.format("http://127.0.0.1:%d/", addr.port))
            end)
            server:Close()
            return ok, tostring(err)
        end

        local ok, err = fetchRaw("HTTP/1.1 200 OK\r\nContent-Length: 3\r\nContent-Length: 20\r\n\r\nabc")
        expect(ok).toBe(false)
        expect(err:find("conflicting content%-length") ~= nil).toBe(true)

        ok, err = fetchRaw("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 3\r\n\r\n0\r\n\r\n")
        expect(ok).toBe(false)
        expect(err:find("transfer%-encoding conflicts") ~= nil).toBe(true)
    end)

    it("HttpServer accepts TLS options", function()
        local server = http.Server.Create({ tls = true })
        server:Listen(0, "127.0.0.1", { tls = true })
        expect(server:IsListening()).toBe(true)
        server:Close()
    end)

    it("http.fetch supports streaming chunked uploads and onData streaming", function()
        local server = http.Server.Create()
        server:Listen(0, "127.0.0.1")
        local addr = server:LocalAddress()

        local receivedBody = ""
        server.RequestReceived:Connect(function(req, res)
            receivedBody = req.body
            res:SetStatus(200)
            res:Send("Streaming Response Data")
        end)

        local chunksReceived = {}
        local response = http.fetch(string.format("http://127.0.0.1:%d/stream", addr.port), {
            method = "POST",
            body = "Chunked Payload Data",
            chunked = true,
            onData = function(chunk)
                table.insert(chunksReceived, chunk)
            end,
        })

        expect(response.status).toBe(200)
        expect(receivedBody).toBe("Chunked Payload Data")
        expect(#chunksReceived > 0).toBe(true)
        server:Close()
    end)
end)
