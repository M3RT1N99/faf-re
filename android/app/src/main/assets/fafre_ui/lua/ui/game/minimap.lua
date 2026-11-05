
-- The UI package (android/TOUCH_UI.md): a new minimap goes beside the app's buttons and above the unit view, where it covers no panel.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local Create = CreateMinimap
        CreateMinimap = function(parent)
            local placed = import('/lua/user/prefs.lua').GetFromCurrentProfile('mini_ui_minimap')
            Create(parent)
            Scale.Try('the place of the minimap', function()
                local window = controls.displayGroup
                Scale.GiveWindowHome(window, window.resetBtn, not placed, 226, 70, 416, 255)
            end)
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/game/minimap.lua: ' .. tostring(problem))
    end
end
