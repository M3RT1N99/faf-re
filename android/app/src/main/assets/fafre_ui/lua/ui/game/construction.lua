
-- Griff, the app's touch scheme: a build icon of this panel puts its building into the app's draft, not FAF's ghost
-- (fafre/griff.lua BuildIconClicked, after FAF's own handler). The panel's buttons take the handler when they are made, after
-- this runs; FAF's build mode keys reach it here too, without a mouse button, and stay FAF's.
do
    local ok, problem = pcall(function()
        local FafClick = OnClickHandler
        OnClickHandler = function(button, modifiers)
            FafClick(button, modifiers)
            local done, failed = pcall(function()
                import('/fafre_ui/lua/fafre/griff.lua').BuildIconClicked(button.Data, modifiers)
            end)
            if not done then
                WARN('fafre_ui: ui/game/construction.lua: griff: ' .. tostring(failed))
            end
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/game/construction.lua: ' .. tostring(problem))
    end
end
