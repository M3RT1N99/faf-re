
-- The UI package (android/TOUCH_UI.md): its margins go by a local factor from the options, which must be the score dialog's.
do
    local ok, problem = pcall(function()
        -- FAF's local, which a later FAF may have dropped: setting it then would make a global nothing reads.
        if type(pixelScaleFactor) ~= 'number' then
            error('this FAF\'s pixelScaleFactor is no number')
        end
        function SetPixelScale(factor)
            pixelScaleFactor = factor
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/dialogs/hotstats.lua: ' .. tostring(problem))
    end
end
