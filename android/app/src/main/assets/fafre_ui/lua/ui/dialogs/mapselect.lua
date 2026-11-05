
-- The UI package (android/TOUCH_UI.md): the dialog stands on the lobby's page, and a finger picks a map from its list.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local Create = CreateDialog
        CreateDialog = function(...)
            local popup = Create(unpack(arg))
            Scale.Try('the rows of the maps', function()
                Scale.FitRows(mapList, 4)
                mapList:ShowItem(mapList:GetSelection())
            end)
            return popup
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/dialogs/mapselect.lua: ' .. tostring(problem))
    end
end
