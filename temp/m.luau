local stdio = require("../modules/stdio")

local nome = stdio.prompt("Qual é o seu nome?\n>> ")
local idade = stdio.prompt("Qual é a sua idade?\n>> ")

stdio.print("Olá, " .. idade .. "! Você tem " .. nome .. " anos!")