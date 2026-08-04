local fs = require("../modules/filesystem")
local path = fs.path
print("Resolve cwd:", path.resolve())
print("Resolve @self:", path.resolve("@self"))

local self = path.resolve("@self")
local testDir = path.join(self, "test_fs_dir")
local testFile = path.join(testDir, "test_file.txt")
local testContent = "Hello from Lode FS!"

-- Cleanup previous test
pcall(fs.rm, testDir, true)

local ok, _err = pcall(fs.mkdir, testDir)
assert(ok, "mkdir failed")
print("mkdir OK")

local ok_write, _err_write = pcall(fs.writeFile, testFile, testContent)
assert(ok_write, "writeFile failed")
print("writeFile OK")

-- test readToBuffer
local testBuf = buffer.create(100)
local ok_r2b, bytesRead = pcall(fs.readToBuffer, testFile, testBuf, 0)
assert(ok_r2b and bytesRead == #testContent, "readToBuffer failed")
assert(buffer.readstring(testBuf, 0, bytesRead) == testContent, "readToBuffer content mismatch")
print("readToBuffer OK")

-- test readToBuffer offset
local ok_r2bo, bytesReadOff = pcall(fs.readToBuffer, testFile, testBuf, 10)
assert(ok_r2bo and bytesReadOff == #testContent, "readToBuffer offset failed")
assert(buffer.readstring(testBuf, 10, bytesReadOff) == testContent, "readToBuffer offset content mismatch")
print("readToBuffer (offset) OK")

-- test appendFile
local appendText = " Append!"
local ok_app, _err_app = pcall(fs.appendFile, testFile, appendText)
assert(ok_app, "appendFile failed")
local newData = fs.readFile(testFile)
assert(newData == testContent .. appendText, "appendFile content mismatch")
print("appendFile OK")

-- test copyFile
local copyPath = path.join(testDir, "copy.txt")
local ok_cp, _err_cp = pcall(fs.copyFile, testFile, copyPath)
assert(ok_cp, "copyFile failed")
assert(fs.exists(copyPath), "copied file should exist")
print("copyFile OK")

-- test rename
local movePath = path.join(testDir, "moved.txt")
local ok_mv, _err_mv = pcall(fs.rename, copyPath, movePath)
assert(ok_mv, "rename failed")
assert(fs.exists(movePath), "renamed file should exist")
assert(not fs.exists(copyPath), "original copied file should not exist")
print("rename OK")

-- test realpath
local real, _err_rp = pcall(fs.realpath, movePath)
assert(real, "realpath failed")
print("Realpath of moved file:", fs.realpath(movePath))
print("realpath OK")

-- test stat timestamps
local ok_st, st = pcall(fs.stat, movePath)
assert(ok_st, "stat failed")
assert(st.mtime > 0, "stat mtime should exist")
assert(st.atime > 0, "stat atime should exist")
assert(st.ctime > 0, "stat ctime should exist")
print("stat timestamps OK")

-- test callbacks
print("\n--- Starting Callback Tests ---")
local cbDir = path.join(self, "test_cb_dir")
pcall(fs.rm, cbDir, true) -- cleanup just in case

fs.mkdirWithCallback(cbDir, function(success, err)
    assert(success, "mkdir cb failed: " .. tostring(err))
    print("mkdirWithCallback OK")
    
    local cbTestFile = path.join(cbDir, "cb_file.txt")
    fs.writeFileWithCallback(cbTestFile, "hello cb", function(success, err)
        assert(success, "writeFile cb failed")
        print("writeFileWithCallback OK")
        
        fs.existsWithCallback(cbTestFile, function(success, exists)
            assert(success and exists, "exists cb failed")
            print("existsWithCallback OK")
            
            fs.readFileWithCallback(cbTestFile, function(success, data)
                assert(success and data == "hello cb", "readFile cb failed")
                print("readFileWithCallback OK")
                
                fs.appendFileWithCallback(cbTestFile, " appended", function(success, err)
                    assert(success, "appendFile cb failed")
                    print("appendFileWithCallback OK")
                    
                    fs.readFileBufferWithCallback(cbTestFile, function(success, buf)
                        assert(success and typeof(buf) == "buffer", "readFileBuffer cb failed")
                        print("readFileBufferWithCallback OK")
                        
                        local readBuf = buffer.create(20)
                        fs.readToBufferWithCallback(cbTestFile, readBuf, function(success, bytesRead)
                            assert(success and type(bytesRead) == "number", "readToBuffer cb failed")
                            print("readToBufferWithCallback OK")
                            
                            local cbCopyFile = path.join(cbDir, "cb_copy.txt")
                            fs.copyFileWithCallback(cbTestFile, cbCopyFile, function(success, err)
                                assert(success, "copyFile cb failed")
                                print("copyFileWithCallback OK")
                                
                                local cbRenameFile = path.join(cbDir, "cb_rename.txt")
                                fs.renameWithCallback(cbCopyFile, cbRenameFile, function(success, err)
                                    assert(success, "rename cb failed")
                                    print("renameWithCallback OK")
                                    
                                    fs.statWithCallback(cbRenameFile, function(success, st)
                                        assert(success and st.size > 0, "stat cb failed")
                                        print("statWithCallback OK")
                                        
                                        fs.realpathWithCallback(cbRenameFile, function(success, rp)
                                            assert(success and type(rp) == "string", "realpath cb failed")
                                            print("realpathWithCallback OK")
                                            
                                            fs.readDirWithCallback(cbDir, function(success, files)
                                                assert(success and #files > 0, "readDir cb failed")
                                                print("readDirWithCallback OK")
                                                
                                                fs.rmWithCallback(cbDir, function(success, err)
                                                    assert(success, "rm cb failed")
                                                    print("rmWithCallback OK")
                                                    print("\nTestes do FS concluidos com sucesso (incluindo TODOS os callbacks)!")
                                                end, true)
                                            end)
                                        end)
                                    end)
                                end)
                            end)
                        end, 0)
                    end)
                end)
            end)
        end)
    end)
end)
