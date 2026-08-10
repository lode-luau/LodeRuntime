local tty = require("@tty")
local sys = require("@sys")

print("Is stdin a TTY?", tty.IsTTY(0), "(Actual Handle Type:", sys.GuessHandleType(0) .. ")")
print("Is stdout a TTY?", tty.IsTTY(1), "(Actual Handle Type:", sys.GuessHandleType(1) .. ")")

if tty.IsTTY(1) then
    local out = tty.Create(1, false)
    out:WriteLine("========================================")
    out:WriteLine(" Hello from native TTY via LodeRuntime! ")
    out:WriteLine("========================================")
    
    local cols, rows = out:GetWindowSize()
    out:WriteLine("Current Window Size: " .. tostring(cols) .. " columns, " .. tostring(rows) .. " rows.")
else
    print("Cannot test WriteLine because stdout is not a TTY.")
end
