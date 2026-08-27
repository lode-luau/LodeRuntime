return {
    name = "task",
    version = "1.0.0",
    description = "Lode Task scheduler native C++ module for Luau",
    author = "Lode Team",
    license = "MIT",
    implementation = {
        artifact = "task",
        required = true,
        targets = {
            build = { "windows/x64", "linux/x64", "macos/x64", "macos/arm64" },
            release = { "windows/x64", "linux/x64", "macos/x64", "macos/arm64" },
        },
    },
}
