
-- The UI package (android/TOUCH_UI.md): a question box of the front end is as large on the glass as a session's. It holds no factor: the lobby asks its first question,
-- that it is connecting, just before it builds all the rest. In a session it stays what it was: at the factor there is, and with the fingers as they are.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local Question = QuickDialog
        -- The longest question with its buttons and frame, and its width.
        local LargerQuestion = Scale.BuiltAt(function() return Scale.Fit(480, 560) end, Question)
        QuickDialog = function(...)
            if Scale.InSession() then
                return Question(unpack(arg))
            end
            return LargerQuestion(unpack(arg))
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/uiutil.lua: ' .. tostring(problem))
    end
end
