
-- The UI package (android/TOUCH_UI.md): its margins go by a local factor from the options, which must be the score dialog's.
do
    local ok, problem = pcall(function()
        function SetPixelScale(factor)
            pixelScaleFactor = factor
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/dialogs/hotstats.lua: ' .. tostring(problem))
    end
end
