-- What the touch scheme Griff shows on the map (android/TOUCH_UI.md "Griff"), in FAF's own look, each piece of SCFA's or
-- FAF's own art: the handle beside the selection, the selection's brackets as the game draws them about a selected unit
-- with the selection's count in FAF's font, and where its group is out of view or under a panel the arrow of FAF's ping off
-- the screen on its edge, pointing at it; the ring of a resting finger, a ring of FAF's pings by what its drag gives; a
-- stroke from the handle as FAF's command graph draws the route once it is ordered, its order line and its waypoints, and in
-- FAF's tooltip what lifting gives; the draft's plans as the ghosts of FAF's build preview; Box's box over deposits as FAF's
-- selection box; the loupe's frame, FAF's marker ring. The activity says where its pieces are (FafreGriffShown: the handle
-- as the screen keeps it, the ring, the stroke, the box, the loupe), the program what the rest found, where the plans stand
-- (FafreGriffPlans) and how large FAF draws a route (FafreGriffWaypoint). On the map's view, as FAF's reclaim labels are,
-- nothing of it takes an event, and the program's walk over FAF's controls passes it by. griff.lua runs Frame each frame.

local Bitmap = import('/lua/maui/bitmap.lua').Bitmap
local LayoutHelpers = import('/lua/maui/layouthelpers.lua')
local UIUtil = import('/lua/ui/uiutil.lua')
local WorldView = import('/lua/ui/game/worldview.lua')

--- Game.SHOWN_: what the activity says of its pieces, and the program of the rest (FafreGriffShown).
local SHOWN_HANDLE, SHOWN_RING, SHOWN_STROKE, SHOWN_BOX, SHOWN_LOUPE, SHOWN_REST = 0, 1, 2, 3, 4, 5

--- The handle, in FAF's pixels at the HUD's factor: as large as an order button (GameActivity.GRIFF_HANDLE_SIDE), its count
--- as FAF writes on its buttons; with Box lit it steps aside for a box that starts under it, faint. Off its group, the arrow
--- of FAF's ping off the screen (OffScreenMarkerIndicator.lua's blue preset), its middle on the handle's edge: the textures
--- by the way they point, as FAF's presets name them (t points down).
local HANDLE_SIDE = 48
local HANDLE_COUNT = 14
local BRACKETS = '/textures/ui/common/game/selection/selection_brackets_player.dds'
local ARROW = '/textures/ui/common/game/ping_edge/ping_edge_blue_'
local ARROW_SIDE = 52
local ARROWS = {'r', 'tr', 't', 'tl', 'l', 'bl', 'b', 'br'}
local FAINT = 0.35

--- A resting finger's ring, as large as two order buttons: FAF's ping rings (ping.lua), drawn in the glow pass as the world
--- view draws a ping's (worldview.lua DisplayPing), by the program's REST_ look of what the finger's drag gives: FAF's
--- formation the move ping's; the left button's drag, as a mouse's there (a box on the map, an armed mode's own drag, the
--- minimap's camera, a panel's), the marker ping's; the lift's deletion of a marked waypoint the attack ping's. It stays as
--- strong as a ping's ring as it begins, where FAF's fades as it grows: at this size on the phone's picture to about a fifth.
local RING_SIDE = 96
local Rings = {
    [1] = '/textures/ui/common/game/marker/ring_blue02-blur.dds',
    [2] = '/textures/ui/common/game/marker/ring_yellow02-blur.dds',
    [3] = '/textures/ui/common/game/marker/ring_red02-blur.dds',
}

--- The stroke: the route lifting gives, as FAF's command graph draws it once it is ordered (UICommandGraph): its order line
--- from where the stroke began through each waypoint, as wide as FAF's for the selection's count (FafreGriffWaypoint) in the
--- order's colour (commandgraphparams.lua) as its texture shows it (orderline/orderline_generic.dds: its middle half at the
--- texture's alpha, a brighter edge along either side of it); FAF's waypoint icon at each waypoint, in its selected colour
--- and as large as FAF draws one there for so many units, on the ground; in FAF's tooltip what lifting gives (tooltip.lua
--- CreateToolTip). FAF's Lua draws plain lines on the map (UI_DrawLine): the line runs straight from waypoint to waypoint,
--- where the command graph rounds its corners and ends it in its arrowhead, and a patrol's and an attack-move's are the same
--- band, where FAF's run as chevrons (orderline_arrow02.dds).
--- By Stroke.SAYS_, in the app's English as the mockup has it; the waypoints before the last are the route's (a patrol's,
--- an attack-move's, else moves).
local MOVE, ATTACK, ENGINEERING, NOTHING = '00ffff', 'ff0000', 'ffff00', 'ffffff'
--- The order line's selected alpha (dd) times its texture's, the band's 122 of 255 and the edge's 202; a stroke that lifting
--- gives up, half as strong.
local BAND, EDGE, BAND_GIVEN_UP, EDGE_GIVEN_UP = '6a', 'af', '35', '58'
--- Across the line's width: the band its middle half, its edges 0.18 of the width either side of the middle (the texture's
--- lie 0.19 and 0.17 from it), 0.05 of it thick.
local BAND_PART, EDGE_FROM, EDGE_PART = 0.5, 0.18, 0.05
--- FAF's waypoints in their selected colour (waypoint_selected_color 88ffffff).
local WAYPOINT_ALPHA = 0x88 / 255
local WAYPOINTS = '/textures/ui/common/game/waypoints/'
local SAYS_PATROL = 4
local Says = {
    [0] = {words = 'Route', colour = MOVE, icon = 'move_btn_up'},
    [1] = {words = 'Attack move', colour = ATTACK, icon = 'attack_move_btn_up'},
    [2] = {words = 'Cancel', colour = NOTHING},
    [3] = {words = 'Minimap', colour = MOVE, icon = 'move_btn_up'},
    [4] = {words = 'Patrol', colour = MOVE, icon = 'patrol_btn_up'},
    [5] = {words = 'Attack', colour = ATTACK, icon = 'attack_btn_up'},
    [6] = {words = 'Assist', colour = ENGINEERING, icon = 'guard_btn_up'},
    [7] = {words = 'Guard', colour = ENGINEERING, icon = 'guard_btn_up'},
    [8] = {words = 'Reclaim', colour = ENGINEERING, icon = 'reclaim_btn_up'},
    [9] = {words = 'Capture', colour = ENGINEERING, icon = 'convert_btn_up'},
    [10] = {words = 'Order', colour = MOVE, icon = 'move_btn_up'},
}

--- The draft's plans: the ghosts of FAF's build preview (CBuildDragPreview: the building's mesh through the UnitPlace shader)
--- in its colours for a place it takes and one it refuses (CWldSessionTypes.cpp). The shader draws every ghost at a fifth of
--- its colour, whatever alpha the colour has (mesh.fx UnitPlacePS), so plans differ by the colour alone, as FAF's own ghost
--- does beside a building it gains adjacency from: the newest plan, which a drag draws a line from, and the aim in FAF's
--- colours, as its ghost under the cursor; the other plans and a line's places, which have neither flag, at half of them.
--- The program's plan flags (FafreGriffPlans).
local PLAN_NEWEST, PLAN_TAKEN, PLAN_AIM = 1, 2, 8
local FREE, TAKEN, FREE_BEHIND, TAKEN_BEHIND = 'd800d800', 'd8d80000', 'd8006c00', 'd86c0000'
--- Far out the game draws a building's mesh still: its plans stay as long.
local GHOST_CUTOFF = 100000

--- Box's box over deposits: FAF's selection box as its world view draws a box drag (SelectionDragger2D::Render), a dark
--- fill and a light edge two of the picture's pixels wide about its border.
local BOX_FILL, BOX_EDGE, BOX_EDGE_HALF = '30000000', 'a0ffffff', 1

--- The loupe's frame: FAF's marker ring, its outer band about the activity's circle of the game's picture.
local LOUPE_RING = '/textures/ui/common/game/marker/ring_yellow02.dds'
local LOUPE_BAND = 0.8

--- The pieces, made on the map's view that shows them (FAF makes the view anew where it splits and joins it).
local pieces = false
local values = {}
local plans = {}
local ids = {}

local function Hide(control)
    if control and not control:IsHidden() then
        control:Hide()
    end
end

--- A bitmap of FAF's art (a texture, or with none a solid colour) on the view that takes no event.
local function Piece(view, texture, depth)
    local piece = Bitmap(view, texture)
    piece:DisableHitTest()
    piece.Depth:Set(function() return view.Depth() + depth end)
    piece:Hide()
    return piece
end

--- A piece's middle at a place of the picture.
local function Centre(piece, x, y)
    piece.Left:Set(x - piece.Width() / 2)
    piece.Top:Set(y - piece.Height() / 2)
end

--- The route's order line on the map, drawn each frame of the world view as FAF's shapes are (UI_DrawLine): from each of its
--- world places to the next, as wide as FAF's line at the wider end.
local function StrokeLine(view)
    local Shape = import('/lua/ui/game/shapes/shape.lua').Shape
    local Line = Class(Shape) {
        OnRender = function(self, delta)
            local route, widths = self.route, self.widths
            for at = 2, self.count do
                local from, to = route[at - 1], route[at]
                local width = math.max(widths[at - 1], widths[at])
                UI_DrawLine(from, to, self.band, BAND_PART * width)
                -- The texture's brighter edges, one on either side of its middle.
                local dx, dz = to[1] - from[1], to[3] - from[3]
                local length = math.sqrt(dx * dx + dz * dz)
                if length > 0 then
                    local x, z = -dz / length * EDGE_FROM * width, dx / length * EDGE_FROM * width
                    for side = -1, 1, 2 do
                        UI_DrawLine(Vector(from[1] + side * x, from[2], from[3] + side * z),
                            Vector(to[1] + side * x, to[2], to[3] + side * z), self.edge, EDGE_PART * width)
                    end
                end
            end
        end,
    }
    local line = Line(view)
    line.route = {}
    line.widths = {}
    line.count = 0
    line:Hide()
    return line
end

--- Box's box: FAF's fill, and its edge as four lines along the border.
local function SelectionBox(view)
    local box = Piece(view, nil, 29)
    box:SetSolidColor(BOX_FILL)
    box.edges = {}
    for side = 1, 4 do
        local edge = Piece(view, nil, 29)
        edge:SetSolidColor(BOX_EDGE)
        box.edges[side] = edge
    end
    return box
end

local function Make(view)
    local made = {view = view}
    made.handle = Piece(view, BRACKETS, 30)
    LayoutHelpers.SetDimensions(made.handle, HANDLE_SIDE, HANDLE_SIDE)
    made.count = UIUtil.CreateText(made.handle, '', HANDLE_COUNT, UIUtil.bodyFont)
    made.count:SetColor('ffffffff')
    made.count:SetDropShadow(true)
    made.count:DisableHitTest()
    LayoutHelpers.AtCenterIn(made.count, made.handle)
    made.arrow = Piece(view, ARROW .. 'r_up.dds', 30)
    LayoutHelpers.SetDimensions(made.arrow, ARROW_SIDE, ARROW_SIDE)
    made.ring = Piece(view, Rings[2], 31)
    LayoutHelpers.SetDimensions(made.ring, RING_SIDE, RING_SIDE)
    made.ring:SetRenderPass(UIUtil.UIRP_PostGlow)
    made.box = SelectionBox(view)
    made.loupe = Piece(view, LOUPE_RING, 32)
    made.icons = {}
    made.label = false
    local drawn, failure = pcall(StrokeLine, view)
    if drawn then
        made.line = failure
    else
        made.line = false
        WARN('fafre_ui: griff: map: no stroke line: ' .. tostring(failure))
    end
    made.ghosts = {}
    return made
end

local function Forget(made)
    -- The view destroyed its line with itself (its Trash, WorldViewShapeComponent AddShape): the line's Destroy after it would
    -- ask the view that is gone to stop rendering shapes.
    if made.line and not IsDestroyed(made.view) then
        made.line:Destroy()
    end
    for _, ghost in made.ghosts do
        if ghost then
            ghost.mesh:Destroy()
        end
    end
    local controls = {made.handle, made.arrow, made.ring, made.box, made.loupe, made.label}
    for _, edge in made.box.edges do
        table.insert(controls, edge)
    end
    for _, icon in made.icons do
        table.insert(controls, icon)
    end
    for _, control in controls do
        if control and not IsDestroyed(control) then
            control:Destroy()
        end
    end
end

local function HideHandle(made)
    Hide(made.handle)
    Hide(made.arrow)
end

--- The handle as the activity keeps it: its middle, its group's middle, the count, 1 where it points at its group from
--- elsewhere, 1 with Box lit (SHOWN_HANDLE); none where it has none.
local function Handle(made, hidden)
    local count = FafreGriffShown(SHOWN_HANDLE, values)
    if hidden or count < 7 then
        HideHandle(made)
        return 0
    end
    local x, y, groupX, groupY, units, off, faint = values[1], values[2], values[3], values[4], values[5], values[6], values[7]
    local handle = made.handle
    if made.units ~= units then
        made.count:SetText(tostring(units))
        made.units = units
    end
    Centre(handle, x, y)
    local alpha = faint == 1 and FAINT or 1
    handle:SetAlpha(alpha, true)
    handle:Show()
    local dx, dy = groupX - x, groupY - y
    local apart = math.sqrt(dx * dx + dy * dy)
    if off ~= 1 or apart < handle.Width() / 2 then
        Hide(made.arrow)
        return units
    end
    -- One of FAF's eight arrows, the nearest way to the group, its middle on the handle's edge that way.
    local way = math.mod(math.floor(math.atan2(dy, dx) / (math.pi / 4) + 8.5), 8) + 1
    if made.way ~= way then
        made.arrow:SetTexture(ARROW .. ARROWS[way] .. '_up.dds')
        made.way = way
    end
    local reach = handle.Width() / 2
    Centre(made.arrow, x + dx / apart * reach, y + dy / apart * reach)
    made.arrow:SetAlpha(alpha)
    made.arrow:Show()
    return units
end

--- A resting finger's ring (SHOWN_RING), in the look of what its drag gives, once the program has said it (SHOWN_REST).
local function Ring(made)
    local count = FafreGriffShown(SHOWN_RING, values)
    local x, y = values[1], values[2]
    local look = count >= 2 and FafreGriffShown(SHOWN_REST, values) >= 1 and Rings[values[1]]
    if not look then
        Hide(made.ring)
        return
    end
    if made.look ~= look then
        made.ring:SetTexture(look)
        made.look = look
    end
    Centre(made.ring, x, y)
    made.ring:Show()
end

--- FAF's tooltip of what lifting the stroke gives, made anew where its words change.
local function Label(made, words, x, y)
    local label = made.label
    if not label or label.words ~= words then
        if label then
            label:Destroy()
        end
        label = import('/lua/ui/game/tooltip.lua').CreateToolTip(made.view, words)
        label.words = words
        made.label = label
    end
    Centre(label, x, y)
    label:Show()
end

--- How long a world length from a place runs on the picture.
local function Across(view, place, x, z)
    local here = view:Project(place)
    local there = view:Project(Vector(place[1] + x, place[2], place[3] + z))
    local dx, dy = there[1] - here[1], there[2] - here[2]
    return math.sqrt(dx * dx + dy * dy)
end

local function HideStroke(made)
    if made.line then
        made.line:Hide()
    end
    Hide(made.label)
    for _, icon in made.icons do
        Hide(icon)
    end
end

--- The stroke (SHOWN_STROKE: what lifting gives, Stroke.SAYS_; whether FAF's attack-move is armed; the label's place; how many
--- points its route has; the route's points on the picture, where the stroke began and then each waypoint, the last where
--- the finger is): its order line through the ground under them, FAF's waypoint icon at each waypoint, its label.
local function Stroke(made, units)
    local count = FafreGriffShown(SHOWN_STROKE, values)
    local points = count >= 5 and values[5] or 0
    if points < 2 or count < 5 + 2 * points then
        HideStroke(made)
        return
    end
    local view = made.view
    local says = Says[values[1]] or Says[0]
    local attackMove = values[2] == 1
    local colour = (attackMove and values[1] == 3) and ATTACK or says.colour
    local route, widths = {}, {}
    local shown = 0
    for at = 1, points do
        local x, y = values[4 + 2 * at], values[5 + 2 * at]
        -- The camera's screen, the picture's pixels (GetMouseScreenPos's), as the activity says them.
        local place = UnProject(view, {x, y})
        local half, width = false, false
        if place and place[1] == place[1] and place[3] == place[3] then
            half, width = FafreGriffWaypoint(place[1], place[2], place[3], units)
        end
        if half then
            table.insert(route, place)
            table.insert(widths, width)
            -- FAF's waypoint for this one: the route's last a waypoint of the order lifting gives.
            local texture = at > 1 and says.icon
                and (at == points and says.icon or values[1] == SAYS_PATROL and 'patrol_btn_up'
                    or attackMove and 'attack_move_btn_up' or 'move_btn_up')
            if texture then
                shown = shown + 1
                local icon = made.icons[shown]
                texture = WAYPOINTS .. texture .. '.dds'
                if not icon then
                    icon = Piece(view, texture, 28)
                    icon:SetAlpha(WAYPOINT_ALPHA)
                    icon.texture = texture
                    made.icons[shown] = icon
                elseif icon.texture ~= texture then
                    icon:SetTexture(texture)
                    icon.texture = texture
                end
                -- A square on the ground, as FAF lays it: as wide across the picture as its side runs east, as high as it
                -- runs south.
                icon.Width:Set(2 * Across(view, place, half, 0))
                icon.Height:Set(2 * Across(view, place, 0, half))
                Centre(icon, x, y)
                icon:Show()
            end
        end
    end
    for at = shown + 1, table.getn(made.icons) do
        Hide(made.icons[at])
    end
    local line = made.line
    if line then
        line.route = route
        line.widths = widths
        line.count = table.getn(route)
        local givenUp = colour == NOTHING
        line.band = (givenUp and BAND_GIVEN_UP or BAND) .. colour
        line.edge = (givenUp and EDGE_GIVEN_UP or EDGE) .. colour
        line:Show()
    end
    Label(made, says.words, values[3], values[4])
end

local function HideBox(made)
    Hide(made.box)
    for _, edge in made.box.edges do
        Hide(edge)
    end
end

--- Box's box over deposits (SHOWN_BOX, its corners), as FAF's selection box: its fill, and its edge on the border.
local function Box(made)
    local count = FafreGriffShown(SHOWN_BOX, values)
    local box = made.box
    if count < 4 then
        HideBox(made)
        return
    end
    local left, top = math.min(values[1], values[3]), math.min(values[2], values[4])
    local right, bottom = math.max(values[1], values[3]), math.max(values[2], values[4])
    box.Left:Set(left)
    box.Top:Set(top)
    box.Width:Set(right - left)
    box.Height:Set(bottom - top)
    box:Show()
    local half = BOX_EDGE_HALF
    -- Top, bottom, left and right: left, top, right and bottom of each.
    for side, line in {{left - half, top - half, right + half, top + half}, {left - half, bottom - half, right + half, bottom + half},
            {left - half, top - half, left + half, bottom + half}, {right - half, top - half, right + half, bottom + half}} do
        local edge = box.edges[side]
        edge.Left:Set(line[1])
        edge.Top:Set(line[2])
        edge.Width:Set(line[3] - line[1])
        edge.Height:Set(line[4] - line[2])
        edge:Show()
    end
end

--- The loupe's frame (SHOWN_LOUPE: the middle of the activity's circle and its radius), its band about the circle.
local function Loupe(made)
    local count = FafreGriffShown(SHOWN_LOUPE, values)
    if count < 3 then
        Hide(made.loupe)
        return
    end
    local side = 2 * values[3] / LOUPE_BAND
    made.loupe.Width:Set(side)
    made.loupe.Height:Set(side)
    Centre(made.loupe, values[1], values[2])
    made.loupe:Show()
end

--- A plan's ghost: the mesh of its building (FafreGriffGhost: the mesh, its scale) through FAF's build preview's shader, at
--- its place in the world, in its colour. A building's mesh is set once: the ghost of another building is made anew.
local function Ghost(made, at, id, x, y, z, colour)
    local ghost = made.ghosts[at]
    if ghost and ghost.id ~= id then
        ghost.mesh:Destroy()
        ghost = false
        made.ghosts[at] = false
    end
    if not ghost then
        local mesh, scale = FafreGriffGhost(id)
        if not mesh then
            return
        end
        ghost = {id = id, mesh = import('/lua/ui/controls/worldmesh.lua').WorldMesh()}
        ghost.mesh:SetMesh({MeshName = mesh, TextureName = '', ShaderName = 'UnitPlace', UniformScale = scale,
            LODCutoff = GHOST_CUTOFF})
        made.ghosts[at] = ghost
    end
    ghost.mesh:SetStance(Vector(x, y, z))
    if ghost.colour ~= colour then
        ghost.mesh:SetColor(colour)
        ghost.colour = colour
    end
    if ghost.hidden ~= false then
        ghost.mesh:SetHidden(false)
        ghost.hidden = false
    end
end

--- The ghosts from the one at `from` on go out of sight.
local function HideGhosts(made, from)
    for at = from, table.getn(made.ghosts) do
        local ghost = made.ghosts[at]
        if ghost and not ghost.hidden then
            ghost.mesh:SetHidden(true)
            ghost.hidden = true
        end
    end
end

--- The draft's plans, a line's places and the aim, as the program has them (FafreGriffPlans: x, y, z and flags each, their
--- buildings' ids apart).
local function Plans(made)
    local count = FafreGriffPlans(plans, ids)
    for at = 1, count do
        local from = 4 * at - 4
        local flags = plans[from + 4]
        local taken = math.mod(math.floor(flags / PLAN_TAKEN), 2) == 1
        local colour
        if math.mod(flags, 2) == 1 or math.mod(math.floor(flags / PLAN_AIM), 2) == 1 then
            colour = taken and TAKEN or FREE
        else
            colour = taken and TAKEN_BEHIND or FREE_BEHIND
        end
        Ghost(made, at, ids[at], plans[from + 1], plans[from + 2], plans[from + 3], colour)
    end
    HideGhosts(made, count + 1)
end

--- The pieces that an error of their own stopped for the game, and how each goes out of sight then.
local stopped = {}
local Hiders = {
    handle = HideHandle,
    ring = function(made) Hide(made.ring) end,
    stroke = HideStroke,
    box = HideBox,
    loupe = function(made) Hide(made.loupe) end,
    plans = function(made) HideGhosts(made, 1) end,
}

--- One piece's frame, `draw` with the pieces and `with`, apart from the others: its first error stops it alone, says itself
--- once and hides it. What `draw` gives, false once it stopped.
local function Run(name, draw, made, with)
    if stopped[name] then
        return false
    end
    local ok, result = pcall(draw, made, with)
    if ok then
        return result
    end
    stopped[name] = true
    WARN('fafre_ui: griff: map: ' .. name .. ': ' .. tostring(result))
    pcall(Hiders[name], made)
    return false
end

--- Each frame of the session's UI: the pieces on the map's view, where the activity and the program have them, each apart
--- (Run); the handle not while FAF's UI is hidden, as the program places none then (griff.lua Report). False once the
--- handle cannot be drawn: griff.lua then reports no selection, so that the program places no handle for a finger either.
function Frame(hidden)
    local view = WorldView.viewLeft
    if pieces and pieces.view ~= view then
        Forget(pieces)
        pieces = false
    end
    if view then
        if not pieces then
            pieces = Make(view)
        end
        local units = Run('handle', Handle, pieces, hidden) or 0
        Run('ring', Ring, pieces)
        Run('stroke', Stroke, pieces, units)
        Run('box', Box, pieces)
        Run('loupe', Loupe, pieces)
        Run('plans', Plans, pieces)
    end
    return not stopped.handle
end
