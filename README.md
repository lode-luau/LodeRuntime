
# LodeRuntime

O **LodeRuntime** é o runtime de execução do ecossistema Lode. Ele carrega e executa bytecode Luau, resolve módulos puros por `init.luau`, carrega módulos nativos por `libraries`, e expõe uma API C++ segura para registrar funções, tables, classes e userdata no Luau [3][1][2].

Ele **não compila fonte**. A compilação fica em uma biblioteca separada, mantendo o runtime focado apenas em execução, carregamento e integração nativa [3].

***

## Objetivos

O projeto deve seguir estas regras:

- O runtime executa bytecode, nunca fonte.
- O módulo puro é um diretório com `init.luau`.
- O módulo nativo é identificado por `libraries` no `lode.json`.
- O `init.luau` continua existindo em módulos nativos para tooling e LSP.
- A API C++ deve esconder a stack manual do Luau sempre que possível.

Esse modelo respeita a semântica de módulo por diretório do Luau, onde um módulo pode ser um arquivo `.luau` ou um diretório contendo `init.luau` [2][1].

***

## Estrutura do repositório

```text
LodeRuntime/
├── CMakeLists.txt
├── README.md
│
├── include/
│   └── lode/
│       ├── state.hpp
│       ├── value.hpp
│       ├── table.hpp
│       ├── module.hpp
│       ├── coroutine.hpp
│       ├── result.hpp
│       └── error.hpp
│
├── src/
│   ├── state.cpp
│   ├── value.cpp
│   ├── table.cpp
│   ├── module.cpp
│   ├── coroutine.cpp
│   ├── error.cpp
│   ├── module_loader.cpp
│   ├── registry.cpp
│   ├── platform/
│   │   ├── windows_dll.cpp
│   │   ├── unix_dlopen.cpp
│   │   └── platform.hpp
│   └── runtime.cpp
│
└── third_party/
    └── luau/
```

### Separação por responsabilidade

- `include/lode/`: API pública.
- `src/`: implementação interna do runtime.
- `src/platform/`: abstração de carregamento dinâmico por sistema.
- `third_party/luau/`: dependência do Luau.

Essa divisão reduz acoplamento e facilita testes, especialmente no loader de módulos e no backend de plataforma.

***

## O que o runtime faz

### 1. Cria a VM
`lode::State` representa uma VM isolada. Ela deve criar e destruir a instância do Luau com RAII, configurando o ambiente necessário para execução [3].

### 2. Executa bytecode
O runtime recebe bytecode já compilado e o executa. A C API do Luau contempla carregamento de bytecode na VM, então o runtime deve se apoiar nisso em vez de implementar parser/compiler [3].

### 3. Resolve módulos
Quando o código chama `require`, o runtime deve:

- verificar cache;
- detectar se o módulo é nativo ou puro;
- se houver `libraries`, carregar a DLL/SO/dylib correspondente;
- se não houver `libraries`, carregar `init.luau` como módulo padrão;
- resolver dependências antes do módulo principal;
- evitar cachear carregamento falho.

### 4. Expõe bindings C++
Módulos nativos usam a API pública do Lode para registrar funções, tabelas, classes e objetos de forma segura e previsível.

***

## Modelo de módulo

O sistema deve suportar dois tipos de módulo.

### Módulo puro
Um módulo puro é um diretório com `init.luau`.

```text
modules/
└── utils/
    ├── lode.json
    └── init.luau
```

### Módulo nativo
Um módulo nativo é um diretório com `libraries` no `lode.json`.

```text
modules/
└── meu_modulo/
    ├── lode.json
    ├── init.luau
    └── libs/
        ├── windows/
        │   └── x64/
        │       └── meu_modulo.dll
        ├── linux/
        │   └── x64/
        │       └── meu_modulo.so
        └── macos/
            └── x64/
                └── meu_modulo.dylib
```

### Regra principal

- Se existir `libraries`, o runtime carrega a biblioteca nativa.
- Se não existir `libraries`, o runtime carrega `init.luau`.
- O `init.luau` de módulo nativo serve para LSP, autocomplete e documentação.

Isso é importante porque o modelo por diretório com `init.luau` existe na semântica de módulo do Luau [1][2].

***

## `lode.json`

### Módulo puro

```json
{
  "name": "utils",
  "version": "1.0.0",
  "description": "Utilitários gerais",
  "author": "Seu nome",
  "license": "MIT",
  "dependencies": {
    "mathx": "^1.0.0"
  }
}
```

### Módulo nativo

```json
{
  "name": "meu_modulo",
  "version": "1.0.0",
  "description": "Bindings nativos do runtime",
  "author": "Seu nome",
  "license": "MIT",
  "dependencies": {
    "utils": "^1.0.0"
  },
  "libraries": {
    "windows": {
      "x64": "libs/windows/x64/meu_modulo.dll"
    },
    "linux": {
      "x64": "libs/linux/x64/meu_modulo.so"
    },
    "macos": {
      "x64": "libs/macos/x64/meu_modulo.dylib"
    }
  }
}
```

### O que validar no loader

- `name` obrigatório.
- `version` obrigatório.
- `dependencies` opcional.
- `libraries` opcional.
- Se `libraries` existir, o módulo é nativo.
- Se `libraries` não existir, o módulo é puro e deve ter `init.luau`.

***

## Como o `require` deve funcionar

### Fluxo geral

1. O runtime recebe `require("@nome")`.
2. Busca no .luaurc ou .config.luau que sao arquivos de configuração do proprio luau ou relativo ao script ou absoluto.
3. Se já estiver carregado, retorna o valor cacheado.
4. Se não estiver, resolve o pacote.
5. Lê `lode.json`.
6. Se houver dependências, carrega primeiro.
7. Se houver `libraries`, carrega a biblioteca nativa.
8. Caso contrário, carrega `init.luau`.
9. Guarda no cache somente se o carregamento terminar com sucesso.

### Módulo puro

```lua
local Utils = require("@utils")
```

Neste caso o runtime deve ler o .luaurc ou o .config.luau que vai ter o path relativo para o modulo, carregar o bytecode correspondente e executar como módulo.

### Módulo nativo

```lua
local greet, Vector = require("@meu_modulo ou passando o path relativo")
```

Neste caso o runtime carrega diretamente a biblioteca definida em `libraries`, procura o símbolo de inicialização do módulo e coleta os exports da API C++.

***

## API pública

A API pública deve ser pequena e clara.

***

## `State`

`State` é a classe central do runtime.

### Responsabilidades
- criar a VM;
- destruir a VM;
- executar bytecode;
- resolver `require`;
- registrar globais;
- criar table, coroutine e userdata;
- expor chamadas protegidas.

### API sugerida

```cpp
namespace lode {

class State {
public:
    static Result<State> create();

    Result<void> execute(BytecodeView bytecode);
    Result<Value> protectedCall(BytecodeView bytecode);

    void addModulePath(std::string path);

    void setGlobal(std::string name, Value value);
    Result<Value> getGlobal(std::string name) const;

    Table createTable();
    Coroutine createCoroutine(FunctionRef fn);

    Result<Value> require(std::string_view name);
};

}
```

### Por que assim é melhor
- `Result<State>` deixa falha de criação explícita.
- `execute` evita ambiguidade com coroutine `resume`.
- `protectedCall` deixa claro que captura erro.
- `require` retorna valor com erro explícito.

***

## `Value`

`Value` é o tipo dinâmico usado na ponte C++/Luau.

### Tipos sugeridos
- `nil`
- `boolean`
- `number`
- `integer`
- `string`
- `table`
- `function`
- `thread`
- `userdata`
- `lightuserdata`

### API sugerida

```cpp
lode::Value v = 42;

if (v.isNumber()) {
    auto n = v.asNumber();
}

auto i = v.tryAs<int>();
```

### Recomendação
Use conversão explícita e segura. Evite `as<T>()` sem checagem, porque isso costuma virar fonte de bug silencioso.

***

## `Table`

`Table` é um wrapper RAII para tabelas do Luau.

### API sugerida

```cpp
auto t = vm.createTable();
t.set("x", 10);
t.set("name", "player");

auto x = t.get<int>("x");
auto name = t.get<std::string>("name");
```

### Recursos úteis
- `get`
- `set`
- `has`
- `remove`
- `rawGet`
- `rawSet`
- `setMetatable`
- `getMetatable`
- `freeze`
- iteração

### Observação
Como o Luau suporta mecanismos de tabela, metatable e operações raw, o wrapper deve refletir isso sem expor a stack ao usuário [3].

***

## `Module`

O sistema de módulo nativo deve ser bem simples.

### API sugerida

```cpp
LODE_MODULE(lode::State& vm, lode::Exports& export_) {
    export_.function("double", [](int x) {
        return x * 2;
    });

    auto meta = vm.createTable();
    meta.set("version", "1.0.0");
    export_.table("meta", meta);
}
```

### Vantagens dessa forma
- nomes explícitos;
- fácil de documentar;
- fácil de dar autocomplete;
- fácil de serializar em tooling;
- não depende de “múltiplos returns” confusos.

***

## `Coroutine`

Coroutines precisam ter contrato claro.

### API sugerida

```cpp
auto co = vm.createCoroutine([](lode::Coroutine& c) {
    c.yield(1);
    c.yield(2);
    return 3;
});
```

### O que documentar
- `resume()` retorna valor ou erro.
- `yield()` só funciona no contexto correto.
- `isFinished()` deve existir.
- coro com erro não deve ficar em estado indefinido.

***

## `Error` e `Result`

Essa parte deve ser uniforme em todo o projeto.

### Tipos sugeridos
- `Error`
- `RuntimeError`
- `ModuleError`
- `TypeError`
- `Result<T>`

### Uso

```cpp
auto result = vm.execute(bytecode);
if (!result) {
    std::cerr << result.error().message();
}
```

### Por que isso importa
A C API do Luau tem vários pontos de falha relacionados a carga, execução e ambiente; um tipo único de resultado torna o wrapper mais previsível [3].

***

## Loader de módulos

O loader é o coração do runtime.

### Ele deve fazer
- leitura de `lode.json`;
- resolução de pacote;
- distinção entre módulo puro e nativo;
- carregamento de binário por plataforma;
- carregamento de `init.luau` quando não houver `libraries`;
- resolução de dependências;
- cache;
- erro detalhado.

### Estados do registry
- `NotLoaded`
- `Loading`
- `Loaded`
- `Failed`

### Regras importantes
- dependência circular deve falhar claramente;
- módulo parcialmente carregado não deve ser cacheado como válido;
- o erro nativo da plataforma deve ser preservado;
- o loader deve ser thread-safe desde o início.

***

## Carregamento dinâmico

Esse código deve ficar isolado em `src/platform/`.

### API sugerida

```cpp
namespace lode::platform {

class DynamicLibrary {
public:
    static Result<DynamicLibrary> open(std::string_view path);
    Result<void*> symbol(std::string_view name) const;
    void close();
};

}
```

### Plataformas
- Windows: `LoadLibrary`, `GetProcAddress`, `FreeLibrary`
- Unix: `dlopen`, `dlsym`, `dlclose`

Isso é o caminho correto para DLL/SO/dylib cross-platform [4][5].

***

## Bytecode only

O runtime só trabalha com bytecode.

### Fluxo

```text
Fonte Luau
    ↓
Compiler separado
    ↓
Bytecode
    ↓
LodeRuntime
    ↓
execução
```

### Benefícios
- runtime menor;
- menos dependências;
- execução mais rápida;
- melhor separação entre tooling e engine.

A própria C API do Luau contempla o carregamento de bytecode, o que combina com esse desenho [3].

***

## LSP e `init.luau`

Essa decisão está boa e deve ser documentada com clareza.

### Regra
- módulo puro: `init.luau` é a entrada;
- módulo nativo: `libraries` manda na execução;
- `init.luau` em módulo nativo existe para LSP/autocomplete.

Esse comportamento é coerente com a ideia de diretório-módulo do Luau [1][2].

***

## Ordem de implementação

Eu sugiro esta sequência:

1. `Result` e `Error`.
2. `Value`.
3. `Table`.
4. `State`.
5. execução de bytecode.
6. registry.
7. loader de módulo puro via `init.luau`.
8. loader nativo via `libraries`.
9. `Exports`.
10. `Coroutine`.
11. userdata e classes.
12. testes e hot reload.

Essa ordem te dá base funcional cedo e evita construir loader antes da VM estar estável.

***

## Ajustes que eu faria no texto original

Eu corrigiria estes pontos imediatamente:

- remover `entry` do `lode.json`;
- deixar `init.luau` como entrada padrão do módulo puro;
- usar `libraries` como chave do módulo nativo;
- incluir macOS;
- definir cache, ciclo de dependência e erro;
- trocar API solta por `Result`/`Error`;
- separar API pública da implementação;
- tirar caracteres quebrados como `源代码`.

***

## Versão resumida da arquitetura

Se eu fosse deixar a ideia em uma frase:

**O LodeRuntime executa bytecode Luau, resolve módulos por diretório com `init.luau`, carrega nativos por `libraries`, e fornece uma API C++ segura e previsível para bindings.**

***

## Próximo passo prático

O melhor próximo passo é eu te devolver isso em um destes formatos:

1. **README.md final pronto para colar**
2. **headers completos com assinaturas de classes**
3. **plano de implementação em ordem de arquivos**
4. **especificação formal do `lode.json` e do loader**
# LodeRuntime
