
-- The UI package (android/TOUCH_UI.md): a new minimap goes right of the screen's middle, where it covers no panel and none of
-- Griff's pieces: past the handle's reach about the middle, under the score panel of a game of up to seven armies (a replay's of
-- up to four: it is taller), over the construction panel and left of the draft's cross. As high as that room, which a map's
-- square needs, and as wide.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local Create = CreateMinimap
        CreateMinimap = function(parent)
            local placed = import('/lua/user/prefs.lua').GetFromCurrentProfile('mini_ui_minimap')
            Create(parent)
            Scale.Try('the place of the minimap', function()
                local window = controls.displayGroup
                Scale.GiveWindowHome(window, window.resetBtn, not placed, 576, 166, 744, 328)
            end)
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/game/minimap.lua: ' .. tostring(problem))
    end
end
