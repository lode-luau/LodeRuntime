-- Sibling module located in temp/, one level above native_module/.
-- Loaded via require("./sibling_module") from inside the C++ native module.
-- This tests that ./X from a native module resolves relative to the PARENT
-- of the package folder, exactly like require("./X") from an init.luau.

return {
    name = "sibling_module",
    message = "Loaded successfully from temp/ via './sibling_module'",
    answer = 42,
}
