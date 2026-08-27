return {
    name = "websocket",
    version = "0.1.0",
    description = "Lode WebSocket module (RFC 6455 / Async/LibUV)",
    author = "Lode Team",
    license = "MIT",
    implementation = {
        artifact = "websocket",
        required = true,
        targets = {
            build = { "windows/x64" },
            release = { "windows/x64" },
        },
    },
}
