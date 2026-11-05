
-- The UI package (android/TOUCH_UI.md): the Menu tab's ten large buttons need 837 of FAF's lines under the HUD's tabs.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local BuildMenu = Scale.Built(837, BuildContent)
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
