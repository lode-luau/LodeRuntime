return {
    name = "http",
    version = "0.1.0",
    description = "Lode HTTP client module (Async/LibUV)",
    author = "Lode Team",
    license = "MIT",
    implementation = {
        artifact = "http",
        required = true,
        targets = {
            build = { "windows/x64" },
            release = { "windows/x64" },
        },
    },
}
