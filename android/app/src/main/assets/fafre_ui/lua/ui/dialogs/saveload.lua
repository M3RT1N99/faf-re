
-- The UI package (android/TOUCH_UI.md): the dialogs need 640 of FAF's lines: 586 and their brackets.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        CreateSaveDialog = Scale.Dialog(640, CreateSaveDialog)
        CreateLoadDialog = Scale.Dialog(640, CreateLoadDialog)
    end)
    if not ok then
        WARN('fafre_ui: ui/dialogs/saveload.lua: ' .. tostring(problem))
    end
end
