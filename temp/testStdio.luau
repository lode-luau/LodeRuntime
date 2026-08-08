local stdio = require("../modules/stdio")
local task = require("../modules/task")

local function sleep(s: number)
    task.wait(s)
end

stdio.clear()
stdio.print("========================================================")
stdio.print("             LODE STDIO MODULE TEST SUITE               ")
stdio.print("========================================================")
stdio.print("")

-- ========================================================
-- 1. Output Tests
-- ========================================================
stdio.print("[1] Output and Formatting")
sleep(0.5)
stdio.print("    -> Print (stdout):", 42, true, "Lode Engine")
stdio.eprint("    -> Error (stderr):", "This is an error stream test")
stdio.write("    -> Write (Raw): ")
stdio.write("Part 1, ")
stdio.write("Part 2.\n")
stdio.print("")
sleep(0.5)

-- ========================================================
-- 2. Concurrency Test (Non-Blocking I/O)
-- ========================================================
stdio.print("[2] Asynchronous Non-Blocking Input")
sleep(0.5)
stdio.print("    A background task will now start counting every second.")
stdio.print("    Take your time to answer; notice how the engine DOES NOT freeze!")
stdio.print("")

local workerRunning = true
task.spawn(function()
    local counter = 1
    while workerRunning do
        task.wait(1)
        if workerRunning then
            stdio.print("    [Background Worker] " .. counter .. " second(s) elapsed...")
            counter += 1
        end
    end
end)

local name = stdio.prompt("    > Please enter your name: ")
workerRunning = false -- Avisa o background worker para parar

stdio.print("\n    Awesome! Welcome to the new async I/O, " .. tostring(name) .. "!")
stdio.print("")
sleep(1)

-- ========================================================
-- 3. Raw readLine
-- ========================================================
stdio.print("[3] Standard Input (readLine)")
sleep(0.5)
stdio.print("    > How are you feeling today?")
stdio.write("    > ")
local answer = stdio.stdin:readLine()
stdio.print("    Good to know you're feeling: '" .. tostring(answer) .. "'")
stdio.print("")
sleep(1)

-- ========================================================
-- 4. Buffer Test (Zero-Copy)
-- ========================================================
stdio.print("[4] Zero-Copy Buffer Read (readInto)")
sleep(0.5)

local BUF_SIZE = 16
local buf = buffer.create(BUF_SIZE)
buffer.fill(buf, 0, 0) -- Offset 0, Value 0 (Limpa a memória)

stdio.print("    > Type exactly 5 characters (e.g., '12345'): ")
stdio.write("    > ")

-- Lê exatamente 5 bytes direto no endereço de memória do buffer
local bytesRead = stdio.stdin:readInto(buf, 0, 5)

-- IMPORTANTE: Como o usuário digitou os 5 caracteres e apertou [ENTER], 
-- a quebra de linha (\n) ainda ficou presa na fila do LibUV/C++.
-- Chamamos readLine() uma vez para "descartar" o restante da linha até o \n
-- e limpar o buffer para os próximos usos!
stdio.stdin:readLine()

stdio.print("    Bytes read directly to C++ memory: " .. tostring(bytesRead))
stdio.print("    Extracted String from Buffer: '" .. buffer.tostring(buf) .. "'")
stdio.print("")

-- ========================================================
-- 5. TTY Features & Interactive Menu
-- ========================================================
stdio.print("[5] Advanced TTY & Raw Mode Selection")
sleep(0.5)

if stdio.stdout:isTTY() and stdio.stdin:isTTY() then
    local size = stdio.stdout:getWindowSize()
    local w, h = size[1], size[2]
    stdio.print("    [Info] TTY detected! Terminal dimensions: " .. w .. "x" .. h)
    stdio.print("    Testing interactive raw-mode menu. Try using UP/DOWN arrows.")
    stdio.print("")
    
    local options = { "Apple", "Banana", "Cherry", "Date", "Elderberry" }
    local idx, choice = stdio.select("    > Which fruit do you prefer?", options)
    
    stdio.print("    Excellent! You chose option " .. tostring(idx) .. ": '" .. choice .. "'")
else
    stdio.print("    [Warning] Tests are not running in a TTY terminal. Skipping interactive menu.")
end
stdio.print("")

-- ========================================================
-- Finalization
-- ========================================================
sleep(0.5)
stdio.print("========================================================")
stdio.print("               ALL TESTS COMPLETED!                     ")
stdio.print("========================================================")
