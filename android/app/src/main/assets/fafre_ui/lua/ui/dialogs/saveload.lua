
-- The UI package (android/TOUCH_UI.md): the dialogs around a file picker.
do
    local ok, problem = pcall(function()
        CreateDialog = import('/lua/ui/controls/filepicker.lua').InDialog(CreateDialog)
    end)
    if not ok then
        WARN('fafre_ui: ui/dialogs/saveload.lua: ' .. tostring(problem))
    end
end
