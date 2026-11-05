
-- The UI package (android/TOUCH_UI.md): as large on the glass as the HUD; its picture of 478x318 with its brackets fits.
do
    local ok, problem = pcall(function()
        CreateUI = import('/fafre_ui/lua/fafre/scale.lua').Dialog(480, CreateUI, nil, 480)
    end)
    if not ok then
        WARN('fafre_ui: ui/lobby/gamecreate.lua: ' .. tostring(problem))
    end
end
