
return {
    luau = {
        languagemode = "nonstrict",
        lint = {
            ["*"] = true,
            LocalUnused = false
        },
        linterrors = true,
        typeerrors = true,
        globals = {"expect"},
        aliases = {
            stdio = "modules/stdio",
            filesystem = "modules/filesystem",
            task = "temp/task_module",
            signal = "temp/signal_module",
            path = "modules/path",
            sanity = "modules/sanity",
            toml = "modules/toml",
            http = "modules/http",
            json = "modules/json",
        }
    }
}