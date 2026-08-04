local path = require("../modules/path")

print("Resolve cwd:", path.resolve())
print("Resolve ./foo:", path.resolve("./foo"))
print("Resolve @self:", path.resolve("@self"))
print("Resolve @self/bar:", path.resolve("@self/bar"))

print("Join:", path.join("a", "b", "c"))
print("Basename:", path.basename("c:/Users/yanlvl99/test.txt"))
print("Dirname:", path.dirname("c:/Users/yanlvl99/test.txt"))
print("Extname:", path.extname("c:/Users/yanlvl99/test.txt"))
