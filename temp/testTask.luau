local task = require("./task_module")
local signal = require("./signal_module")

-- Helper para formatar e rodar testes de forma organizada
local function runTest(name: string, testFn: () -> ())
    task.spawn(function()
        print(string.format("🟢 [RODANDO]: %s", name))
        local startTime = os.clock()
        testFn()
        local elapsed = os.clock() - startTime
        print(string.format("✅ [PASSOU]: %s (Tempo total: %.4fs)\n", name, elapsed))
    end)
end

print("==================================================")
print("🚀 INICIANDO BATERIA DE TESTES DO LODE RUNTIME")
print("==================================================\n")

--------------------------------------------------------------------------------
-- TESTE 1: Precisão do task.wait
--------------------------------------------------------------------------------
runTest("Task: Precisão do task.wait()", function()
    local delayTarget = 0.5 -- 500ms
    local start = os.clock()
    
    task.wait(delayTarget)
    
    local elapsed = os.clock() - start
    
    -- Tolerância de margem devido ao timer do SO e taxa de atualização do event loop
    local minExpected = delayTarget - 0.02
    local maxExpected = delayTarget + 0.10
    
    assert(elapsed >= minExpected, string.format("task.wait retornou cedo demais! Esperado >= %.3f, veio: %.3f", minExpected, elapsed))
    assert(elapsed <= maxExpected, string.format("task.wait atrasou muito! Esperado <= %.3f, veio: %.3f", maxExpected, elapsed))
    
    print(string.format("  └─ Esperado: ~%.2fs | Real: %.4fs", delayTarget, elapsed))
end)

--------------------------------------------------------------------------------
-- TESTE 2: Signal Connect e Fire básico com Múltiplos Argumentos
--------------------------------------------------------------------------------
runTest("Signal: Disparo síncrono e repasse de múltiplos tipos", function()
    local sig = signal.new()
    local chamou = false
    local recebidoTexto = ""
    local recebidoNum = 0
    local recebidoBool = false

    sig:Connect(function(argStr: string, argNum: number, argBool: boolean)
        chamou = true
        recebidoTexto = argStr
        recebidoNum = argNum
        recebidoBool = argBool
    end)

    sig:Fire("LodeRuntime", 42, true)

    assert(chamou, "O callback do Signal não foi executado!")
    assert(recebidoTexto == "LodeRuntime", "Falha ao passar string via Signal!")
    assert(recebidoNum == 42, "Falha ao passar number via Signal!")
    assert(recebidoBool == true, "Falha ao passar boolean via Signal!")
end)

--------------------------------------------------------------------------------
-- TESTE 3: Múltiplos Listeners no mesmo Signal
--------------------------------------------------------------------------------
runTest("Signal: Múltiplos ouvintes conectados", function()
    local sig = signal.new()
    local contador = 0

    sig:Connect(function()
        contador += 1
    end)
    sig:Connect(function()
        contador += 10
    end)
    sig:Connect(function()
        contador += 100
    end)

    sig:Fire()

    assert(contador == 111, string.format("Nem todos os ouvintes foram disparados! Esperado 111, veio: %d", contador))
end)

--------------------------------------------------------------------------------
-- TESTE 4: Disconnect de Sinal
--------------------------------------------------------------------------------
runTest("Signal: Desconexão de Ouvinte (Disconnect)", function()
    local sig = signal.new()
    local chamadas = 0

    local conexao = sig:Connect(function()
        chamadas += 1
    end)

    sig:Fire()
    assert(chamadas == 1, "Primeira execução do sinal falhou!")

    -- Desconecta o ouvinte
    if typeof(conexao) == "table" and conexao.Disconnect then
        conexao:Disconnect()
    elseif typeof(conexao) == "function" then
        conexao() -- Suporte caso retorne uma função cleanup
    end

    sig:Fire()
    assert(chamadas == 1, string.format("Ouvinte continuou executando após Disconnect! Chamadas: %d", chamadas))
end)

--------------------------------------------------------------------------------
-- TESTE 5: Integração Assíncrona (task.delay + Signal)
--------------------------------------------------------------------------------
runTest("Integração: Disparo de Sinal vindo de task.delay", function()
    local sig = signal.new()
    local sinalRecebido = false
    local mensagemSinal = ""
    local tempoDisparo = 0.3
    local inicio = os.clock()

    sig:Connect(function(msg: string)
        sinalRecebido = true
        mensagemSinal = msg
    end)

    -- Agenda o disparo do sinal para daqui a 300ms
    task.delay(tempoDisparo, function()
        sig:Fire("Mensagem Assíncrona")
    end)

    assert(not sinalRecebido, "Sinal disparou antes do tempo programado!")

    -- Aguarda o tempo necessário para o evento ser processado na libuv
    task.wait(tempoDisparo + 0.1)

    local delta = os.clock() - inicio

    assert(sinalRecebido, "Sinal assíncrono disparado via task.delay não foi recebido!")
    assert(mensagemSinal == "Mensagem Assíncrona", "Payload do sinal assíncrono corrompido!")
    assert(delta >= tempoDisparo, "Fluxo principal retomado antes do tempo!")
    
    print(string.format("  └─ Signal recebido com sucesso após %.4fs", delta))
end)

--------------------------------------------------------------------------------
-- TESTE 6: Validação de task.defer (Ordem de Execução do Event Loop)
--------------------------------------------------------------------------------
runTest("Task: Ordem de execução com task.defer", function()
    local ordem = {}

    table.insert(ordem, "1_Inicio")

    task.defer(function()
        table.insert(ordem, "3_Deferred")
    end)

    table.insert(ordem, "2_FimDoScript")

    -- Força um yield para permitir que o Event Loop processe a fila de deferred
    task.wait()

    assert(#ordem == 3, "Nem todas as etapas foram executadas!")
    assert(ordem[1] == "1_Inicio", "Ordem incorreta na etapa 1")
    assert(ordem[2] == "2_FimDoScript", "task.defer rodou antes do script síncrono terminar!")
    assert(ordem[3] == "3_Deferred", "task.defer não foi executado no proximo tick!")
end)

print("==================================================")
print("🎉 TODOS OS TESTES PASSARAM COM SUCESSO!")
print("==================================================")
