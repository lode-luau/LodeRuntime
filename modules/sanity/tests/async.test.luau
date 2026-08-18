--!strict
--!native

-- Self-tests for async test support (yielding tests).

local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local task = require("../../../modules/task")
local fs = require("../../filesystem")
local path = require("../../filesystem/path")

describe("async tests", function()
    it("task.wait yields and resumes", function()
        local start = os.clock()
        task.wait(0.1)
        local elapsed = os.clock() - start
        expect(elapsed).toBeGreaterThanOrEqual(0.08)
    end)

    it("async filesystem write/read", function()

        local selfDir = path.resolve("@self")
        local file = path.join(selfDir, "async_tmp.txt")

        pcall(fs.RemoveFile, file)
        fs.WriteFile(file, "async data")
        local data = fs.ReadFile(file)
        expect(data).toBe("async data")
        pcall(fs.RemoveFile, file)
    end)

    it("task.spawn resumes an existing coroutine", function()
        local ran = false
        local co = coroutine.create(function()
            ran = true
        end)
        task.spawn(co)
        expect(ran).toBe(true)
    end)

    it("task.setTimeout fires the callback and returns a valid timer id", function()
        local fired = false
        local id = task.setTimeout(function()
            fired = true
        end, 50)
        expect(typeof(id)).toBe("number")
        expect(id > 0).toBe(true)
        task.wait(0.1)
        expect(fired).toBe(true)
    end)

    it("task.setInterval fires the callback repeatedly", function()
        local count = 0
        local id = task.setInterval(function()
            count += 1
        end, 20)
        task.wait(0.1)
        task.clearInterval(id)
        expect(count >= 2).toBe(true)
    end)

    it("task.spawn returns a running thread and passes arguments", function()
        local result = ""
        local t = task.spawn(function(a, b)
            result = a .. b
        end, "foo", "bar")
        expect(typeof(t)).toBe("thread")
        expect(result).toBe("foobar")
    end)

    it("task.defer returns a thread and executes on next tick", function()
        local executed = false
        local t = task.defer(function(arg)
            expect(arg).toBe(123)
            executed = true
        end, 123)
        expect(typeof(t)).toBe("thread")
        expect(executed).toBe(false)
        task.wait(0.05)
        expect(executed).toBe(true)
    end)

    it("task.delay returns a thread and executes after duration", function()
        local executed = false
        local t = task.delay(0.05, function(msg)
            expect(msg).toBe("hello")
            executed = true
        end, "hello")
        expect(typeof(t)).toBe("thread")
        expect(executed).toBe(false)
        task.wait(0.1)
        expect(executed).toBe(true)
    end)

    it("task.cancel cancels a thread scheduled with task.delay", function()
        local executed = false
        local t = task.delay(0.05, function()
            executed = true
        end)
        expect(typeof(t)).toBe("thread")
        task.cancel(t)
        task.wait(0.1)
        expect(executed).toBe(false)
    end)

    it("task.cancel cancels a coroutine waiting on a timer", function()
        local ran = false
        local co = coroutine.create(function()
            task.wait(0.1)
            ran = true
        end)
        task.spawn(co)
        task.cancel(co)
        task.wait(0.15)
        expect(ran).toBe(false)
    end)
end)
