-- Griff, the app's touch scheme (buildstage/touch-concept-v2.md): where the selection is on the screen, for the handle
-- that the app shows beside it; the camera's moves that its gestures make, as the UI moves the camera (a glide after a pan, a
-- flight to the group, a pinch), not the simulation; the few things its gestures ask of FAF's UI; and the draft's part that
-- only FAF's UI knows: the build icon that opens it, its builders, the tick's build mode and selection. The app places the
-- handle, holds the draft and decides what a gesture clicks (android/native/src/host/GriffAndroid.cpp); with the mouse scheme
-- nothing is reported and FAF's build icons are FAF's.

local Group = import('/lua/maui/group.lua').Group
local LayoutHelpers = import('/lua/maui/layouthelpers.lua')
local WorldView = import('/lua/ui/game/worldview.lua')
local CommandMode = import('/lua/ui/game/commandmode.lua')

--- A large selection's box comes from this many of its units, spread over all of them: near enough for a handle.
local SampledUnits = 64

--- The answers of Troops and OffersPatrol to the app, whose 0 is no answer.
local Yes, No = 2, 1

--- What a right click at a stroke's end orders, for its label (Stroke.END_): FAF's own orders by their names.
local StrokeEnds = {
    RULEUCC_Move = 1,
    RULEUCC_Attack = 2,
    RULEUCC_Guard = 3,
    RULEUCC_Repair = 4,
    RULEUCC_Reclaim = 5,
    RULEUCC_Capture = 6,
}
local OtherEnd, EngineersSelected, PatrolOffered = 7, 16, 32

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
--- factory is large on the screen), and the map's view, also with nothing selected (the app's free map).
local function Report()
    local view = WorldView.viewLeft
    if not view then
        FafreGriffBox(0)
        return
    end
    local left, top, right, bottom = view.Left(), view.Top(), view.Right(), view.Bottom()
    local units = GetSelectedUnits()
    if not units then
        FafreGriffBox(0, 0, 0, 0, 0, 0, 0, left, top, right, bottom)
        return
    end
    units = Members(units)
    local count = table.getn(units)
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
    FafreGriffBox(count, sumX / sampled, sumY / sampled, minX, minY, maxX, maxY, left, top, right, bottom)
end

-- --- The camera ---------------------------------------------------------------------------------------------------------

--- The camera as Griff's gestures move it (the mockup's camUpdate). `seen`: the camera the last frame drew, which UnProject
--- shows until the next; `glide`: after a pan, the map's velocity on the screen in the picture's pixels a second, slowing
--- by e^-6t until under `stop`; `placed`: where the glide put the focus, to tell another's move; `flight`: where it flies to
--- the group; `pinch`: what the fingers keep under them.
local motion = {seen = false, glide = false, placed = false, flight = false, pinch = false}

local function Valid(place)
    return place and place[1] == place[1] and place[3] == place[3]
end

local function Camera()
    return GetCamera('WorldCamera')
end

--- A finger on the map, or a new gesture of the camera: what moved it moves it no more.
function StopCamera()
    motion.glide = false
    motion.placed = false
    motion.flight = false
end

--- After a pan the map glides on at this velocity in the picture's pixels a second (the finger's), on the system's clock:
--- a paused game glides too.
function Glide(x, y, stop)
    StopCamera()
    motion.glide = {x = x, y = y, stop = math.max(stop, 1)}
end

local function GlideStep(view, settings, delta)
    local glide = motion.glide
    local focus = settings.Focus
    -- Moved since by another, a key or the minimap, or held by the map's edge (FAF keeps its camera over the map): over.
    local placed = motion.placed
    if placed and (math.abs(focus[1] - placed[1]) > 0.05 or math.abs(focus[3] - placed[3]) > 0.05) then
        StopCamera()
        return
    end
    -- The focus moves by the ground the map's velocity takes past the view's middle, at the rate of FAF's own pan, which it
    -- goes on from: the ground's breadth a pixel across the middle for either way (CameraPan), though the tilted camera shows
    -- more ground a pixel up and down.
    local x, y = (view.Left() + view.Right()) / 2, (view.Top() + view.Bottom()) / 2
    local here = UnProject(view, {x, y})
    local across = UnProject(view, {x + 100, y})
    local down = UnProject(view, {x, y + 100})
    if not Valid(here) or not Valid(across) or not Valid(down) then
        StopCamera()
        return
    end
    local wide = math.sqrt((across[1] - here[1]) * (across[1] - here[1]) + (across[3] - here[3]) * (across[3] - here[3]))
    local deep = math.sqrt((down[1] - here[1]) * (down[1] - here[1]) + (down[3] - here[3]) * (down[3] - here[3]))
    local there = UnProject(view, {x - glide.x * delta, y - glide.y * delta * wide / math.max(deep, 1e-3)})
    if not Valid(there) then
        StopCamera()
        return
    end
    settings.Focus = Vector(focus[1] + there[1] - here[1], focus[2] + there[2] - here[2], focus[3] + there[3] - here[3])
    local camera = Camera()
    camera:RestoreSettings(settings)
    motion.placed = camera:SaveSettings().Focus
    local kept = math.exp(-6 * delta)
    glide.x, glide.y = glide.x * kept, glide.y * kept
    if glide.x * glide.x + glide.y * glide.y < glide.stop * glide.stop then
        StopCamera()
    end
end

--- The flight to the group (the mockup's camFly): 1 - e^-14t of the way each frame, there within a pixel; or where the map's
--- edge holds the camera.
local function FlightStep(view, settings, delta)
    local target = motion.flight
    local focus = settings.Focus
    local part = 1 - math.exp(-14 * delta)
    local x = focus[1] + (target[1] - focus[1]) * part
    local y = focus[2] + (target[2] - focus[2]) * part
    local z = focus[3] + (target[3] - focus[3]) * part
    local dx, dz = target[1] - x, target[3] - z
    if math.sqrt(dx * dx + dz * dz) * PixelsPerLength(view, target) < 1 then
        x, y, z = target[1], target[2], target[3]
        motion.flight = false
    end
    settings.Focus = Vector(x, y, z)
    local camera = Camera()
    camera:RestoreSettings(settings)
    local now = camera:SaveSettings().Focus
    if math.abs(now[1] - focus[1]) + math.abs(now[3] - focus[3]) < 0.01 then
        motion.flight = false
    end
end

--- Each frame of the UI, after the camera's: what it drew, and the glide's or the flight's next step.
local function Drive(delta)
    local view = WorldView.viewLeft
    local camera = Camera()
    if not view or not camera then
        return
    end
    local settings = camera:SaveSettings()
    motion.seen = {focus = settings.Focus, zoom = settings.Zoom}
    if motion.glide then
        GlideStep(view, settings, delta)
    elseif motion.flight then
        FlightStep(view, settings, delta)
    end
end

--- Two fingers, or the one left of them, keep what lies under them at this place (the picture's pixels): its world point,
--- the zoom as it is and how far apart the two are (0 for one).
function PinchStart(x, y, distance)
    StopCamera()
    local view = WorldView.viewLeft
    local camera = Camera()
    local anchor = view and UnProject(view, {x, y})
    if not camera or not Valid(anchor) then
        motion.pinch = false
        return
    end
    local zoom = motion.pinch and motion.pinch.last or (motion.seen and motion.seen.zoom) or camera:SaveSettings().Zoom
    motion.pinch = {anchor = anchor, zoom = zoom, distance = distance, last = zoom}
end

--- The fingers are here now, `distance` apart (0: one finger): the zoom by how far apart they are from where they began, and
--- the focus such that the world point they began on is under them again. Measured on the camera the last frame drew (its
--- focus and zoom, and UnProject's picture), so that it holds however many moves come in a frame: a move of the focus moves
--- what is under a place by as much, and a zoom about the focus by its factor.
function Pinch(x, y, distance)
    local pinch = motion.pinch
    local view = WorldView.viewLeft
    local camera = Camera()
    local seen = motion.seen
    if not pinch or not view or not camera or not seen then
        return
    end
    local zoom = pinch.zoom
    if distance > 0 and pinch.distance > 0 then
        zoom = math.max(camera:GetMinZoom(), math.min(camera:GetMaxZoom(), pinch.zoom * pinch.distance / distance))
    end
    local under = UnProject(view, {x, y})
    if not Valid(under) then
        return
    end
    local scale = zoom / seen.zoom
    local anchor, focus = pinch.anchor, seen.focus
    local settings = camera:SaveSettings()
    settings.Focus = Vector(anchor[1] - (under[1] - focus[1]) * scale, focus[2], anchor[3] - (under[3] - focus[3]) * scale)
    settings.Zoom = zoom
    camera:RestoreSettings(settings)
    pinch.last = zoom
end

function PinchEnd()
    motion.pinch = false
end

--- The camera goes to the selection, as high and turned as it is: by a flight (fly 1), else at once.
function ToGroup(fly)
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
    StopCamera()
    local target = Vector(x / count, y / count, z / count)
    if fly == 1 then
        motion.flight = target
        return
    end
    local camera = Camera()
    local settings = camera:SaveSettings()
    settings.Focus = target
    camera:RestoreSettings(settings)
end

-- --- The draft ------------------------------------------------------------------------------------------------------------

--- The draft's builders: the engineers selected when a building was picked, who build it if none is selected at the tick;
--- the selection the tick replaced; the camera as it was before the tick moved it to a plan.
local draft = {builders = false, before = false, camera = false}

--- Whether two lists hold the same units.
local function SameUnits(units, others)
    if table.getn(units) ~= table.getn(others) then
        return false
    end
    local ids = {}
    for _, unit in units do
        ids[unit:GetEntityId()] = true
    end
    for _, unit in others do
        if not ids[unit:GetEntityId()] then
            return false
        end
    end
    return true
end

--- A button of FAF's construction panel was clicked (its hook, after FAF's own handler): a build icon that started FAF's build
--- mode puts its building in the app's draft instead (FafreGriffDraft), and FAF's ghost goes. Only the panel's own click, the
--- left button's: FAF's hotbuild keys and its templates keep FAF's ghost.
function BuildIconClicked(item, modifiers)
    if not FafreGriffWanted() or not item or item.type ~= 'item' or not modifiers or not modifiers.Left then
        return
    end
    local mode = CommandMode.GetCommandMode()
    if mode[1] ~= 'build' or not mode[2] or mode[2].name ~= item.id then
        return
    end
    CommandMode.EndCommandMode(true)
    local units = GetSelectedUnits()
    local engineers = units and EntityCategoryFilterDown(categories.ENGINEER, units)
    if engineers and not table.empty(engineers) then
        draft.builders = engineers
    end
    FafreGriffDraft(item.id)
end

--- The draft ended (ticked, dropped, ended by an order mode): its builders are forgotten.
function DraftEnded()
    draft.builders = false
end

--- An order mode armed while the draft is open (FAF's orders panel, a key): with plans the draft comes first, by its tick or
--- cross, and nothing is armed; an empty draft ends and the mode stays. FAF's own start behaviours have run before this one,
--- as its identifier puts it after them.
function OrderWhileDrafting(mode, data)
    if mode ~= 'order' or not FafreGriffWanted() then
        return
    end
    local plans = FafreGriffDraftPlans()
    if plans > 0 then
        CommandMode.EndCommandMode(true)
        FafreGriffDraftRefused()
    elseif plans == 0 then
        FafreGriffDraftEnd()
        DraftEnded()
    end
end

--- Where the draft's tick and cross stand (the mockup's: above FAF's construction panel, left of the column at the right):
--- the panel's top and the leftmost of the avatars and their idle lists, in the picture's pixels; -1 where not shown.
function DraftAnchors()
    local top = -1
    local construction = import('/lua/ui/game/construction.lua').controls.constructionGroup
    if construction and not construction:IsHidden() then
        top = construction.Top()
    end
    local left = -1
    local avatars = import('/lua/ui/game/avatars.lua').controls
    local function Column(control)
        if control and not control:IsHidden() then
            local at = control.Left()
            if left < 0 or at < left then
                left = at
            end
        end
    end
    for _, avatar in avatars.avatars or {} do
        Column(avatar)
    end
    Column(avatars.idleEngineers)
    Column(avatars.idleFactories)
    FafreGriffDraftAnchors(top, left)
end

--- The tick begins (the program's DraftCommitTask): its builders are the selected engineers, or else the draft's that still
--- live, and they are selected for FAF's build clicks; the selection the player had comes back at CommitEnd. Answers whether
--- there are any. Everything the tick orders comes between this and CommitEnd.
function CommitBegin()
    local units = GetSelectedUnits() or {}
    local builders = EntityCategoryFilterDown(categories.ENGINEER, units)
    if table.empty(builders) then
        builders = {}
        for _, unit in draft.builders or {} do
            if not IsDestroyed(unit) and not unit:IsDead() then
                table.insert(builders, unit)
            end
        end
    end
    if table.empty(builders) then
        FafreGriffAnswer(No)
        return
    end
    draft.before = false
    if not SameUnits(units, builders) then
        draft.before = units
        SelectUnits(builders)
    end
    FafreGriffAnswer(Yes)
end

--- A plan's click comes next: FAF's build mode for its building, as its construction panel arms it.
function StartBuild(id)
    CommandMode.StartCommandMode('build', {name = id})
end

--- The tick is over: the build mode it left ends, and the selection the player had comes back.
function CommitEnd()
    CommandMode.EndCommandMode(true)
    local before = draft.before
    draft.before = false
    if before then
        SelectUnits(before)
    end
end

--- The tick's camera to a plan the free map does not show, as high and turned as it is; LookBack puts it where it was.
function LookAt(x, y, z)
    StopCamera()
    local camera = Camera()
    if not draft.camera then
        draft.camera = camera:SaveSettings()
    end
    local settings = camera:SaveSettings()
    settings.Focus = Vector(x, y, z)
    camera:RestoreSettings(settings)
end

function LookBack()
    local saved = draft.camera
    draft.camera = false
    if saved then
        StopCamera()
        Camera():RestoreSettings(saved)
    end
end

-- --- The session's UI, each frame -----------------------------------------------------------------------------------------

--- Each frame of the session's UI under `parent`, while the app wants it. An error stops what failed, with one warning.
function Start(parent)
    if not FafreGriffBox then
        return
    end
    local reporter = Group(parent)
    reporter:DisableHitTest()
    LayoutHelpers.AtLeftTopIn(reporter, parent)
    reporter.Width:Set(0)
    reporter.Height:Set(0)
    local reporting, driving = true, true
    local anchoring = true
    reporter.OnFrame = function(self, delta)
        if not FafreGriffWanted() then
            return
        end
        if reporting then
            local ok, problem = pcall(Report)
            if not ok then
                WARN('fafre_ui: griff: ' .. tostring(problem))
                FafreGriffBox(0)
                reporting = false
            end
        end
        if anchoring and FafreGriffDraftPlans() >= 0 then
            local ok, problem = pcall(DraftAnchors)
            if not ok then
                WARN('fafre_ui: griff: draft: ' .. tostring(problem))
                anchoring = false
            end
        end
        if driving then
            local ok, problem = pcall(Drive, delta)
            if not ok then
                WARN('fafre_ui: griff: camera: ' .. tostring(problem))
                StopCamera()
                driving = false
            end
        end
        if not reporting and not driving then
            self:SetNeedsFrameUpdate(false)
        end
    end
    -- The game is over: what Griff set of FAF's goes back (FAF's reach for a unit beside a click), and the draft goes.
    reporter.OnDestroy = function(self)
        FafreGriffEnded()
    end
    reporter:SetNeedsFrameUpdate(true)
    CommandMode.AddStartBehavior(OrderWhileDrafting, 'fafre_griff')
end

-- --- What a gesture asks ----------------------------------------------------------------------------------------------------

--- Whether the selection takes orders at a place: mobile units, or factories alone (their rally point), as the mockup's
--- troops are.
local function HasTroops(units)
    if not table.empty(EntityCategoryFilterDown(categories.MOBILE, units)) then
        return true
    end
    return table.getn(EntityCategoryFilterDown(categories.FACTORY, units)) == table.getn(units)
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

--- Whether FAF's orders panel offers its patrol for a loop of these units: mobile ones that can patrol. Factories alone have
--- their own patrol of the units they make, which a loop is not: theirs is their rally route.
local function OffersPatrolFor(units)
    if table.empty(EntityCategoryFilterDown(categories.MOBILE, units)) then
        return false
    end
    local orders = GetUnitCommandData(units)
    for _, order in orders do
        if order == 'RULEUCC_Patrol' then
            return true
        end
    end
    return false
end

--- For a tap, where the cursor rests: 1 if a unit the player selects is under the cursor, their own, or any one an observer
--- sees (a replay, a defeat); 2 if anything is selected; 4 if any unit is under the cursor; 8 if the selection has troops;
--- 16 if FAF's right click there is a plain move.
function TapKind()
    local info = GetRolloverInfo()
    local focus = GetFocusArmy()
    local units = GetSelectedUnits()
    local view = WorldView.viewLeft
    local kind = 0
    if info and info.armyIndex and (info.armyIndex + 1 == focus or (focus == -1 and info.userUnit)) then
        kind = kind + 1
    end
    if units then
        kind = kind + 2
        if HasTroops(units) then
            kind = kind + 8
        end
        if view and view:GetRightMouseButtonOrder() == 'RULEUCC_Move' then
            kind = kind + 16
        end
    end
    if info then
        kind = kind + 4
    end
    FafreGriffAnswer(kind)
end

--- Whether the selection has troops, for FAF's reach for a unit beside a tap.
function Troops()
    local units = GetSelectedUnits()
    FafreGriffAnswer(units and HasTroops(units) and Yes or No)
end

--- For a stroke's label, where the cursor hovers at its end: what FAF's right click there orders (Stroke.END_), and whether
--- engineers are selected and FAF offers its patrol.
function StrokeKind()
    local units = GetSelectedUnits()
    local view = WorldView.viewLeft
    local order = units and view and view:GetRightMouseButtonOrder()
    local kind = StrokeEnds[order or 'RULEUCC_Move'] or OtherEnd
    if units and not table.empty(EntityCategoryFilterDown(categories.ENGINEER, units)) then
        kind = kind + EngineersSelected
    end
    if units and OffersPatrolFor(units) then
        kind = kind + PatrolOffered
    end
    FafreGriffAnswer(kind)
end

function OffersPatrol()
    local units = GetSelectedUnits()
    FafreGriffAnswer(units and OffersPatrolFor(units) and Yes or No)
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

--- A loop from the handle: the next click on the map is FAF's patrol there, as its orders panel's button makes it.
function StartPatrol()
    local units = GetSelectedUnits()
    if units and OffersPatrolFor(units) then
        CommandMode.StartCommandMode('order', {name = 'RULEUCC_Patrol'})
    end
end

--- The tap ladder's second rung on a unit: all of the selected unit's type on the screen, as FAF's double click on it, by its
--- key action select_all_onscreen for the type's own category (the engine gives each unit's blueprint id one).
function SelectTypeInView()
    local units = GetSelectedUnits()
    if units then
        ConExecute('UI_SelectByCategory +inview ' .. units[1]:GetBlueprint().BlueprintId)
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
