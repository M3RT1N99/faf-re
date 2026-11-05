
-- The UI package (android/TOUCH_UI.md): FAF's front end needs its 768 lines, and its UI scale above 100 % loses the lower part.
-- The front end stays at a factor of 1, and a session sets its own (fafre/scale.lua), in which the option has its say.
do
    local ok, problem = pcall(function()
        pixelScaleFactor = 1
        if GetPixelScaleFactor() ~= 1 then
            error('FAF has its factor elsewhere now')
        end
    end)
    if not ok then
        WARN('fafre_ui: maui/layouthelpers.lua: ' .. tostring(problem))
    end
end
