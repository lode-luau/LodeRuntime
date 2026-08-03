local base64 = require("./base64")
local initialTime = os.clock()
local bufferMessage = buffer.fromstring(string.rep("TESTANDO BASE 64!", 6000))

--[[
   Debug Mode:
   Time encoding: 0.12123140000039712 seconds
   Time decoding: 0.10798300000169547 seconds
   Time tostring: 0.00011719999747583643 seconds

   Release Mode:
   Time encoding: 0.027641000000585336 seconds
   Time decoding: 0.023162199999205768 seconds
   Time tostring: 0.000028099995688535273 seconds

   Gemini Optimization Debug:
   Time encoding: 0.1355122000022675 seconds
   Time decoding: 0.10527100000035716 seconds
   Time tostring: 0.00012399999832268804 seconds

   Gemini Optimization Release:
   Time encoding: 0.02955620000284398 seconds
   Time decoding: 0.02384100000199396 seconds
   Time tostring: 0.00004170000465819612 seconds
]]

local encoded = base64.encode(bufferMessage)
print("Time encoding: " .. (os.clock() - initialTime) .. " seconds")

initialTime = os.clock()
local decoded = base64.decode(encoded)
print("Time decoding: " .. (os.clock() - initialTime) .. " seconds")

initialTime = os.clock()
local str = buffer.tostring(decoded)
print("Time tostring: " .. (os.clock() - initialTime) .. " seconds")
