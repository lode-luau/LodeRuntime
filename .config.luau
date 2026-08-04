
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
            signal = "temp/signal_module",
            path = "modules/path"
        }
    }
}