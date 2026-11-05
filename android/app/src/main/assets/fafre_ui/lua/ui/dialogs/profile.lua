
-- The UI package (android/TOUCH_UI.md): the dialog with its brackets needs 510 of FAF's lines and 670 pixels, and a finger picks a row of its list.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local RealList = ItemList
        local list = false
        ItemList = function(...)
            list = RealList(unpack(arg))
            return list
        end
        local Create = CreateDialog
        CreateDialog = Scale.Dialog(510, function(exitBehavior)
            local panel = Create(exitBehavior)
            Scale.Try('the rows of the profiles', function() Scale.FitRows(list, 4.5) end)
            return panel
        end, nil, 670)
    end)
    if not ok then
        WARN('fafre_ui: ui/dialogs/profile.lua: ' .. tostring(problem))
    end
end
