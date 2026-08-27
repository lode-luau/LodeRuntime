return {
    name = "filesystem",
    version = "1.0.0",
    description = "Lode standard filesystem module",
    author = "Lode Team",
    license = "MIT",
    implementation = {
        artifact = "filesystem",
        required = true,
        targets = {
            build = { "windows/x64" },
            release = { "windows/x64" },
        },
    },
}
