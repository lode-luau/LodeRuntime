
-- Filesystem module tests. All file/dir operations are confined to a temp
-- directory resolved relative to this package via path.resolve("@self"), so
-- nothing outside the sanity package is touched. Every created file/dir is
-- removed afterward with fs.RemoveFile / fs.RemoveDirectory.

local sanity = require("../init")
local describe = sanity.describe
local beforeEach = sanity.beforeEach
local afterEach = sanity.afterEach
local it = sanity.it
local expect = sanity.expect
local skip = sanity.skip

local fs = require("../../filesystem")
local path = require("../../filesystem/path")
local task = require("../../../modules/task")

-- Temp workspace rooted inside the sanity package (no external pollution).
local tmpRoot = path.resolve("@self", "tmp_fs_test")

local function norm(s: string): string
    return string.gsub(s, "\\", "/")
end

local function cleanup()
    -- Remove the entire per-file test workspace so every test is isolated and
    -- newly added fixtures are cleaned automatically without updating a list.
    pcall(function()
        fs.RemoveDirectory(tmpRoot, true)
    end)
end

describe("fs", function()
    beforeEach(cleanup)
    afterEach(cleanup)

    it("exposes the expected API surface", function()
        fs.CreateDirectory(tmpRoot)
        expect(typeof(fs.ReadFile)).toBe("function")
        expect(typeof(fs.WriteFile)).toBe("function")
        expect(typeof(fs.AppendFile)).toBe("function")
        expect(typeof(fs.CopyFile)).toBe("function")
        expect(typeof(fs.Rename)).toBe("function")
        expect(typeof(fs.Exists)).toBe("function")
        expect(typeof(fs.CreateDirectory)).toBe("function")
        expect(typeof(fs.ReadDirectory)).toBe("function")
        expect(typeof(fs.Glob)).toBe("function")
        expect(typeof(fs.Stat)).toBe("function")
        expect(typeof(fs.RealPath)).toBe("function")
        expect(typeof(fs.RemoveFile)).toBe("function")
        expect(typeof(fs.RemoveDirectory)).toBe("function")
        expect(typeof(fs.File.Open)).toBe("function")
        local surface = fs.File:Open(path.join(tmpRoot, "surface.txt"), "w+")
        expect(typeof(surface.Seek)).toBe("function")
        surface:Close()
        expect(typeof(fs.ReadStream.Create)).toBe("function")
        expect(typeof(fs.Watcher.Create)).toBe("function")
    end)

    it("writes and reads a file round-trip", function()
        fs.CreateDirectory(tmpRoot)
        local filePath = path.join(tmpRoot, "write.txt")
        fs.WriteFile(filePath, "hello fs")
        local contents = fs.ReadFile(filePath)
        expect(contents).toBe("hello fs")
        expect(fs.Exists(filePath)).toBe(true)
    end)

    it("appends to a file", function()
        fs.CreateDirectory(tmpRoot)
        local filePath = path.join(tmpRoot, "append.txt")
        fs.WriteFile(filePath, "a")
        fs.AppendFile(filePath, "b")
        fs.AppendFile(filePath, "c")
        expect(fs.ReadFile(filePath)).toBe("abc")
    end)

    it("reads a file into a buffer", function()
        fs.CreateDirectory(tmpRoot)
        local filePath = path.join(tmpRoot, "buffer.txt")
        fs.WriteFile(filePath, "0123456789")
        local buf = fs.ReadFileBuffer(filePath)
        expect(buffer.len(buf)).toBe(10)
        local out = buffer.create(4)
        local n = fs.ReadToBuffer(filePath, out, 0)
        expect(n).toBe(4)
        expect(buffer.tostring(out)).toBe("0123")
    end)

    it("copies a file", function()
        fs.CreateDirectory(tmpRoot)
        local src = path.join(tmpRoot, "copy_src.txt")
        local dst = path.join(tmpRoot, "copy_dst.txt")
        fs.WriteFile(src, "copy me")
        fs.CopyFile(src, dst)
        expect(fs.ReadFile(dst)).toBe("copy me")
    end)

    it("copies a directory tree", function()
        local source = path.join(tmpRoot, "copy_src_dir")
        local nested = path.join(source, "nested")
        local destination = path.join(tmpRoot, "copy_dst_dir")
        fs.CreateDirectory(nested)
        fs.WriteFile(path.join(source, "root.txt"), "root")
        fs.WriteFile(path.join(nested, "deep.txt"), "deep")
        fs.CopyFile(source, destination)
        expect(fs.ReadFile(path.join(destination, "root.txt"))).toBe("root")
        expect(fs.ReadFile(path.join(destination, "nested", "deep.txt"))).toBe("deep")
    end)

    it("renames a file", function()
        fs.CreateDirectory(tmpRoot)
        local src = path.join(tmpRoot, "rename_src.txt")
        local dst = path.join(tmpRoot, "rename_dst.txt")
        fs.WriteFile(src, "rename me")
        fs.Rename(src, dst)
        expect(fs.Exists(src)).toBe(false)
        expect(fs.ReadFile(dst)).toBe("rename me")
    end)

    it("moves a non-empty directory", function()
        local source = path.join(tmpRoot, "move_src_dir")
        local destination = path.join(tmpRoot, "move_dst_dir")
        fs.CreateDirectory(source)
        fs.WriteFile(path.join(source, "child.txt"), "child")
        fs.Rename(source, destination)
        expect(fs.Exists(source)).toBe(false)
        expect(fs.ReadFile(path.join(destination, "child.txt"))).toBe("child")
    end)

    it("creates and reads a directory", function()
        fs.CreateDirectory(tmpRoot)
        local sub = path.join(tmpRoot, "subdir")
        fs.CreateDirectory(sub)
        fs.WriteFile(path.join(sub, "inner.txt"), "inner")
        local entries = fs.ReadDirectory(sub)
        local found = false
        for _, e in ipairs(entries) do
            if e == "inner.txt" then found = true end
        end
        expect(found).toBe(true)
    end)

    it("matches files with glob patterns", function()
        local nested = path.join(tmpRoot, "subdir", "nested")
        fs.CreateDirectory(nested)
        fs.WriteFile(path.join(tmpRoot, "root.lua"), "root")
        fs.WriteFile(path.join(tmpRoot, "root.txt"), "text")
        fs.WriteFile(path.join(nested, "deep.lua"), "deep")

        local direct = fs.Glob(path.join(tmpRoot, "*.lua"))
        expect(#direct).toBe(1)
        expect(norm(direct[1])).toBe("root.lua")

        local singleChar = fs.Glob(path.join(tmpRoot, "r?ot.lua"))
        expect(#singleChar).toBe(1)
        expect(norm(singleChar[1])).toBe("root.lua")

        local recursive = fs.Glob(path.join(tmpRoot, "**", "*.lua"))
        expect(#recursive).toBe(2)
        local foundDeep = false
        for _, entry in ipairs(recursive) do
            if norm(entry) == "subdir/nested/deep.lua" then
                foundDeep = true
            end
        end
        expect(foundDeep).toBe(true)
    end)

    it("returns file stats", function()
        fs.CreateDirectory(tmpRoot)
        local filePath = path.join(tmpRoot, "write.txt")
        fs.WriteFile(filePath, "12345")
        local st = fs.Stat(filePath)
        expect(st.isFile).toBe(true)
        expect(st.isDirectory).toBe(false)
        expect(st.size).toBe(5)
    end)

    it("supports symlinks and permission updates", function()
        fs.CreateDirectory(tmpRoot)
        local target = path.join(tmpRoot, "target.txt")
        local link = path.join(tmpRoot, "target-link.txt")
        fs.WriteFile(target, "linked")
        fs.SetPermissions(target, 420)
        expect(type(fs.Stat(target).mode)).toBe("number")

        local ok, err = pcall(function()
            fs.CreateSymlink(target, link)
        end)
        if not ok then
                skip("Symbolic links are unavailable: " .. tostring(err))
            return
        end

        local stat = fs.Stat(link)
        expect(stat.isSymbolicLink).toBe(true)
        expect(norm(fs.ReadLink(link))).toBe(norm(target))
        expect(fs.RealPath(link)).toBe(fs.RealPath(target))
    end)

    it("resolves a real path", function()
        fs.CreateDirectory(tmpRoot)
        local filePath = path.join(tmpRoot, "write.txt")
        fs.WriteFile(filePath, "x")
        local real = fs.RealPath(filePath)
        expect(norm(real):find("write.txt", 1, true) ~= nil).toBe(true)
    end)

    it("reports existence correctly", function()
        fs.CreateDirectory(tmpRoot)
        local filePath = path.join(tmpRoot, "write.txt")
        expect(fs.Exists(filePath)).toBe(false)
        fs.WriteFile(filePath, "x")
        expect(fs.Exists(filePath)).toBe(true)
    end)

    it("opens a File handle and reads/writes", function()
        fs.CreateDirectory(tmpRoot)
        local filePath = path.join(tmpRoot, "handle.txt")
        local f = fs.File:Open(filePath, "w+")
        expect(f:IsOpen()).toBe(true)
        f:Write("handle data")
        f:Close()
        expect(f:IsOpen()).toBe(false)

        local f2 = fs.File:Open(filePath, "r")
        local data = f2:Read(100)
        expect(data).toBe("handle data")
        local st = f2:Stat()
        expect(st.size).toBe(11)
        f2:Close()
    end)

    it("seeks a File handle and preserves the descriptor cursor", function()
        fs.CreateDirectory(tmpRoot)
        local filePath = path.join(tmpRoot, "handle.txt")
        fs.WriteFile(filePath, "0123456789")
        local file = fs.File:Open(filePath, "r+")

        expect(tostring(file:Seek(0))).toBe("0")
        expect(file:Read(3)).toBe("012")
        expect(tostring(file:Seek(2, "cur"))).toBe("5")
        expect(file:Read(2)).toBe("56")
        expect(tostring(file:Seek(-2, "end"))).toBe("8")
        expect(file:Read(2)).toBe("89")
        expect(tostring(file:Seek(4, "set"))).toBe("4")
        expect(file:Read(2)).toBe("45")

        local ok, err = pcall(function()
            file:Seek(-1, "set")
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("fs File:Seek", 1, true) ~= nil).toBe(true)

        for _, invalid in { 1.5, math.huge, -math.huge } do
            ok, err = pcall(function()
                file:Seek(invalid)
            end)
            expect(ok).toBe(false)
            expect(tostring(err):find("fs File:Seek", 1, true) ~= nil).toBe(true)
        end

        ok, err = pcall(function()
            file:Seek(0, "invalid")
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("fs File:Seek", 1, true) ~= nil).toBe(true)

        file:Close()
        ok, err = pcall(function()
            file:Seek(0)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("fs File:Seek", 1, true) ~= nil).toBe(true)
    end)

    it("validates Seek edge cases", function()
        fs.CreateDirectory(tmpRoot)
        local filePath = path.join(tmpRoot, "seek-edges.txt")
        fs.WriteFile(filePath, "0123456789")
        local file = fs.File:Open(filePath, "r+")

        expect(tostring(file:Seek(0, nil))).toBe("0")
        expect(tostring(file:Seek(2, "cur"))).toBe("2")
        expect(tostring(file:Seek(-2, "cur"))).toBe("0")
        expect(tostring(file:Seek(-2, "end"))).toBe("8")
        expect(file:Read(2)).toBe("89")

        local invalidArguments = {
            { "missing offset", function() (file.Seek :: any)(file) end },
            { "string offset", function() file:Seek("0" :: any) end },
            { "boolean whence", function() file:Seek(0, true :: any) end },
            { "fractional offset", function() file:Seek(1.5) end },
            { "positive infinity", function() file:Seek(math.huge) end },
            { "negative infinity", function() file:Seek(-math.huge) end },
            { "unknown whence", function() file:Seek(0, "invalid") end },
            { "integer overflow", function() file:Seek(9223372036854775808) end },
            { "negative result", function() file:Seek(-1, "set") end },
        }
        for _, case in ipairs(invalidArguments) do
            local ok, err = pcall(case[2])
            expect(ok).toBe(false)
            expect(tostring(err):find("fs File:Seek", 1, true) ~= nil).toBe(true)
        end

        file:Close()
        local ok, err = pcall(function()
            file:Seek(0)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("fs File:Seek", 1, true) ~= nil).toBe(true)
    end)

    it("completes concurrent Seek and read operations", function()
        fs.CreateDirectory(tmpRoot)
        local filePath = path.join(tmpRoot, "seek-concurrent.txt")
        fs.WriteFile(filePath, string.rep("a", 1024 * 1024))
        local file = fs.File:Open(filePath, "r+")
        local results = {}
        local completed = 0

        local function spawnOperation(name, operation)
            task.spawn(function()
                local ok, value = pcall(operation)
                results[name] = { ok, value }
                completed += 1
            end)
        end

        for round = 1, 20 do
            results = {}
            completed = 0
            spawnOperation("start", function() return file:Seek(0) end)
            spawnOperation("offset", function() return file:Seek(512) end)
            spawnOperation("read", function() return file:Read(16) end)
            while completed < 3 do
                task.wait(0.01)
            end
            expect(results.start[1]).toBe(true)
            expect(results.offset[1]).toBe(true)
            expect(results.read[1]).toBe(true)
            expect(tostring(results.start[2]) == "0" or tostring(results.start[2]) == "512").toBe(true)
            expect(tostring(results.offset[2]) == "0" or tostring(results.offset[2]) == "512").toBe(true)
        end

        file:Close()
    end)

    it("rejects Seek on append-mode handles", function()
        fs.CreateDirectory(tmpRoot)
        local filePath = path.join(tmpRoot, "append.txt")
        fs.WriteFile(filePath, "data")
        local file = fs.File:Open(filePath, "a+")
        local ok, err = pcall(function()
            file:Seek(0)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("append-mode", 1, true) ~= nil).toBe(true)
        file:Close()
    end)

    it("rejects invalid File read sizes before allocation", function()
        fs.CreateDirectory(tmpRoot)
        local filePath = path.join(tmpRoot, "read-size.txt")
        fs.WriteFile(filePath, "data")
        local file = fs.File:Open(filePath, "r")

        for _, invalid in { -1, 1.5, math.huge, 128 * 1024 * 1024 } do
            local ok, err = pcall(function()
                file:Read(invalid)
            end)
            expect(ok).toBe(false)
            expect(tostring(err):find("read size", 1, true) ~= nil).toBe(true)

            ok, err = pcall(function()
                file:ReadBuffer(invalid)
            end)
            expect(ok).toBe(false)
            expect(tostring(err):find("read size", 1, true) ~= nil).toBe(true)
        end

        file:Close()
    end)

    it("reads a file through a ReadStream", function()
        fs.CreateDirectory(tmpRoot)
        local filePath = path.join(tmpRoot, "stream.txt")
        fs.WriteFile(filePath, "stream contents")

        local stream = fs.ReadStream:Create(filePath)
        local received = ""
        local eof = false
        local conn = stream.DataReceived:Connect(function(data: buffer)
            received ..= buffer.tostring(data)
        end)
        local conn2 = stream.EndOfStream:Connect(function()
            eof = true
        end)
        stream:Start()
        -- Wait for the stream to drain (yields the test coroutine).
        while not eof do
            task.wait(0.01)
        end
        conn:Disconnect()
        conn2:Disconnect()
        stream:Close()
        expect(received).toBe("stream contents")
    end)

    it("rejects a non-string path in ReadFile", function()
        local ok, err = pcall(function()
            (fs.ReadFile :: any)(123)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("path", 1, true) ~= nil).toBe(true)
    end)

    it("rejects a non-string path in File.Open", function()
        local ok, err = pcall(function()
            (fs.File.Open :: any)(123)
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("path must be a string", 1, true) ~= nil).toBe(true)
    end)

    it("rejects an invalid mode in File.Open", function()
        fs.CreateDirectory(tmpRoot)
        local filePath = path.join(tmpRoot, "handle.txt")
        local ok, err = pcall(function()
            fs.File:Open(filePath, "bogus")
        end)
        expect(ok).toBe(false)
        expect(tostring(err):find("invalid mode", 1, true) ~= nil).toBe(true)
    end)

    it("changes file permissions with SetPermissions and chmod", function()
        fs.CreateDirectory(tmpRoot)
        local filePath = path.join(tmpRoot, "perm.txt")
        fs.WriteFile(filePath, "perm content")

        local stBefore = fs.Stat(filePath)
        expect(typeof(stBefore.mode)).toBe("number")
        expect(stBefore.mode > 0).toBe(true)

        fs.SetPermissions(filePath, 420) -- 0644
        local stAfter = fs.Stat(filePath)
        expect(stAfter.mode > 0).toBe(true)

        fs.chmod(filePath, 493) -- 0755
        local stChmod = fs.Stat(filePath)
        expect(stChmod.mode > 0).toBe(true)
    end)
end)
