--!strict
--!native

-- TTY module tests: API surface, error categorization (signals only for TTY
-- errors; API misuse raises Lua VM errors), and proper cleanup (streams closed
-- so the event loop can drain). Tests that need a real terminal are skipped
-- when the environment is not a TTY.

local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local tty = require("../../tty")

describe("tty", function()
    it("exposes the TtyStream class and IsTTY", function()
        expect(typeof(tty.TtyStream)).toBe("table")
        expect(typeof(tty.TtyStream.Create)).toBe("function")
        expect(typeof(tty.IsTTY)).toBe("function")
    end)

    it("checks IsTTY without crashing", function()
        local isStdinTty = tty.IsTTY(0)
        local isStdoutTty = tty.IsTTY(1)
        expect(type(isStdinTty)).toBe("boolean")
        expect(type(isStdoutTty)).toBe("boolean")
    end)

    it("rejects IsTTY with a non-number fd", function()
        local ok, err = pcall(function()
            (tty.IsTTY :: any)("3")
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("fd must be a number", 1, true) ~= nil).toBe(true)
    end)

    it("rejects Create with a non-number fd", function()
        local create: any = tty.TtyStream.Create
        local ok, err = pcall(function()
            create("1", false)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("expected fd (number) and readable (boolean)", 1, true) ~= nil).toBe(true)
    end)

    it("rejects Create with a non-boolean readable", function()
        local create: any = tty.TtyStream.Create
        local ok, err = pcall(function()
            create(1, "yes")
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("expected fd (number) and readable (boolean)", 1, true) ~= nil).toBe(true)
    end)

    it("creates TtyStream if stdout is a TTY", function()
        if tty.IsTTY(1) then
            local out = tty.TtyStream:Create(1, false)
            expect(type(out)).toBe("userdata")
            expect(type(out.Write)).toBe("function")
            expect(type(out.GetWindowSize)).toBe("function")
            expect(out:IsOpen()).toBe(true)

            local size = out:GetWindowSize()
            expect(type(size)).toBe("table")
            expect(type(size.width)).toBe("number")
            expect(type(size.height)).toBe("number")

            out:Close()
            expect(out:IsOpen()).toBe(false)
        else
            sanity.skip("Stdout is not a TTY in this environment.")
        end
    end)

    it("TtyStream has all expected methods and signals", function()
        if tty.IsTTY(1) then
            local out = tty.TtyStream:Create(1, false)
            expect(type(out.Write)).toBe("function")
            expect(type(out.WriteLine)).toBe("function")
            expect(type(out.Read)).toBe("function")
            expect(type(out.ReadBuffer)).toBe("function")
            expect(type(out.ReadLine)).toBe("function")
            expect(type(out.StartStreaming)).toBe("function")
            expect(type(out.StopStreaming)).toBe("function")
            expect(type(out.SetMode)).toBe("function")
            expect(type(out.Close)).toBe("function")
            expect(type(out.Destroy)).toBe("function")

            expect(type(out.DataReceived)).toBe("table")
            expect(type(out.DataReceived.Connect)).toBe("function")
            expect(type(out.EndOfStream)).toBe("table")
            expect(type(out.EndOfStream.Connect)).toBe("function")
            expect(type(out.ErrorOccurred)).toBe("table")
            expect(type(out.ErrorOccurred.Connect)).toBe("function")

            out:Close()
        else
            sanity.skip("Stdout is not a TTY in this environment.")
        end
    end)

    it("rejects Write with invalid arguments on an open stream", function()
        if tty.IsTTY(1) then
            local out = tty.TtyStream:Create(1, false)

            local ok1, err1 = pcall(function()
                (out :: any):Write() -- missing argument
            end)
            expect(ok1).toBe(false)
            expect(tostring(err1):find("data must be a string or buffer", 1, true) ~= nil).toBe(true)

            local ok2, err2 = pcall(function()
                (out :: any):Write(12345) -- wrong type
            end)
            expect(ok2).toBe(false)
            expect(tostring(err2):find("data must be a string or buffer", 1, true) ~= nil).toBe(true)

            out:Close()
        else
            sanity.skip("Stdout is not a TTY in this environment.")
        end
    end)

    it("rejects WriteLine with a non-string on an open stream", function()
        if tty.IsTTY(1) then
            local out = tty.TtyStream:Create(1, false)
            local ok, err = pcall(function()
                (out :: any):WriteLine(123)
            end)
            expect(ok).toBe(false)
            expect(tostring(err):find("data must be a string", 1, true) ~= nil).toBe(true)
            out:Close()
        else
            sanity.skip("Stdout is not a TTY in this environment.")
        end
    end)

    it("rejects Write on a closed stream", function()
        if tty.IsTTY(1) then
            local out = tty.TtyStream:Create(1, false)
            out:Close()
            local ok, err = pcall(function()
                out:Write("hello")
            end)
            expect(ok).toBe(false)
            expect(tostring(err):find("stream is closed", 1, true) ~= nil).toBe(true)
        else
            sanity.skip("Stdout is not a TTY in this environment.")
        end
    end)

    it("rejects GetWindowSize on a closed stream", function()
        if tty.IsTTY(1) then
            local out = tty.TtyStream:Create(1, false)
            out:Close()
            local ok, err = pcall(function()
                out:GetWindowSize()
            end)
            expect(ok).toBe(false)
            expect(tostring(err):find("not open", 1, true) ~= nil).toBe(true)
        else
            sanity.skip("Stdout is not a TTY in this environment.")
        end
    end)

    it("rejects SetMode with a non-number on an open stream", function()
        if tty.IsTTY(1) then
            local out = tty.TtyStream:Create(1, false)
            local ok, err = pcall(function()
                (out :: any):SetMode("raw")
            end)
            expect(ok).toBe(false)
            expect(tostring(err):find("mode must be a number", 1, true) ~= nil).toBe(true)
            out:Close()
        else
            sanity.skip("Stdout is not a TTY in this environment.")
        end
    end)

    it("rejects Read on a closed stream", function()
        if tty.IsTTY(0) then
            local inp = tty.TtyStream:Create(0, true)
            inp:Close()
            local ok, err = pcall(function()
                inp:Read()
            end)
            expect(ok).toBe(false)
            expect(tostring(err):find("stream is closed", 1, true) ~= nil).toBe(true)
        else
            sanity.skip("Stdin is not a TTY in this environment.")
        end
    end)
end)
