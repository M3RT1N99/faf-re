
-- The UI package (android/TOUCH_UI.md): the multifunction panel's dropouts, the overlay filters' list right of the panel, the map's
-- options and the team colours in the middle, are what the player opened, which nothing of Griff's may cover (fafre/opened.lua):
-- the list lies across the rail's row. Each comes from one background, whose box holds its rows.
do
    local ok, problem = pcall(function()
        local Opened = import('/fafre_ui/lua/fafre/opened.lua')
        local CreateBackground = CreateDropoutBG
        CreateDropoutBG = function(createConnector)
            local background = CreateBackground(createConnector)
            Opened.Add(background)
            return background
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/game/multifunction.lua: ' .. tostring(problem))
    end
end
