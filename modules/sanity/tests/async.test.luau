--!strict
--!native

-- Self-tests for async test support (yielding tests).

local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local task = require("../../../temp/task_module")
local fs = require("../../filesystem")

describe("async tests", function()
    it("task.wait yields and resumes", function()
        local start = os.clock()
        task.wait(0.1)
        local elapsed = os.clock() - start
        expect(elapsed).toBeGreaterThanOrEqual(0.08)
    end)

    it("async filesystem write/read", function()
        local path = fs.path
        local selfDir = path.resolve("@self")
        local file = path.join(selfDir, "async_tmp.txt")

        pcall(fs.rm, file)
        fs.writeFile(file, "async data")
        local data = fs.readFile(file)
        expect(data).toBe("async data")
        pcall(fs.rm, file)
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
