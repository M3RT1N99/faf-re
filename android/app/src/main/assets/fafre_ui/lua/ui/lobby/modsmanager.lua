
-- The UI package (android/TOUCH_UI.md): FAF lays the dialog out by the screen's height at any factor, 850 wide: from the main menu it is as large on
-- the glass as the HUD, and on the lobby's page it needs no more.
do
    local ok, problem = pcall(function()
        CreateDialog = import('/fafre_ui/lua/fafre/scale.lua').Dialog(480, CreateDialog, nil, 860)
    end)
    if not ok then
        WARN('fafre_ui: ui/lobby/modsmanager.lua: ' .. tostring(problem))
    end
end
