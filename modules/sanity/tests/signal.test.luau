
-- Coverage for the pure-Luau @signal module (Connect/Once/Wait/Fire).

local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local task = require("../../../modules/task")
local Signal = require("../../signal")

describe("signal module", function()
    it("exposes the documented API surface", function()
        local sig = Signal.new()
        expect(sig).toBeType("table")
        expect(type(sig.Connect)).toBe("function")
        expect(type(sig.Once)).toBe("function")
        expect(type(sig.Wait)).toBe("function")
        expect(type(sig.Fire)).toBe("function")
        expect(type(sig.DisconnectAll)).toBe("function")
        expect(type(sig.Destroy)).toBe("function")
        expect(type(sig.Public)).toBe("function")
    end)

    it("fires connected callbacks with arguments", function()
        local sig = Signal.new()
        local received = nil :: any
        sig:Connect(function(a: number, b: string)
            received = { a, b }
        end)
        sig:Fire(42, "hello")
        -- Fire dispatches asynchronously via task.spawn: give it a tick.
        task.wait()
        expect(received).toEqual({ 42, "hello" })
    end)

    it("disconnects a connection and stops delivering", function()
        local sig = Signal.new()
        local count = 0
        local conn = sig:Connect(function()
            count += 1
        end)
        sig:Fire()
        task.wait()
        expect(count).toBe(1)
        conn:Disconnect()
        sig:Fire()
        task.wait()
        expect(count).toBe(1)
        expect(conn.Connected).toBe(false)
    end)

    it("Once fires exactly once then disconnects", function()
        local sig = Signal.new()
        local count = 0
        sig:Once(function()
            count += 1
        end)
        sig:Fire()
        sig:Fire()
        task.wait()
        expect(count).toBe(1)
    end)

    it("Wait resumes with the fired arguments", function()
        local sig = Signal.new()
        local results
        local t = task.spawn(function()
            results = { sig:Wait() }
        end)
        sig:Fire("payload", 7)
        task.wait()
        task.cancel(t)
        expect(results).toEqual({ "payload", 7 })
    end)

    it("Wait returns on timeout without firing", function()
        local sig = Signal.new()
        local start = os.clock()
        local result = sig:Wait(0.05)
        local elapsed = os.clock() - start
        expect(result).toBe(nil)
        expect(elapsed).toBeGreaterThanOrEqual(0.04)
    end)

    it("DisconnectAll stops every connection", function()
        local sig = Signal.new()
        local hits = 0
        sig:Connect(function() hits += 1 end)
        sig:Connect(function() hits += 1 end)
        sig:DisconnectAll()
        sig:Fire()
        task.wait()
        expect(hits).toBe(0)
    end)

    it("Public exposes only Connect/Once/Wait", function()
        local sig = Signal.new()
        local pub = sig:Public()
        expect((pub :: any).Fire).toBe(nil)
        expect((pub :: any).DisconnectAll).toBe(nil)
        expect(type(pub.Connect)).toBe("function")
    end)
end)