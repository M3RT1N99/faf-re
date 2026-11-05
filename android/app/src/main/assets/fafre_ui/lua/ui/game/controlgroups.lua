
-- The UI package (android/TOUCH_UI.md): FAF's 368 lines down lie under the construction panel at the HUD's factor: right below the avatars.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local Layout = SetLayout
        SetLayout = function()
            Layout()
            Scale.Try('the place of the control groups', function()
                local LayoutHelpers = import('/lua/maui/layouthelpers.lua')
                local avatars = import('/lua/ui/game/avatars.lua').controls.avatarGroup
                controls.container.Top:Set(function()
                    return avatars.Bottom() + LayoutHelpers.ScaleNumber(24)
                end)
            end)
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/game/controlgroups.lua: ' .. tostring(problem))
    end
end
