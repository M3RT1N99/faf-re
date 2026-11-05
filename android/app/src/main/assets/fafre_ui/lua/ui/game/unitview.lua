
-- The UI package (android/TOUCH_UI.md): no pointer hovers on a phone: a selected unit keeps its view, and the abilities that stand over the middle of the screen wait for a finger that rests on a unit.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        options.gui_enhanced_unitview = 1
        local Create = CreateUI
        CreateUI = function()
            Create()
            Scale.Try('the unit view at the HUD factor', function()
                controls.bg.OnFrame = Scale.AtHud(controls.bg.OnFrame)
            end)
        end
        local Update = UpdateWindow
        UpdateWindow = function(info)
            Update(info)
            if controls.abilities and not GetRolloverInfo() then
                controls.abilities:Hide()
            end
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/game/unitview.lua: ' .. tostring(problem))
    end
end
