
-- The UI package (android/TOUCH_UI.md): a finger takes a slider where it lands. A press on the bar sets the value, in FAF, and here
-- it also takes the thumb along for the drag that may follow, which FAF has only from the thumb.
do
    local ok, problem = pcall(function()
        local Init = Slider.__init
        Slider.__init = function(self, ...)
            Init(self, unpack(arg))
            local OnBar = self._background.HandleEvent
            self._background.HandleEvent = function(control, event)
                local handled = OnBar(control, event)
                if event.Type == 'ButtonPress' then
                    self._thumb.HandleEvent(self._thumb, event)
                end
                return handled
            end
        end
    end)
    if not ok then
        WARN('fafre_ui: maui/slider.lua: ' .. tostring(problem))
    end
end
