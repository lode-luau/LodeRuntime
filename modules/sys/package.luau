return {
    name = "sys",
    version = "1.0.0",
    description = "Lode System API",
    author = "Lode Team",
    license = "MIT",
    implementation = {
        artifact = "sys",
        required = true,
        targets = {
            build = { "windows/x64" },
            release = { "windows/x64" },
        },
    },
}
