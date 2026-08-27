return {
    name = "source_package",
    version = "1.0.0",
    implementation = {
        artifact = "source_package",
        required = true,
        targets = {
            build = { "windows/x64" },
            release = { "windows/x64" },
        },
    },
}
