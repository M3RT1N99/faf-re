
-- The UI package (android/TOUCH_UI.md): a finger's pointer leaves after each tap (The pointer), which FAF's avatar takes for the
-- mouse going and starts its round of the idle units over: a tap took the first idle unit each time. The round goes on from where
-- it was, so that taps take the idle units in turn, as a mouse's clicks do.
do
    local ok, problem = pcall(function()
        local Click = ClickFunc
        ClickFunc = function(self, event)
            local at = self.curIndex
            local handled = Click(self, event)
            if event.Type == 'MouseExit' then
                self.curIndex = at
            end
            return handled
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/game/avatars.lua: ' .. tostring(problem))
    end
end
