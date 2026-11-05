
-- The UI package (android/TOUCH_UI.md): the lobby is a page of its panel and of its Launch button, which stands out below it: all of
-- FAF's 768 lines. Its slot rows lie further apart when the map has few of them: the 16 rows of FAF's layout have room for 8 at twice the
-- distance. A finger that rests on an observer for the host's right click sees which row it has. A dialog can hold a factor of its own over the
-- lobby, the Unit Manager, while the host goes on sending and the ping's thread and the keyboard go on: what they do stays at the lobby's.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        -- What the rows take in FAF's layout, 16 of them 22 apart.
        local SLOTS_HEIGHT = 352
        local LEAST_PITCH = 22
        local MOST_PITCH = 44
        local rowHeight = false
        local pitch = LEAST_PITCH

        local Create = CreateUI
        CreateUI = Scale.Page(function(...)
            -- A new lobby has new rows, of FAF's height.
            rowHeight = false
            local results = {Create(unpack(arg))}
            Scale.Try('the list of observers', function() GUI.observerList:ShowMouseoverItem(true) end)
            return unpack(results)
        end, function()
            return GUI.panel, {
                top = math.max(0, GUI.panel.Top() - LayoutHelpers.ScaleNumber(8)),
                bottom = math.min(GetFrame(0).Height(), GUI.panel.Bottom() + LayoutHelpers.ScaleNumber(8)),
            }
        end)

        local function PlaceSlot(index)
            local slot = GUI.slots[index]
            local top = (index - 1) * LayoutHelpers.ScaleNumber(pitch)
            local height = LayoutHelpers.ScaleNumber(pitch - 3)
            slot.Top:Set(function()
                return GUI.labelGroup.Bottom() + top + math.floor((height - rowHeight) / 2)
            end)
            slot.SlotBackground.Top:Set(function()
                return GUI.labelGroup.Bottom() + top
            end)
            slot.SlotBackground.Height:Set(height)
        end

        local UpdateSlots = UpdateAvailableSlots
        UpdateAvailableSlots = function(numAvailStartSpots, scenario)
            UpdateSlots(numAvailStartSpots, scenario)
            Scale.Try('the slot rows', function()
                if not rowHeight then
                    rowHeight = GUI.slots[1].Height()
                    for index, slot in GUI.slots do
                        slot.Height:Set(rowHeight)
                    end
                end
                pitch = math.max(LEAST_PITCH, math.min(MOST_PITCH, math.floor(SLOTS_HEIGHT / numAvailStartSpots)))
                for index, slot in GUI.slots do
                    PlaceSlot(index)
                    -- FAF keeps showing the background of a row it has hidden, for which there is no room here.
                    slot.SlotBackground:SetHidden(index > numAvailStartSpots and pitch > LEAST_PITCH)
                end
            end)
        end

        local function AtLobby(work)
            return Scale.AtFrontEnd(work, function() return GUI end)
        end
        UpdateGame = AtLobby(UpdateGame)
        RefreshOptionDisplayData = AtLobby(RefreshOptionDisplayData)
        AddChatText = AtLobby(AddChatText)
        CalcConnectionStatus = AtLobby(CalcConnectionStatus)
        local InitComm = InitLobbyComm
        InitLobbyComm = function(...)
            InitComm(unpack(arg))
            Scale.Try('the messages of a lobby', function()
                lobbyComm.DataReceived = AtLobby(lobbyComm.DataReceived)
                lobbyComm.PeerDisconnected = AtLobby(lobbyComm.PeerDisconnected)
            end)
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/lobby/lobby.lua: ' .. tostring(problem))
    end
end
