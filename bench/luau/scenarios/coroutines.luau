--!strict
-- Scenario: coroutine spawn + resume throughput (the per-task path used by
-- task.spawn / timers). A fresh coroutine is created per iteration so the
-- measurement covers thread creation + first resume.
return function(bench: any, NativeMod: any)
    local function oneYield()
        coroutine.yield()
    end

    bench("coroutine_spawn_resume", function()
        local co = coroutine.create(oneYield)
        coroutine.resume(co)
    end)
end
