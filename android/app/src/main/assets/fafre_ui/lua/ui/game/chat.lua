
-- The UI package (android/TOUCH_UI.md): FAF's place for a new chat window lies at this factor over the multifunction panel and
-- Griff's rail. It goes above the screen's middle instead, between the rail's row with the undo's pill and the score panel, under
-- FAF's tabs and short of the handle's reach about the middle, where it covers no panel and none of Griff's pieces; open, its
-- bottom lies over the minimap's top. The phone's keyboard covers most of the screen while it is up, wherever the window stands.
-- From FAF 3841 on the chat is ui/game/chat/ChatInterface.lua, whose hook gives its window the home, and this file only forwards
-- to it (the engine imports it at Enter): here there is nothing to do then.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        if Scale.Replaced('/lua/ui/game/chat.lua') then
            return
        end
        local Create = CreateChatBackground
        CreateChatBackground = function()
            local placed = import('/lua/user/prefs.lua').GetFromCurrentProfile('chat_window')
            local window = Create()
            Scale.Try('the place of the chat window', function()
                local reset = Scale.Needed(window.ResetPositionBtn, 'the chat window\'s ResetPositionBtn')
                Scale.GiveWindowHome(window, reset, not placed, 472, 72, 662, 186)
                -- FAF's reset makes the lines for the size of its own place: they are made again for the home's, as after the
                -- player's resize.
                local Reset = reset.OnClick
                reset.OnClick = function(self, modifiers)
                    Reset(self, modifiers)
                    window:OnResizeSet()
                end
            end)
            return window
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/game/chat.lua: ' .. tostring(problem))
    end
end
