--!strict
--!native

-- Pipe module tests: API surface, error categorization (signals only for pipe
-- errors; API misuse raises Lua VM errors), and named-pipe IPC with proper
-- cleanup (all streams/servers closed so the event loop can drain).

local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local pipe = require("../../pipe")
local task = require("../../../modules/task")

local nameCounter = 0
local function uniqueName(prefix: string): string
    nameCounter += 1
    return prefix .. "_" .. tostring(os.time()) .. "_" .. tostring(nameCounter)
end

-- Sets up a server + connected client stream, returning both so the caller
-- can exercise type/state validation on an open stream and then close both.
local function openClient(name: string): { stream: any, server: any }
    local server = pipe.PipeServer:Create()
    server:Listen(name)
    local client = pipe.PipeStream:Create()
    client:Connect(name)
    return { stream = client, server = server }
end

describe("pipe", function()
    it("exposes the PipeStream and PipeServer classes", function()
        expect(typeof(pipe.PipeStream)).toBe("table")
        expect(typeof(pipe.PipeServer)).toBe("table")
        expect(typeof(pipe.PipeStream.Create)).toBe("function")
        expect(typeof(pipe.PipeServer.Create)).toBe("function")
        expect(typeof(pipe.IsPipe)).toBe("function")
    end)

    it("rejects a non-string path in Connect", function()
        local stream = pipe.PipeStream:Create()
        local ok, err = pcall(function()
            (stream :: any):Connect(123)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("path must be a string", 1, true) ~= nil).toBe(true)
        stream:Close()
    end)

    it("rejects an empty path in Connect", function()
        local stream = pipe.PipeStream:Create()
        local ok, err = pcall(function()
            stream:Connect("")
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("path must not be empty", 1, true) ~= nil).toBe(true)
        stream:Close()
    end)

    it("rejects Write on a not-open stream", function()
        local stream = pipe.PipeStream:Create()
        local ok, err = pcall(function()
            stream:Write("hello")
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("not open", 1, true) ~= nil).toBe(true)
        stream:Close()
    end)

    it("rejects Write on a closed stream", function()
        local stream = pipe.PipeStream:Create()
        stream:Close()
        local ok, err = pcall(function()
            stream:Write("hello")
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("stream is closed", 1, true) ~= nil).toBe(true)
    end)

    it("rejects WriteLine on a not-open stream", function()
        local stream = pipe.PipeStream:Create()
        local ok, err = pcall(function()
            stream:WriteLine("hello")
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("not open", 1, true) ~= nil).toBe(true)
        stream:Close()
    end)

    it("rejects WriteLine on a closed stream", function()
        local stream = pipe.PipeStream:Create()
        stream:Close()
        local ok, err = pcall(function()
            stream:WriteLine("hello")
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("stream is closed", 1, true) ~= nil).toBe(true)
    end)

    it("rejects Write with a non-string/non-buffer on an open stream", function()
        local pair = openClient(uniqueName("lode_pipe_write_type"))
        local ok, err = pcall(function()
            (pair.stream :: any):Write(123)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("data must be a string or buffer", 1, true) ~= nil).toBe(true)
        pair.stream:Close()
        pair.server:Close()
    end)

    it("rejects WriteLine with a non-string on an open stream", function()
        local pair = openClient(uniqueName("lode_pipe_writeline_type"))
        local ok, err = pcall(function()
            (pair.stream :: any):WriteLine(123)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("data must be a string", 1, true) ~= nil).toBe(true)
        pair.stream:Close()
        pair.server:Close()
    end)

    it("rejects Read on a not-open stream", function()
        local stream = pipe.PipeStream:Create()
        local ok, err = pcall(function()
            stream:Read()
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("not open", 1, true) ~= nil).toBe(true)
        stream:Close()
    end)

    it("rejects Read with a non-number bytes on an open stream", function()
        local pair = openClient(uniqueName("lode_pipe_read_type"))
        local ok, err = pcall(function()
            (pair.stream :: any):Read("5")
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("bytes must be a number or nil", 1, true) ~= nil).toBe(true)
        pair.stream:Close()
        pair.server:Close()
    end)

    it("rejects ReadBuffer with a non-number bytes on an open stream", function()
        local pair = openClient(uniqueName("lode_pipe_readbuf_type"))
        local ok, err = pcall(function()
            (pair.stream :: any):ReadBuffer("5")
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("bytes must be a number or nil", 1, true) ~= nil).toBe(true)
        pair.stream:Close()
        pair.server:Close()
    end)

    it("rejects ReadLine on a not-open stream", function()
        local stream = pipe.PipeStream:Create()
        local ok, err = pcall(function()
            stream:ReadLine()
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("not open", 1, true) ~= nil).toBe(true)
        stream:Close()
    end)

    it("rejects StartStreaming on a not-open stream", function()
        local stream = pipe.PipeStream:Create()
        local ok, err = pcall(function()
            stream:StartStreaming()
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("not open", 1, true) ~= nil).toBe(true)
        stream:Close()
    end)

    it("rejects OpenFD with a non-number fd", function()
        local stream = pipe.PipeStream:Create()
        local ok, err = pcall(function()
            (stream :: any):OpenFD("3")
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("fd must be a number", 1, true) ~= nil).toBe(true)
        stream:Close()
    end)

    it("rejects Listen with a non-string path", function()
        local server = pipe.PipeServer:Create()
        local ok, err = pcall(function()
            (server :: any):Listen(123)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("path must be a string", 1, true) ~= nil).toBe(true)
        server:Close()
    end)

    it("rejects Listen with an empty path", function()
        local server = pipe.PipeServer:Create()
        local ok, err = pcall(function()
            server:Listen("")
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("path must not be empty", 1, true) ~= nil).toBe(true)
        server:Close()
    end)

    it("rejects Accept when not listening", function()
        local server = pipe.PipeServer:Create()
        local ok, err = pcall(function()
            server:Accept()
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("not listening", 1, true) ~= nil).toBe(true)
        server:Close()
    end)

    it("rejects IsPipe with a non-number fd", function()
        local ok, err = pcall(function()
            (pipe.IsPipe :: any)("3")
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("fd must be a number", 1, true) ~= nil).toBe(true)
    end)
end)

describe("pipe IPC", function()
    it("exchanges data between server and client over a named pipe", function()
        local name = uniqueName("lode_pipe_ipc")
        local server = pipe.PipeServer:Create()
        local client = pipe.PipeStream:Create()

        local serverGot: { string } = {}
        local clientGot: { string } = {}

        server.ConnectionReceived:Connect(function(stream: any)
            stream.DataReceived:Connect(function(data: any)
                table.insert(serverGot, tostring(data))
            end)
            stream:WriteLine("hello from server")
        end)

        server:Listen(name)
        expect(server:IsListening()).toBe(true)

        client.DataReceived:Connect(function(data: any)
            table.insert(clientGot, tostring(data))
        end)
        client:Connect(name)
        expect(client:IsOpen()).toBe(true)

        client:WriteLine("hello from client")
        task.wait(0.2)

        expect(#serverGot >= 1).toBe(true)
        expect(#clientGot >= 1).toBe(true)

        client:Close()
        server:Close()
    end)

    it("yield-based Accept returns a connected stream and ReadLine works", function()
        local name = uniqueName("lode_pipe_accept")
        local server = pipe.PipeServer:Create()
        server:Listen(name)

        local client: any
        task.spawn(function()
            client = pipe.PipeStream:Create()
            client:Connect(name)
            client:WriteLine("accept me")
        end)

        local accepted = server:Accept()
        expect(accepted ~= nil).toBe(true)

        local line = accepted:ReadLine()
        expect(line).toBe("accept me")

        accepted:Close()
        if client ~= nil then
            client:Close()
        end
        server:Close()
    end)

    it("rejects Listen twice on the same server", function()
        local name = uniqueName("lode_pipe_twice")
        local server = pipe.PipeServer:Create()
        server:Listen(name)
        local ok, err = pcall(function()
            server:Listen(name)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("already listening", 1, true) ~= nil).toBe(true)
        server:Close()
    end)
end)