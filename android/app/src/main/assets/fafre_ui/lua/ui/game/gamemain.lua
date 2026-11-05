
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
