--!strict

local http = require("@http")
local _task = require("@task")

-- ============================================================================
-- 1. USO DO FETCH (Requisição direta via HTTP e HTTPS)
-- ============================================================================
print("--- Testando fetch ---")

local response = http.fetch("https://jsonplaceholder.typicode.com/posts/1", {
    method = "GET",
    headers = {
        ["Accept"] = "application/json",
        ["User-Agent"] = "LodeRuntime/1.0",
    },
    timeout = 5000,
})

if response.status == http.StatusCodes.Ok then
    print(`Sucesso [{response.status}] URL: {response.url}`)
    
    -- Utilizando a abstração de body
    local data = response.body:AsTable()
    print(`ID do Post: {data.id}`)
    print(`Título: {data.title}`)
else
    print(`Falha na requisição: {response.status} {response.statusText}`)
end

-- ============================================================================
-- 2. USO DO HTTPCLIENT (Reuso de conexões / Chamadas Async via Signals)
-- ============================================================================
print("\n--- Testando HttpClient ---")

local client = http.HttpClient:Create()

-- Escutando respostas e erros assíncronos via Signals
client.ResponseReceived:Connect(function(res)
    print(`[Async Response] Status: {res.status}`)
    print(`[Async Body]: {res.body:AsJson(true)}`)
    client:Close()
end)

client.ErrorOccurred:Connect(function(err)
    print(`[Async Error]: {err}`)
    client:Close()
end)

-- Requisição síncrona/yielding reutilizando o cliente
local syncRes = client:Request("https://httpbin.org/post", {
    method = "POST",
    headers = { ["Content-Type"] = "application/json" },
    body = '{"foo": "bar"}',
})
print(`[Client Sync] Status POST: {syncRes.status}`)

-- Requisição assíncrona (não-bloqueante)
client:RequestAsync("https://httpbin.org/get")

-- ============================================================================
-- 3. USO DO HTTPSERVER (Servidor de rotas via Signals)
-- ============================================================================
print("\n--- Iniciando HttpServer ---")

local server = http.Server:Create()

server.RequestReceived:Connect(function(req, res)
    print(`[Server] Requisição recebida na rota: {req.url or "/"}`)
    
    -- Exemplo simples de resposta
    if res then
        res:Send({
            status = http.StatusCodes.Ok,
            body = '{"message": "Hello from LodeRuntime!"}',
        })
    end
end)

server.ErrorOccurred:Connect(function(err)
    print(`[Server Error]: {err}`)
end)
-- Inicia a escuta na porta 8080
server:Listen(8080, "127.0.0.1")

if server:IsListening() then
    local addr = server:LocalAddress()
    print(`Servidor HTTP rodando em http://{addr.host}:{addr.port}`)
end