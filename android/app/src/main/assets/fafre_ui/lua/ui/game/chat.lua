
-- The UI package (android/TOUCH_UI.md): a new chat window goes where it covers no panel and the on-screen keyboard leaves it in view: FAF's place at this factor lies over the multifunction panel and the app's buttons.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local Create = CreateChatBackground
        CreateChatBackground = function()
            local placed = import('/lua/user/prefs.lua').GetFromCurrentProfile('chat_window')
            local window = Create()
            Scale.Try('the place of the chat window', function()
                Scale.GiveWindowHome(window, window.ResetPositionBtn, not placed, 420, 80, 842, 235)
            end)
            return window
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/game/chat.lua: ' .. tostring(problem))
    end
end
