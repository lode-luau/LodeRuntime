local stdio = require("@stdio")
local nome = stdio.prompt("Qual é o seu nome?\n>> ")

local idade = stdio.prompt("Qual é a sua idade?\n>> ")

if type(tonumber(nome)) == "number" then
    stdio.print("Olá, " .. nome .. "! Você tem " .. idade .. " anos!")
else
    stdio.print("Olá, " .. idade .. "! Você tem " .. nome .. " anos!")
end