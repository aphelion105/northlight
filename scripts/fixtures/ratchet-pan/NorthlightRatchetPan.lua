local fixture = CreateFrame("Frame")
fixture:RegisterEvent("PLAYER_ENTERING_WORLD")

fixture:SetScript("OnEvent", function(self)
    self:UnregisterEvent("PLAYER_ENTERING_WORLD")
    local deadline = GetTime() + 20
    local phase = 0
    local speed = 100 / (tonumber(GetCVar("cameraYawMoveSpeed")) or 180)

    self:SetScript("OnUpdate", function(self)
        local now = GetTime()
        if now < deadline then return end
        phase = phase + 1
        if phase == 1 then
            SendChatMessage("NorthlightRatchet:idle", "SAY")
            deadline = now + 4
        elseif phase == 2 then
            MoveViewLeftStart(speed)
            SendChatMessage("NorthlightRatchet:left", "SAY")
            deadline = now + 4.5
        elseif phase == 3 then
            MoveViewLeftStop()
            MoveViewRightStart(speed)
            SendChatMessage("NorthlightRatchet:right", "SAY")
            deadline = now + 4.5
        else
            MoveViewRightStop()
            SendChatMessage("NorthlightRatchet:done", "SAY")
            self:SetScript("OnUpdate", nil)
        end
    end)
end)
