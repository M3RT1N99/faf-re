
-- The UI package (android/TOUCH_UI.md): the Menu tab has the app's Keyboard after Key Bindings, which brings the phone's keyboard as the
-- bar's Keys does (FafreKeyboard): Griff's rail has no Keys. Its large buttons need 137 of FAF's lines for the HUD's tabs and the gap
-- under them and 70 for each, of the list it shows: eleven in a single-player game, ten in a replay, eight in a network game.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        actions.FafreKeyboard = function()
            FafreKeyboard()
        end
        local most = 0
        for _, list in menus.main do
            -- After Key Bindings, or where a list has none, before its last, Close Menu.
            local at = table.getn(list)
            for index, button in list do
                if button.action == 'KeyBindings' then
                    at = index + 1
                    break
                end
            end
            table.insert(list, at, {action = 'FafreKeyboard', label = 'Keyboard'})
            most = math.max(most, table.getn(list))
        end
        -- The lines of the list BuildContent shows, picked as it picks it (FAF 3835's tabs.lua, 608 to 622): the FAF client's
        -- (/gpgnet), a network game's, a replay's, else a single-player game's, without its buttons hidden in a ranked game. Where
        -- FAF's Lua has changed under it, those of the longest list, which fit whichever FAF shows.
        local function MenuLines()
            local count = most
            Scale.Try('the Menu\'s list', function()
                local tableID = 'singlePlayer'
                if HasCommandLineArg('/gpgnet') then
                    tableID = 'gpgnet'
                elseif SessionIsMultiplayer() then
                    tableID = 'lan'
                elseif GameMain.GetReplayState() then
                    tableID = 'replay'
                end
                local ranked = sessionInfo.Options.Ranked
                local shown = 0
                for _, button in menus.main[tableID] do
                    if not ranked or not button.hideWhenRanked then
                        shown = shown + 1
                    end
                end
                count = shown
            end)
            return 137 + 70 * count
        end
        local BuildMenu = Scale.Built(MenuLines, BuildContent)
        local BuildOther = BuildContent
        local Opened = import('/fafre_ui/lua/fafre/opened.lua')
        BuildContent = function(contentID)
            if contentID ~= 'main' then
                BuildOther(contentID)
            else
                BuildMenu(contentID)
            end
            -- What a tab shows, the Menu's list or the diplomacy, is what the player opened, which nothing of Griff's may cover
            -- (fafre/opened.lua). Where another was open, FAF first folds that one away and builds this one after.
            if controls.contentGroup then
                Opened.Add(controls.contentGroup)
            end
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/game/tabs.lua: ' .. tostring(problem))
    end
end
