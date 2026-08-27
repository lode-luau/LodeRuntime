return {
    name = "tty",
    version = "0.1.0",
    description = "Lode TTY module (Terminal Streams, Async/LibUV)",
    author = "Lode Team",
    license = "MIT",
    implementation = {
        artifact = "tty",
        required = true,
        targets = {
            build = { "windows/x64", "linux/x64", "macos/x64", "macos/arm64" },
            release = { "windows/x64", "linux/x64", "macos/x64", "macos/arm64" },
        },
    },
}
