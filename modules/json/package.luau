return {
    name = "json",
    version = "1.0.0",
    description = "Lode JSON parse and stringify native C++ module for Luau",
    author = "Lode Team",
    license = "MIT",
    implementation = {
        artifact = "json",
        required = true,
        targets = {
            build = { "windows/x64", "linux/x64", "macos/x64", "macos/arm64" },
            release = { "windows/x64", "linux/x64", "macos/x64", "macos/arm64" },
        },
    },
}
