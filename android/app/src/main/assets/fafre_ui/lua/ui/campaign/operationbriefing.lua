
-- The UI package (android/TOUCH_UI.md): the rows of the choice of difficulty and faction are 20 of FAF's lines: as many that they are a finger's size.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        CreatePopup = Scale.BuiltAt(function()
            return Scale.PicturePixels(5) / 20
        end, CreatePopup)
    end)
    if not ok then
        WARN('fafre_ui: ui/campaign/operationbriefing.lua: ' .. tostring(problem))
    end
end
