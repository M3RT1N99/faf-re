
-- The UI package (android/TOUCH_UI.md): each UI state says once, as it is set up, which hooks of the package this FAF no longer
-- runs, as the files they are for are gone (fafre/scale.lua CheckHookedFiles). The engine sets up every state by SetupUI: the
-- front end's, a lobby's from the command line or from the FAF client (gpgnet), a session's. A new front end, also after a
-- session, takes the fingers and the view back.
do
    local ok, problem = pcall(function()
        local Setup = SetupUI
        SetupUI = function()
            Setup()
            -- The engine ends the game where SetupUI fails: what fails here only says so.
            local checked, why = pcall(function()
                import('/fafre_ui/lua/fafre/scale.lua').CheckHookedFiles()
            end)
            if not checked then
                WARN('fafre_ui: the hooks\' files: ' .. tostring(why))
            end
        end
        local Start = StartFrontEndUI
        StartFrontEndUI = function()
            local started, why = pcall(function()
                import('/fafre_ui/lua/fafre/scale.lua').StartFrontEnd()
            end)
            if not started then
                WARN('fafre_ui: the front end: ' .. tostring(why))
            end
            Start()
            -- The bar over the menus (fafre/bar.lua), after FAF's front end has made its first screen.
            local made, failure = pcall(function()
                import('/fafre_ui/lua/fafre/bar.lua').Create(false)
            end)
            if not made then
                WARN('fafre_ui: the bar: ' .. tostring(failure))
            end
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/uimain.lua: ' .. tostring(problem))
    end
end
