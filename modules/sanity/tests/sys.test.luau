
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
            
            expect(sys.GetEnv(TEST_KEY)).toBe(nil)
            
            sys.SetEnv(TEST_KEY, "HelloSys")
            expect(sys.GetEnv(TEST_KEY)).toBe("HelloSys")
        end)

        it("gets cwd", function()
            local cwd = sys.GetCwd()
            expect(type(cwd)).toBe("string")
            expect(string.len(cwd) > 0).toBe(true)
        end)

        it("returns command-line arguments as an array", function()
            local args = sys.Args()
            expect(type(args)).toBe("table")
            local args2 = sys.GetArgs()
            expect(type(args2)).toBe("table")
        end)

        it("throws error for Chdir to invalid path", function()
            local ok, err = pcall(function()
                sys.Chdir("/path/that/definitely/does/not/exist/lode123")
            end)
            expect(ok).toBe(false)
            expect(string.find(tostring(err), "sys.Chdir error")).toBeTruthy()
        end)

        it("guesses handle type for standard fds", function()
            local t = sys.GuessHandleType(0)
            expect(type(t)).toBe("string")
            local valid = (t == "tty" or t == "file" or t == "pipe" or t == "unknown")
            expect(valid).toBe(true)
        end)
    end)

    describe("Process Spawning", function()
        it("exposes spawn function", function()
            expect(typeof(sys.spawn)).toBe("function")
            expect(typeof(sys.Spawn)).toBe("function")
        end)

        it("rejects invalid spawn arguments", function()
            local ok, err = pcall(function()
                (sys.spawn :: any)(123)
            end)
            expect(ok).toBe(false)
            expect(tostring(err):find("sys.spawn", 1, true) ~= nil).toBe(true)
        end)

        it("spawns a child process and reads stdout via pipe", function()
            local isWin = string.find(string.lower(sys.GetPlatform()), "win", 1, true) ~= nil
            local cmd = isWin and "cmd.exe" or "/bin/echo"
            local args = isWin and { "/c", "echo", "LodeChildProcessWorks" } or { "LodeChildProcessWorks" }

            local child = sys.spawn(cmd, {
                args = args,
                stdio = { "pipe", "pipe", "pipe" }
            })

            expect(typeof(child)).toBe("userdata")
            expect(child.pid > 0).toBe(true)
            expect(typeof(child.stdout)).toBe("userdata")

            local line = child.stdout:ReadLine()
            expect(type(line)).toBe("string")
            expect(tostring(line):find("LodeChildProcessWorks", 1, true) ~= nil).toBe(true)

            local exitCode = child:Wait()
            expect(exitCode).toBe(0)
            expect(child:GetExitCode()).toBe(0)
            expect(child:IsRunning()).toBe(false)
        end)

        it("delivers child exit code on non-zero exit", function()
            local isWin = string.find(string.lower(sys.GetPlatform()), "win", 1, true) ~= nil
            local cmd = isWin and "cmd.exe" or "/bin/sh"
            local args = isWin and { "/c", "exit", "42" } or { "-c", "exit 42" }

            local child = sys.spawn(cmd, {
                args = args,
                stdio = { "ignore", "ignore", "ignore" }
            })

            expect(child.pid > 0).toBe(true)
            local exitCode = child:Wait()
            expect(exitCode).toBe(42)
            expect(child:GetExitCode()).toBe(42)
        end)
    end)
end)

describe("sys process identity and env completion", function()
    it("returns coherent pid/ppid/execPath", function()
        local pid = sys.GetPid()
        local ppid = sys.GetPpid()
        expect(pid).toBeType("number")
        expect(ppid).toBeType("number")
        expect(sys.GetExecPath()).toBeType("string")
        expect(#sys.GetExecPath() > 0).toBe(true)
    end)

    it("GetMonotonicMs never goes backwards", function()
        local a = sys.GetMonotonicMs()
        local b = sys.GetMonotonicMs()
        expect(b >= a).toBe(true)
    end)

    it("UnsetEnv removes variables set by SetEnv", function()
        sys.SetEnv("LODE_SYS_TEST", "x")
        expect(sys.GetEnv("LODE_SYS_TEST")).toBe("x")
        sys.UnsetEnv("LODE_SYS_TEST")
        expect(sys.GetEnv("LODE_SYS_TEST")).toBe(nil)
        -- unset of missing variable must not raise
        sys.UnsetEnv("LODE_SYS_TEST_NEVER_SET")
    end)

    it("EnvEntries lists environment variables including ones we set", function()
        sys.SetEnv("LODE_ENV_PROBE", "present")
        local entries = sys.EnvEntries()
        expect(entries.LODE_ENV_PROBE).toBe("present")
    end)
end)
