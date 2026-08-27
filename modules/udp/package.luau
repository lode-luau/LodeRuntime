return {
    name = "udp",
    version = "0.1.0",
    description = "Lode UDP module (Async/LibUV)",
    author = "Lode Team",
    license = "MIT",
    implementation = {
        artifact = "udp",
        required = true,
        targets = {
            build = { "windows/x64" },
            release = { "windows/x64" },
        },
    },
}
