
-- The UI package (android/TOUCH_UI.md): the dialog grows with the players: 640 of FAF's lines hold nine.
do
    local ok, problem = pcall(function()
        CreateUI = import('/fafre_ui/lua/fafre/scale.lua').Dialog(640, CreateUI)
    end)
    if not ok then
        WARN('fafre_ui: ui/game/connectivity.lua: ' .. tostring(problem))
    end
end
