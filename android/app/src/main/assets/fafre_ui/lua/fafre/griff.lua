-- Griff, the app's touch scheme (buildstage/touch-concept-v2.md): where the selection is on the screen, for the handle
-- that the app shows beside it, and the few things its gestures ask of FAF's UI. The app places the handle and decides
-- what a gesture clicks (android/native/src/host/GriffAndroid.cpp); with the mouse scheme nothing is reported.

local Group = import('/lua/maui/group.lua').Group
local LayoutHelpers = import('/lua/maui/layouthelpers.lua')
local WorldView = import('/lua/ui/game/worldview.lua')
local CommandMode = import('/lua/ui/game/commandmode.lua')

--- A large selection's box comes from this many of its units, spread over all of them: near enough for a handle.
local SampledUnits = 64

--- What the handle stands for: the mobile units, which a route moves; a selection without any, such as factories,
--- whose route is their rally point, all of it.
local function Members(units)
    local mobile = EntityCategoryFilterDown(categories.MOBILE, units)
    if table.getn(mobile) > 0 then
        return mobile
    end
    return units
end

--- About how many of the picture's pixels one of the world's lengths takes on the screen at this place.
local function PixelsPerLength(view, place)
    local here = view:Project(place)
    local function Away(x, z)
        local there = view:Project(Vector(place[1] + x, place[2], place[3] + z))
        local dx, dy = there[1] - here[1], there[2] - here[2]
        return math.sqrt(dx * dx + dy * dy)
    end
    return math.max(Away(1, 0), Away(0, 1))
end

--- The selection's units on the screen, in the picture's pixels: their middle and the box they take, with their sizes (a
--- factory is large on the screen), and the map's view.
local function Report()
    local view = WorldView.viewLeft
    local units = view and GetSelectedUnits()
    if not units then
        FafreGriffBox(0)
        return
    end
    units = Members(units)
    local count = table.getn(units)
    local left, top = view.Left(), view.Top()
    local scale = PixelsPerLength(view, units[1]:GetPosition())
    local sumX, sumY, sampled = 0, 0, 0
    local minX, minY, maxX, maxY = 1e9, 1e9, -1e9, -1e9
    for at = 1, count, math.ceil(count / SampledUnits) do
        local unit = units[at]
        local place = view:Project(unit:GetPosition())
        local x, y = place[1] + left, place[2] + top
        local blueprint = unit:GetBlueprint()
        local half = 0.5 * scale * math.max(blueprint.SizeX or 1, blueprint.SizeY or 1, blueprint.SizeZ or 1)
        sumX, sumY, sampled = sumX + x, sumY + y, sampled + 1
        minX, maxX = math.min(minX, x - half), math.max(maxX, x + half)
        minY, maxY = math.min(minY, y - half), math.max(maxY, y + half)
    end
    FafreGriffBox(count, sumX / sampled, sumY / sampled, minX, minY, maxX, maxY, left, top, view.Right(), view.Bottom())
end

--- Each frame of the session's UI under `parent`, while the app wants it. An error stops it, with one warning.
function Start(parent)
    if not FafreGriffBox then
        return
    end
    local reporter = Group(parent)
    reporter:DisableHitTest()
    LayoutHelpers.AtLeftTopIn(reporter, parent)
    reporter.Width:Set(0)
    reporter.Height:Set(0)
    reporter.OnFrame = function(self, delta)
        if FafreGriffWanted() then
            local ok, problem = pcall(Report)
            if not ok then
                WARN('fafre_ui: griff: ' .. tostring(problem))
                FafreGriffBox(0)
                self:SetNeedsFrameUpdate(false)
            end
        end
    end
    reporter:SetNeedsFrameUpdate(true)
end

--- For a tap: 1 if a unit the player selects is under the cursor, their own, or any one an observer sees (a replay, a
--- defeat); plus 2 if anything is selected.
function TapKind()
    local info = GetRolloverInfo()
    local focus = GetFocusArmy()
    local kind = 0
    if info and info.armyIndex and (info.armyIndex + 1 == focus or (focus == -1 and info.userUnit)) then
        kind = kind + 1
    end
    if GetSelectedUnits() then
        kind = kind + 2
    end
    FafreGriffAnswer(kind)
end

--- The camera goes to the selection, as high and turned as it is.
function ToGroup()
    local units = GetSelectedUnits()
    if not units then
        return
    end
    units = Members(units)
    local x, y, z = 0, 0, 0
    for _, unit in units do
        local place = unit:GetPosition()
        x, y, z = x + place[1], y + place[2], z + place[3]
    end
    local count = table.getn(units)
    local camera = GetCamera('WorldCamera')
    local settings = camera:SaveSettings()
    settings.Focus = Vector(x / count, y / count, z / count)
    camera:RestoreSettings(settings)
end

--- Whether FAF's orders panel offers its attack-move for these units (orders.lua CreateCommonOrders): engineers, or mobile
--- units with a fire state. Factories alone get none, and FAF's attack-move fails on them in the sim.
local function OffersAttackMove(units)
    if not table.empty(EntityCategoryFilterDown(categories.ENGINEER - categories.POD, units)) then
        return true
    end
    if table.empty(EntityCategoryFilterDown(categories.MOBILE, units)) then
        return false
    end
    -- The orders alone: a for over the call itself would start at its third value, as Lua 5.0 iterates a table.
    local orders = GetUnitCommandData(units)
    for _, order in orders do
        if order == 'RULEUCC_RetaliateToggle' then
            return true
        end
    end
    return false
end

--- The rail's Attack: the next click on the map is FAF's attack-move there, as its orders panel's button makes it, where
--- that button is offered for the selection.
function StartAttackMove()
    local units = GetSelectedUnits()
    if units and OffersAttackMove(units) then
        CommandMode.StartCommandMode('order', {
            name = 'RULEUCC_Script',
            AbilityName = 'AttackMove',
            TaskName = 'AttackMove',
            Cursor = 'ATTACK_MOVE',
        })
    end
end

function EndCommandMode()
    CommandMode.EndCommandMode(true)
end

--- The app's Shift went up: FAF's check of each beat, at once, which ends a command mode that an order with Shift used
--- once Shift is up (commandmode.lua OnCommandModeBeat). A paused game has no beat, and the mode would wait for it.
function ShiftLetGo()
    CommandMode.OnCommandModeBeat()
end

function Deselect()
    SelectUnits(nil)
end
