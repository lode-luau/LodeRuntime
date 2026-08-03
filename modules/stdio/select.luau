return function(stdin, stdout)
    return function(prompt, options)
        if not stdin:isTTY() or not stdout:isTTY() then
            error("stdio.select requires a TTY terminal")
        end
        
        local index = 1
        local numOptions = #options
        if numOptions == 0 then return 0, "" end
        
        stdin:setRawMode(true)
        stdout:write("\x1B[?25l") -- hide cursor
        
        local function render()
            stdout:writeLine(prompt)
            for i, opt in ipairs(options) do
                if i == index then
                    stdout:writeLine("\x1B[36m> " .. opt .. "\x1B[0m")
                else
                    stdout:writeLine("  " .. opt)
                end
            end
        end
        
        local function clear()
            stdout:write("\x1B[" .. tostring(numOptions + 1) .. "A")
            stdout:write("\x1B[J")
        end
        
        render()
        
        while true do
            local b = stdin:read(1)
            if not b or b == "" then break end
            
            if b == "\x03" then -- Ctrl+C
                clear()
                stdout:write("\x1B[?25h")
                stdin:setRawMode(false)
                error("Interrupted")
            elseif b == "\r" or b == "\n" then
                break
            elseif b == "\x1B" then
                local seq1 = stdin:read(1)
                if seq1 == "[" then
                    local seq2 = stdin:read(1)
                    if seq2 == "A" then -- Up
                        if index > 1 then
                            index = index - 1
                            clear()
                            render()
                        end
                    elseif seq2 == "B" then -- Down
                        if index < numOptions then
                            index = index + 1
                            clear()
                            render()
                        end
                    end
                end
            end
        end
        
        stdout:write("\x1B[?25h") -- show cursor
        stdin:setRawMode(false)
        return index, options[index]
    end
end
