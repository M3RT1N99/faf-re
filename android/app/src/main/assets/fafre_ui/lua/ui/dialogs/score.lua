
-- The UI package (android/TOUCH_UI.md): the score dialog needs 646 of FAF's lines; setting its skin rebuilds the HUD, so its first control is not its root; its statistics read the factor from the options.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local Create = Scale.Dialog(646, CreateSkirmishScreen, function() return dialog end)
        CreateSkirmishScreen = function(victory, showCampaign, operationVictoryTable)
            Scale.Try('the factor of the statistics', function()
                hotstats.SetPixelScale(Scale.Fit(646))
            end)
            return Create(victory, showCampaign, operationVictoryTable)
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/dialogs/score.lua: ' .. tostring(problem))
    end
end
