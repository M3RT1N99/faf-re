
-- The UI package (android/TOUCH_UI.md): a finger takes a slider where it lands. A press on the bar sets the value, in FAF, and here
-- it also takes the thumb along for the drag that may follow, which FAF has only from the thumb.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local Init = Scale.Needed(Slider.__init, 'Slider.__init')
        Slider.__init = function(self, ...)
            Init(self, unpack(arg))
            Scale.Try('a slider\'s bar', function()
                local thumb = Scale.Needed(self._thumb, 'a slider\'s _thumb')
                local bar = Scale.Needed(self._background, 'a slider\'s _background')
                local OnBar = Scale.Needed(bar.HandleEvent, 'the HandleEvent of a slider\'s bar')
                bar.HandleEvent = function(control, event)
                    local handled = OnBar(control, event)
                    if event.Type == 'ButtonPress' then
                        thumb.HandleEvent(thumb, event)
                    end
                    return handled
                end
            end)
        end
    end)
    if not ok then
        WARN('fafre_ui: maui/slider.lua: ' .. tostring(problem))
    end
end
