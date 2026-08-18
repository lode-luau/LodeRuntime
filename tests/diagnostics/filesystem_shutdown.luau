local fs = require("../../modules/filesystem")

local root = "tests/diagnostics/tmp_filesystem_shutdown"
pcall(fs.Remove, root, true)
fs.CreateDirectory(root)
local filePath = root .. "/active.txt"
fs.WriteFile(filePath, "shutdown")

local file = fs.File:Open(filePath, "r")
local stream = fs.ReadStream:Create(filePath)
local watcher = fs.Watcher:Create(root)
stream:Start()
watcher:Start(false)

assert(file:IsOpen())
error("intentional shutdown with active filesystem resources")
