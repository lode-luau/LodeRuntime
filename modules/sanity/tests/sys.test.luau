--!strict
--!native

local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect

local sys = require("../../sys")

describe("sys", function()
    describe("System Info", function()
        it("returns platform", function()
            local p = sys.GetPlatform()
            expect(type(p)).toBe("string")
            expect(string.len(p) > 0).toBe(true)
        end)

        it("returns architecture", function()
            local a = sys.GetArchitecture()
            expect(type(a)).toBe("string")
            expect(string.len(a) > 0).toBe(true)
        end)

        it("returns release", function()
            local r = sys.GetRelease()
            expect(type(r)).toBe("string")
            expect(string.len(r) > 0).toBe(true)
        end)

        it("returns version", function()
            local v = sys.GetVersion()
            expect(type(v)).toBe("string")
            expect(string.len(v) > 0).toBe(true)
        end)

        it("returns hostname", function()
            local h = sys.GetHostname()
            expect(type(h)).toBe("string")
            expect(string.len(h) > 0).toBe(true)
        end)

        it("returns memory info", function()
            local total = sys.GetTotalMemory()
            local free = sys.GetFreeMemory()
            
            expect(type(total)).toBe("number")
            expect(type(free)).toBe("number")
            expect(total > 0).toBe(true)
            expect(free > 0).toBe(true)
            expect(total >= free).toBe(true)
        end)

        it("returns uptime", function()
            local up = sys.GetUptime()
            expect(type(up)).toBe("number")
            expect(up > 0).toBe(true)
        end)

        it("returns CPU and directory information", function()
            local parallelism = sys.GetAvailableParallelism()
            local count = sys.GetCpuCount()
            local cpus = sys.GetCpuInfo()
            local tmpDir = sys.GetTmpDir()
            local homeDir = sys.GetHomeDir()
            local load = sys.GetLoadAverage()

            expect(parallelism > 0).toBe(true)
            expect(count > 0).toBe(true)
            expect(#cpus).toBe(count)
            expect(type(cpus[1].model)).toBe("string")
            expect(#tmpDir > 0).toBe(true)
            expect(#homeDir > 0).toBe(true)
            expect(#load).toBe(3)
        end)

        it("registers operating system signals", function()
            local signal = sys.Signal("SIGINT")
            expect(typeof(signal)).toBe("table")
            expect(typeof(signal.Connect)).toBe("function")
            local ok, err = pcall(function()
                sys.Signal("SIG_NOT_SUPPORTED")
            end)
            expect(ok).toBe(false)
            expect(tostring(err):find("unsupported signal", 1, true) ~= nil).toBe(true)
        end)
    end)

    describe("Process Control", function()
        it("gets and sets environment variables", function()
            local TEST_KEY = "LODE_SYS_TEST_VAR"
            
            -- Deveria ser nulo por padrão
            expect(sys.GetEnv(TEST_KEY)).toBe(nil)
            
            -- Seta e depois pega
            sys.SetEnv(TEST_KEY, "HelloSys")
            expect(sys.GetEnv(TEST_KEY)).toBe("HelloSys")
        end)

        it("gets cwd", function()
            local cwd = sys.GetCwd()
            expect(type(cwd)).toBe("string")
            expect(string.len(cwd) > 0).toBe(true)
        end)

        it("throws error for Chdir to invalid path", function()
            local ok, err = pcall(function()
                sys.Chdir("/path/that/definitely/does/not/exist/lode123")
            end)
            expect(ok).toBe(false)
            expect(string.find(tostring(err), "sys.Chdir error")).toBeTruthy()
        end)

        it("guesses handle type for standard fds", function()
            -- FD 0 é tipicamente tty, pipe ou file. Se der erro, ele também jogará pcall ou retornará.
            local t = sys.GuessHandleType(0)
            expect(type(t)).toBe("string")
            local valid = (t == "tty" or t == "file" or t == "pipe" or t == "unknown")
            expect(valid).toBe(true)
        end)
    end)
end)
