
-- The UI package (android/TOUCH_UI.md): the LAN screen is a page of its panel, with its brackets above and below and its Create button,
-- which stands out on the right.
do
    local ok, problem = pcall(function()
        CreateUI = import('/fafre_ui/lua/fafre/scale.lua').Page(CreateUI, function(made)
            local panel = made[1]
            return panel, {
                left = panel.Left() - 20, right = panel.Right() + 20,
                top = math.max(0, panel.Top() - 32), bottom = math.min(GetFrame(0).Height(), panel.Bottom() + 30),
            }
        end)
    end)
    if not ok then
        WARN('fafre_ui: ui/lobby/gameselect.lua: ' .. tostring(problem))
    end
end
