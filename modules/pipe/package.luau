return {
    name = "pipe",
    version = "0.1.0",
    description = "Lode Pipe module (Named Pipes / IPC, Async/LibUV)",
    author = "Lode Team",
    license = "MIT",
    implementation = {
        artifact = "pipe",
        required = true,
        targets = {
            build = { "windows/x64", "linux/x64", "macos/x64", "macos/arm64" },
            release = { "windows/x64", "linux/x64", "macos/x64", "macos/arm64" },
        },
    },
}
