
-- The UI package (android/TOUCH_UI.md): FAF hands the main menu's function that shows the menu again to where the Mods Manager goes through the other players'
-- mods, which calls it as it loads them: the menu comes back under the dialog, and at the dialog's factor. Here the function comes when the dialog has closed.
do
    local ok, problem = pcall(function()
        CreateDialog = function(over, inLobby, exitBehavior)
            local popup = import('/lua/ui/lobby/modsmanager.lua').CreateDialog(over, true, {})
            import('/fafre_ui/lua/fafre/scale.lua').Try('the way back from the Mods Manager', function()
                local Close = popup.Close
                popup.Close = function(self)
                    Close(self)
                    exitBehavior()
                end
            end)
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/dialogs/modmanager.lua: ' .. tostring(problem))
    end
end
