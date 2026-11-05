
-- The UI package (android/TOUCH_UI.md): the dialog needs 646 of FAF's lines: 598 and its brackets.
do
    local ok, problem = pcall(function()
        CreateDialog = import('/fafre_ui/lua/fafre/scale.lua').Dialog(646, CreateDialog)
    end)
    if not ok then
        WARN('fafre_ui: ui/dialogs/options.lua: ' .. tostring(problem))
    end
end
