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
end)