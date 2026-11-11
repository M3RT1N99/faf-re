
-- The UI package (android/TOUCH_UI.md): what the HUD builds when the game calls it keeps the HUD's factor, whatever dialog is open.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        OnSelectionChanged = Scale.AtHud(OnSelectionChanged)
        OnQueueChanged = Scale.AtHud(OnQueueChanged)
        OnBeat = Scale.AtHud(OnBeat)
        OnPause = Scale.AtHud(OnPause)
        OnResume = Scale.AtHud(OnResume)
    end)
    if not ok then
        WARN('fafre_ui: ui/game/gamemain.lua: ' .. tostring(problem))
    end
end

-- Griff, the app's touch scheme: the session's UI reports where the selection is, for the handle the app shows beside it.
do
    local CreateGameUI = CreateUI
    CreateUI = function(isReplay)
        CreateGameUI(isReplay)
        local ok, problem = pcall(function()
            import('/fafre_ui/lua/fafre/griff.lua').Start(GetGameParent())
        end)
        if not ok then
            WARN('fafre_ui: ui/game/gamemain.lua: griff: ' .. tostring(problem))
        end
    end
end
