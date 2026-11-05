
-- The UI package (android/TOUCH_UI.md): the licence is read as large on the glass as the HUD, with fewer of its lines at a time, which a finger scrolls.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local LEAST_LINES = 480
        local WIDTH = 800
        local BRACKET_LINES = 25
        local RealList = ItemList
        local list = false
        ItemList = function(...)
            list = RealList(unpack(arg))
            return list
        end
        local Create = CreateEULA
        CreateEULA = Scale.Dialog(LEAST_LINES, function(inParent, callback)
            Create(inParent, callback)
            Scale.Try('the frame of the licence', function()
                Scale.FitByList(list, UIUtil.UIFile('/scx_menu/eula/eula.dds'), BRACKET_LINES, list)
            end)
        end, nil, WIDTH)
    end)
    if not ok then
        WARN('fafre_ui: ui/menus/eula.lua: ' .. tostring(problem))
    end
end
