local unusedVar = 42

local function compute()
    print("Working...")
    return 100
    print("This code is unreachable!")
end

compute()
