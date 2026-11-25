
-- The UI package (android/TOUCH_UI.md): FAF puts an announcement's title 250 of its lines above the screen's middle, which at the HUD's
-- factor on the phone's 768 lines is above the screen's top, so that only the end of its text showed. It stands no further above the
-- middle than FAF's least screen has it (fafre/scale.lua AboveMiddle), and its text below it. FAF takes the content's bottom from the
-- text once, where the title stood before: it follows the text.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local Lay = Scale.Needed(TitleTextAnnouncement.__post_init, 'TitleTextAnnouncement.__post_init')
        TitleTextAnnouncement.__post_init = function(self, parent, titleText, bodyText)
            Lay(self, parent, titleText, bodyText)
            Scale.Try('an announcement\'s place', function()
                local text = Scale.Needed(self.Text, 'an announcement\'s Text')
                Scale.AboveMiddle(Scale.Needed(self.Title, 'an announcement\'s Title'), parent, 250)
                Scale.Needed(self.ContentArea, 'an announcement\'s ContentArea').Bottom:Set(function() return text.Bottom() end)
            end)
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/game/announcement/titletextannouncement.lua: ' .. tostring(problem))
    end
end
