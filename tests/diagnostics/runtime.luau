local function causeCrash()
    local nilTable: any = nil
    print(nilTable.foo.bar)
end

causeCrash()
