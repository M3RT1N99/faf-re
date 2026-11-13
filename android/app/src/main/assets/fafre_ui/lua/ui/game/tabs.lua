
-- The UI package (android/TOUCH_UI.md): the Menu tab has the app's Keyboard after Key Bindings, which brings the phone's keyboard as the
-- bar's Keys does (FafreKeyboard): Griff's rail has no Keys. Its eleven large buttons need 907 of FAF's lines under the HUD's tabs,
-- 70 for each.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        actions.FafreKeyboard = function()
            FafreKeyboard()
        end
        for _, list in menus.main do
            -- After Key Bindings, or where a list has none, before its last, Close Menu.
            local at = table.getn(list)
            for index, button in list do
                if button.action == 'KeyBindings' then
                    at = index + 1
                    break
                end
            end
            table.insert(list, at, {action = 'FafreKeyboard', label = 'Keyboard'})
        end
        local BuildMenu = Scale.Built(907, BuildContent)
        local BuildOther = BuildContent
        BuildContent = function(contentID)
            if contentID ~= 'main' then
                return BuildOther(contentID)
            end
            BuildMenu(contentID)
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/game/tabs.lua: ' .. tostring(problem))
    end
end
