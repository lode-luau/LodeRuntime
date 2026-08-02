-- Module Nativo: Type definitions para LSP, autocomplete e documentação
-- O LodeRuntime carrega a DLL nativa diretamente via lode.json.

export type Vector3 = {
    length: (self: Vector3) -> number,
}

export type Vector3Statics = {
    new: (x: number, y: number, z: number) -> Vector3
}

export type NativeModule = {
    greet: (name: string?) -> string,
    square: (x: number) -> number,
    getSystemInfo: () -> { platform: string, arch: string },
    Vector3: Vector3Statics
}

export type ModuleMetaInfo = {
    moduleName: string,
    version: string
}

return {} :: NativeModule, {} :: ModuleMetaInfo
