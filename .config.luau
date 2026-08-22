
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
            task = "modules/task",
            signal = "modules/signal",
            path = "modules/path",
            sanity = "modules/sanity",
            toml = "modules/toml",
            http = "modules/http",
            json = "modules/json",
            socket = "modules/tcp",
            websocket = "modules/websocket",
            tcp = "modules/tcp",
            udp = "modules/udp",
            sys = "modules/sys",
            tty = "modules/tty",
            pipe = "modules/pipe",
            crypto = "modules/crypto",
        }
    }
}
