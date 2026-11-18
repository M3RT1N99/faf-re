-- Griff, the app's touch scheme (buildstage/touch-concept-v2.md): where the selection is on the screen, for the handle
-- that the program places beside it; the camera's moves that its gestures make, as the UI moves the camera (a glide after a
-- pan, a flight to the group, a pinch), not the simulation; the few things its gestures ask of FAF's UI; the draft's part
-- that only FAF's UI knows: the build icon that opens it, its builders, the tick's build mode and selection; and what a
-- gesture orders, for the undo that takes it back with FAF's own inputs; and what the player opened of FAF's, which Griff's
-- pieces step aside from. All Griff shows is FAF's UI's, in FAF's look: its rail, its undo's button, the draft's tick and
-- cross, its hints and a waypoint marked for deletion griffhud.lua's; the handle, a resting finger's ring, a stroke, the
-- draft's plans, Box's box and the loupe's frame griffmap.lua's; its groups' icons griffgroups.lua's. The app places the
-- handle, holds the draft and the groups, decides what a gesture clicks and how an undo goes
-- (android/native/src/host/GriffAndroid.cpp); with the mouse scheme nothing is reported or watched and FAF's build icons
-- are FAF's.

local Group = import('/lua/maui/group.lua').Group
local LayoutHelpers = import('/lua/maui/layouthelpers.lua')
local Prefs = import('/lua/user/prefs.lua')
local WorldView = import('/lua/ui/game/worldview.lua')
local CommandMode = import('/lua/ui/game/commandmode.lua')
local Selection = import('/lua/ui/game/selection.lua')
local Opened = import('/fafre_ui/lua/fafre/opened.lua')
local Hud = import('/fafre_ui/lua/fafre/griffhud.lua')
local Groups = import('/fafre_ui/lua/fafre/griffgroups.lua')
local Map = import('/fafre_ui/lua/fafre/griffmap.lua')

--- A large selection's box comes from this many of its units, spread over all of them: near enough for a handle.
local SampledUnits = 64

--- FAF's attack-move mode, as its orders panel's button arms it (orders.lua AttackMoveBehavior): a new table each time, as
--- FAF writes into the mode's data when it ends.
local function AttackMoveMode()
    return {
        name = 'RULEUCC_Script',
        AbilityName = 'AttackMove',
        TaskName = 'AttackMove',
        Cursor = 'ATTACK_MOVE',
    }
end

--- The answers of Troops, OffersPatrol, CommitBegin and UndoBegin to the app, whose 0 is no answer.
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
local OtherEnd, EngineersSelected, PatrolOffered, AttackMoveArmedEnd = 7, 16, 32, 64

--- What the handle stands for: the mobile units, which a route moves; a selection without any, such as factories,
--- whose route is their rally point, all of it.
local function Members(units)
    local mobile = EntityCategoryFilterDown(categories.MOBILE, units)
    if table.getn(mobile) > 0 then
        return mobile
    end
    return units
end

--- How long a move of dx and dz on the map is.
local function Length(dx, dz)
    return math.sqrt(dx * dx + dz * dz)
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

--- The map's view on the screen, in the picture's pixels, as if nothing were selected: the app's free map, which its clicks
--- and its plans are on; Report then adds the selection, where it can. Nothing without a view.
local function ReportView()
    local view = WorldView.viewLeft
    if not view then
        FafreGriffBox(0)
        return
    end
    FafreGriffBox(0, 0, 0, 0, 0, 0, 0, view.Left(), view.Top(), view.Right(), view.Bottom())
end

--- The selection's units on the screen, in the picture's pixels: their middle and the box they take, with their sizes (a
--- factory is large on the screen), and the map's view again; with nothing selected the view's own report stands.
local function Report()
    local view = WorldView.viewLeft
    local units = view and GetSelectedUnits()
    if not units then
        return
    end
    local left, top, right, bottom = view.Left(), view.Top(), view.Right(), view.Bottom()
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
    local scale = PixelsPerLength(view, target)
    if Length(target[1] - x, target[3] - z) * scale < 1 then
        x, y, z = target[1], target[2], target[3]
        motion.flight = false
    end
    settings.Focus = Vector(x, y, z)
    local camera = Camera()
    camera:RestoreSettings(settings)
    -- Held by the map's edge, as FAF keeps its camera over the map: FAF moved it less than half the step asked. A step under a
    -- pixel says nothing of that, as from a frame's tiny delta, and so near the group the pixel's snap ends the flight.
    local asked = Length(x - focus[1], z - focus[3])
    local now = camera:SaveSettings().Focus
    if asked * scale >= 1 and Length(now[1] - focus[1], now[3] - focus[3]) < asked / 2 then
        motion.flight = false
    end
end

--- Each frame of the UI, after the camera's: the camera it drew, which UnProject shows until the next, for the pinch's moves.
--- None without a view or a camera: a pinch then does nothing rather than work from an old camera.
local function See()
    motion.seen = false
    local view = WorldView.viewLeft
    local camera = Camera()
    if view and camera then
        local settings = camera:SaveSettings()
        motion.seen = {focus = settings.Focus, zoom = settings.Zoom}
    end
end

--- Each frame of the UI, after the camera's: the glide's or the flight's next step.
local function Drive(delta)
    local view = WorldView.viewLeft
    local camera = Camera()
    if not view or not camera then
        return
    end
    local settings = camera:SaveSettings()
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

-- --- What a gesture orders: its undo -----------------------------------------------------------------------------------

--- The yes and no of an order's flags to the program.
local function Flag(value)
    return value and 1 or 0
end

--- An undo whose clicks run: the selection it puts back.
local undo = false

--- Whether FAF's capping is held off while the undo's clicks run (Start).
local cappingHeld = false

--- FAF's UI gave an order (commandmode.lua OnCommandIssued, before FAF's own handler; the engine has put it in its units'
--- queues already): the program hears of it, with its units by their ids (strings, which the game's Lua numbers would
--- round), whether it replaced what they did (FAF's Clear) and the entity it targets. The program reads their queues itself
--- and decides whether a watched gesture gave it, for its undo; it counts it for the clicks that run. FAF's by-product, None,
--- is none of them. FAF's own feedback blip marks its place (AddCommandFeedbackByType).
local function Ordered(command)
    local units = command.Units
    if command.CommandType == 'None' or not units or table.empty(units) then
        return
    end
    local params = command.LuaParams
    local attackMove = command.CommandType == 'Script' and params and params.TaskName == 'AttackMove' or false
    local ids = {}
    for _, unit in units do
        table.insert(ids, unit:GetEntityId())
    end
    local target = command.Target or {}
    FafreGriffOrdered(command.CommandType, Flag(attackMove), Flag(command.Clear), table.concat(ids, ','),
        target.Type == 'Entity' and tostring(target.EntityId or '') or '')
end

--- A hint of Griff's (Game's HINT_, with its number where it counts), where FAF prints its own messages (griffhud.lua).
function Hint(kind, number)
    Hud.Hint(kind, number)
end

--- The modes FAF's orders panel arms for an order the undo gives again, by the number of its way (include/GriffUndo.h Way).
local WayModes = {
    [1] = function() return {name = 'RULEUCC_Move'} end,
    [2] = AttackMoveMode,
    [3] = function() return {name = 'RULEUCC_Patrol'} end,
    [4] = function() return {name = 'RULEUCC_Attack'} end,
    [5] = function() return {name = 'RULEUCC_Guard'} end,
    [6] = function() return {name = 'RULEUCC_Repair'} end,
    [7] = function() return {name = 'RULEUCC_Reclaim'} end,
    [8] = function() return {name = 'RULEUCC_Capture'} end,
}

function Arm(way)
    local mode = WayModes[way]
    if mode then
        CommandMode.StartCommandMode('order', mode())
    end
end

--- The living units of a list of ids.
local function UnitsOf(ids)
    local units = {}
    for _, id in ids do
        local unit = GetUnitById(tostring(id))
        if unit and not unit:IsDead() then
            table.insert(units, unit)
        end
    end
    return units
end

--- The undo's clicks begin (the program's TryUndo): no command mode, which would take the release on a waypoint for its own
--- and keep FAF's cursor from the waypoints, and the selection kept for UndoEnd. It answers whether FAF's capping is held
--- off meanwhile (Start).
function UndoBegin()
    CommandMode.EndCommandMode(true)
    undo = {selection = GetSelectedUnits()}
    FafreGriffAnswer(cappingHeld and Yes or No)
end

--- Units whose orders the undo takes out or gives again, selected for its clicks.
function UndoSelect(ids)
    SelectUnits(UnitsOf(ids))
end

--- The undo is over: the mode its clicks left ended, and the selection the player had.
function UndoEnd()
    CommandMode.EndCommandMode(true)
    local u = undo
    undo = false
    if u then
        SelectUnits(u.selection)
    end
end

-- --- The groups ----------------------------------------------------------------------------------------------------------

--- A group by a tap on its icon (griffgroups.lua; the program's SelectGroup): its units are the selection (`how` 0), or join
--- it (1, as Shift does with FAF's control groups), or with Box (2) join it or, where all of them are selected, leave it, as
--- Box's tap does with one's own unit; with `camera`, the ladder's second tap, the camera goes to them as to a control group
--- selected twice, as the player's option selection_sets_double_tap_behavior has it (selection.lua DoubleTapBehavior, which
--- is FAF's own and local to its file).
function SelectGroup(ids, how, camera)
    local units = UnitsOf(ids)
    if table.empty(units) then
        return
    end
    if how == 2 then
        local selected = GetSelectedUnits() or {}
        local inGroup = {}
        for _, unit in units do
            inGroup[unit:GetEntityId()] = true
        end
        local others, all = {}, 0
        for _, unit in selected do
            if inGroup[unit:GetEntityId()] then
                all = all + 1
            else
                table.insert(others, unit)
            end
        end
        if all == table.getn(units) then
            SelectUnits(others)
        else
            AddSelectUnits(units)
        end
    elseif how == 1 then
        AddSelectUnits(units)
    else
        SelectUnits(units)
    end
    local behavior = Prefs.GetFromCurrentProfile('options.selection_sets_double_tap_behavior')
    if not camera or behavior == 'none' then
        return
    end
    StopCamera()
    local cam = Camera()
    local settings = cam:SaveSettings()
    UIZoomTo(units)
    if behavior == 'translate-zoom-out-only' then
        if cam:GetZoom() < settings.Zoom then
            cam:SetZoom(settings.Zoom, 0)
        end
    elseif behavior == 'translate' then
        cam:SetZoom(settings.Zoom, 0)
    end
    cam:RevertRotation()
end

--- The undo's button gives control group `group` back the units a rest on its slot stored over (the program's GroupBack; the
--- hook of FAF's controlgroups.lua): FAF's AddSelectionSet, as Ctrl and the number store a selection, whose callbacks show
--- its control. Those of them that live; answers whether any does.
function GroupBack(group, ids)
    local units = UnitsOf(ids)
    if table.empty(units) then
        FafreGriffAnswer(No)
        return
    end
    Selection.AddSelectionSet(tostring(group), units)
    FafreGriffAnswer(Yes)
end

-- --- The draft ------------------------------------------------------------------------------------------------------------

--- The draft's builders: the engineers selected when a building was picked, who build it if none is selected at the tick,
--- and those of the draft that ended last, which the undo's button may bring back; the selection the tick replaced; the
--- camera as it was before the tick moved it to a plan.
local draft = {builders = false, ended = false, before = false, camera = false}

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
--- mode puts its building in the app's draft instead (FafreGriffDraft), and FAF's ghost goes. Only a build item's click with
--- the left button: FAF's build mode keys and hotbuild arm the mode themselves, and a template's key (no button) and click (no
--- build item) come here too; all keep FAF's ghost. `item` is the button's Data, which FAF's own handler reads too: without it a
--- later FAF has renamed it, and the draft would never open without a word.
function BuildIconClicked(item, modifiers)
    if not FafreGriffWanted() then
        return
    end
    if not item then
        error('this FAF\'s build icon has no Data')
    end
    if item.type ~= 'item' or not modifiers or not modifiers.Left then
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

--- The draft ended (ticked, dropped, ended by an order mode): its builders are forgotten, but for the undo of a drop.
function DraftEnded()
    draft.ended = draft.builders
    draft.builders = false
end

--- The undo's button brought the dropped draft back (the program's DraftBack): its builders are the draft's again.
function DraftBack()
    draft.builders = draft.ended
    draft.ended = false
end

--- An order mode armed while the draft is open (FAF's orders panel, a key): with plans the draft comes first, by its tick or
--- cross, and nothing is armed; an empty draft ends and the mode stays. FAF's own start behaviours have run before this one,
--- as its identifier puts it after them.
function OrderWhileDrafting(mode, data)
    -- The undo's own modes, which give orders back, are none of the draft's.
    if mode ~= 'order' or not FafreGriffWanted() or undo then
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

--- Where the draft's tick and the cross stand (the mockup's: above FAF's construction panel, left of the column at the right):
--- the panel's top and the leftmost of the avatars and their idle lists, in the picture's pixels; -1 where not shown, for
--- griffhud.lua, which places them.
local function DraftAnchors()
    local top = -1
    -- FAF makes the panel with the game's UI: a later FAF that renamed it would leave the tick and the cross without their anchor.
    local construction = import('/lua/ui/game/construction.lua').controls.constructionGroup
    if not construction then
        error('this FAF has no controls.constructionGroup in construction.lua')
    end
    if not construction:IsHidden() then
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
    return {top = top, left = left}
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

--- A building's click comes next, the tick's or the undo's: FAF's build mode for it, as its construction panel arms it for a
--- build icon, the build templates cleared first. A template the player picked and put away by a right click stays the
--- session's (commandmode.lua clears it when a mode ends after its orders, not at a right click), and FAF's next build click
--- would build the template in place of the building.
function StartBuild(id)
    ClearBuildTemplates()
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

--- The camera of the program's clicks, the tick's or the undo's, to a place the free map does not show, as high and turned as
--- it is; with `closer` under 1, that much of its zoom as well (not past FAF's nearest), for a building whose cell no pixel
--- of the picture lands in. LookBack puts it where it was.
function LookAt(x, y, z, closer)
    StopCamera()
    local camera = Camera()
    if not draft.camera then
        draft.camera = camera:SaveSettings()
    end
    local settings = camera:SaveSettings()
    settings.Focus = Vector(x, y, z)
    if closer and closer < 1 then
        settings.Zoom = math.max(camera:GetMinZoom(), settings.Zoom * closer)
    end
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

--- What the player opened of FAF's that shows (opened.lua), as the program last heard it.
local openedSaid = {}

--- The program hears what the player opened of FAF's that shows, where it changed: the app's pieces step aside from it, as
--- do Griff's in FAF's UI (griffhud.lua).
local function ReportOpened()
    local boxes = Opened.Boxes()
    local same = table.getn(boxes) == table.getn(openedSaid)
    for index, value in boxes do
        same = same and value == openedSaid[index]
    end
    if not same then
        openedSaid = boxes
        FafreGriffOpened(unpack(boxes))
    end
end

--- Each frame of the session's UI under `parent`, while the app wants it: first the frame itself, counted for the program,
--- whose clicks wait for frames by it, then the rest. An error stops what failed, with one warning, never the count: the
--- camera's glide and flight stop apart from what the pinch sees of the camera, which goes on; the selection's report stops
--- apart from the map's view, which goes on each frame (an error there says itself once), so that the clicks and the plans
--- keep their map when the handle goes; the report of what the player opened stops with none, so that no piece of the app's
--- stays away for it; Griff's pieces in FAF's UI (griffhud.lua) stop apart, and so do its groups' icons (griffgroups.lua),
--- which leave none for a tap, and what it shows on the map (griffmap.lua, each piece apart), where a handle that cannot be
--- drawn stops the selection's report too, which leaves the program no handle to place. While the player hides FAF's UI
--- (gamemain.lua HideGameUI) Griff's buttons and icons hide with it, and the selection is not reported: the program places no
--- handle. FAF's UI tells each order it gives (OnCommandIssued, which the engine looks up at each order): the program hears of
--- it first. FAF's capping is held off while the undo's clicks run.
function Start(parent)
    if not FafreGriffBox then
        return
    end
    local reporter = Group(parent)
    reporter:DisableHitTest()
    LayoutHelpers.AtLeftTopIn(reporter, parent)
    reporter.Width:Set(0)
    reporter.Height:Set(0)
    local reporting, seeing, driving = true, true, true
    local anchoring, opening, showing, grouping, mapping = true, true, true, true, true
    local viewSaid = false
    local anchors = {top = -1, left = -1}
    local colour = false
    -- Griff's pieces in FAF's UI, on the map's group as FAF's own panels: in the game Griff plays only.
    if FafreGriffWanted() then
        local made, failure = pcall(Hud.Create, import('/lua/ui/game/borders.lua').GetMapGroup())
        if not made then
            WARN('fafre_ui: griff: hud: ' .. tostring(failure))
            showing = false
        end
    end
    reporter.OnFrame = function(self, delta)
        if not FafreGriffWanted() then
            return
        end
        FafreGriffFrame()
        local hidden = import('/lua/ui/game/gamemain.lua').gameUIHidden
        local viewed, failure = pcall(ReportView)
        if not viewed then
            if not viewSaid then
                WARN('fafre_ui: griff: view: ' .. tostring(failure))
                viewSaid = true
            end
            FafreGriffBox(0)
        end
        if reporting and not hidden then
            local ok, problem = pcall(Report)
            if not ok then
                WARN('fafre_ui: griff: ' .. tostring(problem))
                reporting = false
            end
        end
        -- Where the tick and the cross stand, while either shows: the draft's tick, and the cross, Griff's Esc.
        if anchoring and (FafreGriffDraftPlans() >= 0 or FafreGriffEscapes()) then
            local ok, found = pcall(DraftAnchors)
            if ok then
                anchors = found
            else
                WARN('fafre_ui: griff: draft: ' .. tostring(found))
                anchoring = false
            end
        end
        if opening then
            local ok, problem = pcall(ReportOpened)
            if not ok then
                WARN('fafre_ui: griff: opened: ' .. tostring(problem))
                FafreGriffOpened()
                openedSaid = {}
                opening = false
            end
        end
        if showing then
            local ok, problem = pcall(Hud.Frame, openedSaid, delta, anchors, hidden, colour)
            if not ok then
                WARN('fafre_ui: griff: hud: ' .. tostring(problem))
                showing = false
            end
        end
        if grouping then
            local ok, problem = pcall(Groups.Frame, hidden)
            if ok then
                colour = problem
            else
                WARN('fafre_ui: griff: groups: ' .. tostring(problem))
                -- No icon is left for a tap.
                pcall(FafreGriffGroups)
                grouping = false
            end
        end
        if mapping then
            local ok, handled = pcall(Map.Frame, hidden)
            if not ok then
                WARN('fafre_ui: griff: map: ' .. tostring(handled))
                mapping = false
            end
            -- A handle that FAF's UI cannot draw takes no finger: the selection goes unreported, so the program places none.
            if not (ok and handled) then
                reporting = false
            end
        end
        -- First what the frame drew, before a step moves the camera for the next.
        if seeing then
            local ok, problem = pcall(See)
            if not ok then
                WARN('fafre_ui: griff: camera seen: ' .. tostring(problem))
                motion.seen = false
                seeing = false
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
    end
    -- The game is over: what Griff set of FAF's goes back (FAF's reach for a unit beside a click), and the draft goes.
    reporter.OnDestroy = function(self)
        FafreGriffEnded()
    end
    reporter:SetNeedsFrameUpdate(true)
    CommandMode.AddStartBehavior(OrderWhileDrafting, 'fafre_griff')
    -- The orders FAF's UI gives, which the program hears of for the gestures' undo (Ordered). A FAF without
    -- OnCommandIssued costs those, and nothing else of Griff's.
    local watched, unwatched = pcall(function()
        local FafCommandIssued = CommandMode.OnCommandIssued
        if type(FafCommandIssued) ~= 'function' then
            error('no OnCommandIssued')
        end
        local watching = true
        CommandMode.OnCommandIssued = function(command)
            if watching and FafreGriffWanted() then
                local ok, problem = pcall(Ordered, command)
                if not ok then
                    WARN('fafre_ui: griff: orders: ' .. tostring(problem))
                    watching = false
                end
            end
            return FafCommandIssued(command)
        end
    end)
    if not watched then
        WARN('fafre_ui: griff: orders: ' .. tostring(unwatched))
    end
    -- FAF's capping answers a Guard of engineers on an extractor, a radar or an artillery structure (OnGuardIssued calls
    -- AssistToCap) with rings of buildings about it, by its options, the Shift held and its memory of the player's taps: to
    -- it a Guard the undo gives back with Shift is the player's next tap there, which may ring the structure with buildings
    -- the engineers never had, or ring nothing of what they had. The undo gives a Guard back as a PC player who wants only
    -- the Guard does, with FAF's capping options off; what the engineers held of FAF's rings it gives back by their own
    -- clicks. The rest of FAF's answer to the Guard (its assist-to-upgrade, its unpause) stays.
    local held, failure = pcall(function()
        local Capping = import('/lua/ui/game/hotkeys/capping.lua')
        local FafAssistToCap = Capping.AssistToCap
        if type(FafAssistToCap) ~= 'function' then
            error('no AssistToCap')
        end
        Capping.AssistToCap = function(structure, units)
            if undo then
                return
            end
            return FafAssistToCap(structure, units)
        end
    end)
    cappingHeld = held
    if not held then
        WARN('fafre_ui: griff: capping: ' .. tostring(failure))
    end
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

--- For a tap or a rest, where the cursor rests: 1 if a unit the player selects is under the cursor, one of the focus army's, as
--- the engine selects no other (CanSelectUnit: an observer without a focus army, as a replay begins or after a defeat, selects
--- none, by a PC's click as by a tap); 2 if anything is selected; 4 if any unit is under the cursor; 8 if the selection has
--- troops; 16 if FAF's right click there is a plain move; 32 if the selection has mobile units, which a formation moves.
function TapKind()
    local info = GetRolloverInfo()
    local focus = GetFocusArmy()
    local units = GetSelectedUnits()
    local view = WorldView.viewLeft
    local kind = 0
    if info and info.armyIndex and info.armyIndex + 1 == focus then
        kind = kind + 1
    end
    if units then
        kind = kind + 2
        if HasTroops(units) then
            kind = kind + 8
        end
        if not table.empty(EntityCategoryFilterDown(categories.MOBILE, units)) then
            kind = kind + 32
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

--- Whether the command mode armed is FAF's attack-move, as its orders panel's button arms it.
local function IsAttackMoveArmed()
    local mode = CommandMode.GetCommandMode()
    return mode[1] == 'order' and mode[2] and mode[2].TaskName == 'AttackMove' or false
end

--- For a route from the handle: whether FAF's attack-move is armed, whose waypoints it gives then.
function AttackMoveArmed()
    FafreGriffAnswer(IsAttackMoveArmed() and Yes or No)
end

--- For a stroke's label, where the cursor hovers at its end: what FAF's right click there orders (Stroke.END_), and whether
--- engineers are selected, FAF offers its patrol and its attack-move is armed.
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
    if IsAttackMoveArmed() then
        kind = kind + AttackMoveArmedEnd
    end
    FafreGriffAnswer(kind)
end

function OffersPatrol()
    local units = GetSelectedUnits()
    FafreGriffAnswer(units and OffersPatrolFor(units) and Yes or No)
end

--- For the handle's rest: whether FAF's Distribute orders, its key action G, deals out a queue with the cursor at this place of
--- the picture, the group's middle: FAF picks the selected non-factory unit nearest the world place under the cursor
--- (distribute-queue.lua DistributeOrders, by the same filter, the same distance and the same first pick), here two of them
--- at least, so that something is dealt out, and that unit's queue two orders or more, so that there is more than one order.
function CanDistribute(x, y)
    local units = GetSelectedUnits()
    local view = WorldView.viewLeft
    local place = view and UnProject(view, {x, y})
    local valid = units and EntityCategoryFilterOut(categories.FACTORY, units)
    local can = false
    if Valid(place) and valid and table.getn(valid) >= 2 then
        local nearest, shortest = valid[1], 4096 * 4096
        for _, unit in valid do
            local position = unit:GetPosition()
            local dx, dz = position[1] - place[1], position[3] - place[3]
            local distance = dx * dx + dz * dz
            if distance < shortest then
                nearest, shortest = unit, distance
            end
        end
        can = table.getn(nearest:GetCommandQueue() or {}) >= 2
    end
    FafreGriffAnswer(can and Yes or No)
end

--- The next click on the map is FAF's attack-move there, as its orders panel's button makes it, where that button is offered
--- for the selection: the tap ladder's attack-move in place of a move, and a route's next waypoint where FAF ended the mode.
function StartAttackMove()
    local units = GetSelectedUnits()
    if units and OffersAttackMove(units) then
        CommandMode.StartCommandMode('order', AttackMoveMode())
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
