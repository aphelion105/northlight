local benchmark = CreateFrame("Frame")
local armedAt
local function effectStatus(name)
    local ok, value = pcall(GetCVar, name)
    if not ok or type(value) ~= "string" or value == "" then return "unavailable" end
    return (value:gsub("[:%c]", " "))
end
benchmark:RegisterEvent("PLAYER_ENTERING_WORLD")
benchmark:RegisterEvent("CHAT_MSG_SYSTEM")
benchmark:SetScript("OnEvent", function(self, event, message)
    if event == "CHAT_MSG_SYSTEM" then
        if message == "NorthlightBenchReady" then
            armedAt = GetTime() + 1
            self:UnregisterEvent("CHAT_MSG_SYSTEM")
        end
        return
    end
    self:UnregisterEvent("PLAYER_ENTERING_WORLD")
    local warmUntil = GetTime() + 20
    local requested, started, last, turned
    local samples = {}
    local rotate = NorthlightBenchmarkRotate
    local speed = 100 / (tonumber(GetCVar("cameraYawMoveSpeed")) or 180)
    local clock = debugprofilestop
    local function send(text)
        SendChatMessage("NorthlightBench:" .. text, "SAY")
    end
    self:SetScript("OnUpdate", function(self)
        if not started then
            if GetTime() < warmUntil then return end
            if not requested then
                send("arm")
                requested = true
            end
            if not armedAt or GetTime() < armedAt then return end
            if rotate then MoveViewLeftStart(speed) end
            send("start")
            started = clock()
            last = started
            return
        end
        local now = clock()
        local elapsed = now - started
        samples[#samples + 1] = now - last
        last = now
        if rotate and not turned and elapsed >= 5000 then
            MoveViewLeftStop()
            MoveViewRightStart(speed)
            send("right")
            turned = #samples
        end
        if elapsed < 10000 then return end
        if rotate then MoveViewRightStop() end
        send("done")
        if NorthlightBenchmarkVulkan then
            send("effects:" .. effectStatus("northlightVulkanContactInfo") .. ":" .. effectStatus("northlightVulkanBloomInfo"))
        end
        send(("header:%d:%d:%.3f"):format(#samples, turned or 0, elapsed))
        local first = 1
        self:SetScript("OnUpdate", function(self)
            local payload = {}
            for i = first, math.min(first + 19, #samples) do
                payload[#payload + 1] = ("%.3f"):format(samples[i])
            end
            send(("data:%d:%s"):format(first, table.concat(payload, ",")))
            first = first + #payload
            if first > #samples then
                send("complete")
                self:SetScript("OnUpdate", nil)
            end
        end)
    end)
end)
