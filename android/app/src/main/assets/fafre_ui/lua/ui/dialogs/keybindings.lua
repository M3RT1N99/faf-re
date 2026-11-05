
-- The UI package (android/TOUCH_UI.md): the dialog needs 740 of FAF's lines: 730 and the screen's margin.
do
    local ok, problem = pcall(function()
        CreateUI = import('/fafre_ui/lua/fafre/scale.lua').Dialog(740, CreateUI)
    end)
    if not ok then
        WARN('fafre_ui: ui/dialogs/keybindings.lua: ' .. tostring(problem))
    end
end
