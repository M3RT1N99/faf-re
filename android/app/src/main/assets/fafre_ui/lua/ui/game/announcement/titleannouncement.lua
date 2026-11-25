
-- The UI package (android/TOUCH_UI.md): FAF puts an announcement's title 250 of its lines above the screen's middle, which at the HUD's
-- factor on the phone's 768 lines is above the screen's top, so that only the announcement's lower edge showed. It stands no further above
-- the middle than FAF's least screen has it (fafre/scale.lua AboveMiddle); the announcement's content goes with its title.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local Lay = Scale.Needed(TitleAnnouncement.__post_init, 'TitleAnnouncement.__post_init')
        TitleAnnouncement.__post_init = function(self, parent, titleText)
            Lay(self, parent, titleText)
            Scale.Try('an announcement\'s place', function()
                Scale.AboveMiddle(Scale.Needed(self.Title, 'an announcement\'s Title'), parent, 250)
            end)
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/game/announcement/titleannouncement.lua: ' .. tostring(problem))
    end
end
