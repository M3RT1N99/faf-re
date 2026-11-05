
-- The UI package (android/TOUCH_UI.md): a new front end, also after a session, takes the fingers and the view back.
do
    local ok, problem = pcall(function()
        local Start = StartFrontEndUI
        StartFrontEndUI = function()
            local started, why = pcall(function()
                import('/fafre_ui/lua/fafre/scale.lua').StartFrontEnd()
            end)
            if not started then
                WARN('fafre_ui: the front end: ' .. tostring(why))
            end
            Start()
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/uimain.lua: ' .. tostring(problem))
    end
end
