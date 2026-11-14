
-- The UI package (android/TOUCH_UI.md), for FAF's chat from 3841 on (ui/game/chat.lua's hook is for the one before): FAF's place
-- for a new chat window, DefaultRect, which its reset button brings back too, lies at this factor below the picture, 736 of its
-- 768 lines down. The window goes above the screen's middle instead, between the rail's row with the undo's pill and the score
-- panel, under FAF's tabs and short of the handle's reach about the middle, where it covers no panel and none of Griff's pieces;
-- open, its bottom lies over the minimap's top. It is the older chat's home, 8 of FAF's pixels wider for the title this window
-- has, which its buttons would cut. A place the player chose stays FAF's. The phone's keyboard covers most of the screen while it
-- is up, wherever the window stands.
do
    local ok, problem = pcall(function()
        local home = {Left = 466, Top = 72, Right = 664, Bottom = 186}
        for side, _ in home do
            if type(DefaultRect[side]) ~= 'number' then
                error('this FAF\'s DefaultRect has no ' .. side)
            end
        end
        for side, value in home do
            DefaultRect[side] = value
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/game/chat/chatinterface.lua: ' .. tostring(problem))
    end
end
