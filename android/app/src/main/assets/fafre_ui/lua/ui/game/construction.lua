
-- Griff, the app's touch scheme: a build icon of this panel puts its building into the app's draft, not FAF's ghost
-- (fafre/griff.lua BuildIconClicked, after FAF's own handler). The panel's buttons take the handler when they are made, after
-- this runs. FAF's build mode keys arm the build mode themselves (buildmode.lua) and never come here; a template's key comes
-- without a mouse button (BuildTemplate) and a template's click is no build item's, so both stay FAF's.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local FafClick = OnClickHandler
        OnClickHandler = function(button, modifiers)
            FafClick(button, modifiers)
            Scale.Try('ui/game/construction.lua: griff', function()
                import('/fafre_ui/lua/fafre/griff.lua').BuildIconClicked(button.Data, modifiers)
            end)
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/game/construction.lua: ' .. tostring(problem))
    end
end
