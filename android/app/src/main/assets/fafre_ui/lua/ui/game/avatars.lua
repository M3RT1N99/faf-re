
-- The UI package (android/TOUCH_UI.md): a finger's pointer leaves after each tap (The pointer), which FAF's avatar takes for the
-- mouse going and starts its round of the idle units over: a tap took the first idle unit each time. The round goes on from where
-- it was, so that taps take the idle units in turn, as a mouse's clicks do, while it still fits the avatar's units, which FAF makes
-- anew each beat, best first, without those ordered since. Where the unit the round took last no longer stands just before it, as
-- once the player has ordered that unit, the next press starts over at the best idle unit, as a mouse's click does after the mouse
-- went to give the order.
do
    local ok, problem = pcall(function()
        local Click = ClickFunc
        ClickFunc = function(self, event)
            local press = event.Type == 'ButtonPress' or event.Type == 'ButtonDClick'
            if press and self.fafreKept then
                self.fafreKept = nil
                if not (self.units and self.units[self.curIndex - 1] == self.fafreTaken) then
                    self.curIndex = 1
                end
            end
            local at = self.curIndex
            local handled = Click(self, event)
            if press then
                -- FAF counts on past the unit it takes.
                self.fafreTaken = self.units and self.units[self.curIndex - 1]
            elseif event.Type == 'MouseExit' then
                self.curIndex = at
                self.fafreKept = true
            end
            return handled
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/game/avatars.lua: ' .. tostring(problem))
    end
end
