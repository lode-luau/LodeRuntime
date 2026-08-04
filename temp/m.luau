local stdio = require("@stdio")
local signal = require("@signal")

local infoReceived: signal.SignalInternal<string> = signal.new()

infoReceived:Connect(function(info: string)
    stdio.print("Info: " .. info)
end)

local nome = stdio.prompt("Qual é o seu nome?\n>> ")
infoReceived:Fire(nome)

local idade = stdio.prompt("Qual é a sua idade?\n>> ")
infoReceived:Fire(idade)

if type(tonumber(nome)) == "number" then
    stdio.print("Olá, " .. nome .. "! Você tem " .. idade .. " anos!")
else
    stdio.print("Olá, " .. idade .. "! Você tem " .. nome .. " anos!")
end