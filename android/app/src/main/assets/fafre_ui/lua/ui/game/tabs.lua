
-- The UI package (android/TOUCH_UI.md): the Menu tab's ten large buttons need 837 of FAF's lines under the HUD's tabs.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local BuildMenu = Scale.Built(837, BuildContent)
        local BuildOther = BuildContent
        BuildContent = function(contentID)
            if contentID ~= 'main' then
                return BuildOther(contentID)
            end
            BuildMenu(contentID)
            -- A button's next texture would size it anew at the HUD's factor.
            Scale.Try('the size of the buttons of the Menu tab', function()
                for _, button in (controls.contentGroup and controls.contentGroup.Buttons or {}) do
                    button.Width:SetValue(button.Width())
                    button.Height:SetValue(button.Height())
                end
            end)
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/game/tabs.lua: ' .. tostring(problem))
    end
end
