-- What the touch scheme Griff shows of its own buttons and words (android/TOUCH_UI.md "Griff"), in FAF's own UI and look, at
-- the HUD's factor: the rail's Box, Plus and Groups, in a replay Time, and the undo's button in a row at the top left, and the
-- draft's tick and the cross, Griff's Esc, above the construction panel, each made as FAF's orders panel makes its buttons (the empty slot's frame, an icon of
-- FAF's own in it, FAF's glow pulsing under a finger and a selected order's edge while lit); the hints where FAF prints its
-- own messages (textdisplay.lua, as its PrintText); and the waypoint a resting finger marked for deletion, under FAF's own
-- Cancel icon (mousetooltips.lua). The activity keeps the fingers: one on Box, Plus, the undo's button, the tick or the
-- cross is its own, as a thumb may hold Plus while another finger taps (it lays a view of its own where this reports each,
-- FafreGriffHudPlaces), and it says how they look (FafreGriffHud). Groups and Time are FAF's clicks': a tap toggles each. Each piece
-- steps aside while something the player opened of FAF's lies over it (opened.lua), and all of them while the player hides
-- FAF's UI (gamemain.lua HideGameUI). What Griff shows on the map is griffmap.lua's. griff.lua makes the pieces with the
-- game's UI and runs Frame each frame.

local Bitmap = import('/lua/maui/bitmap.lua').Bitmap
local Group = import('/lua/maui/group.lua').Group
local StatusBar = import('/lua/maui/statusbar.lua').StatusBar
local LayoutHelpers = import('/lua/maui/layouthelpers.lua')
local UIUtil = import('/lua/ui/uiutil.lua')
local WorldView = import('/lua/ui/game/worldview.lua')

--- The row's place in FAF's pixels at the HUD's factor: the top an H under FAF's panels at the left edge (138 of its lines,
--- the multifunction panel's bottom), the left as far in as the multifunction panel's buttons; its buttons as far apart as
--- FAF's orders grid has them (cells of 50 for a bitmap of 48), the undo's button a gap of a quarter button after the rail.
--- The draft's tick and cross that gap above the construction panel and left of the column at the right (FAF's avatars),
--- and as far apart.
local TOP, LEFT, GAP, UNDO_GAP = 145, 10, 2, 12
--- A selected order's edge, cut from FAF's art of one (orders/move_btn_up_sel: its frame and its edge, this far in from
--- each side of its 48), laid over the button while it is lit.
local SELECTED = '/textures/ui/common/game/orders/move_btn_up_sel.dds'
local EDGE, SIDE = 7, 48
--- FAF's glow over an order button under the mouse (orders.lua CreateOrderGlow): it pulses up to its brightest and back at
--- this rate a second.
local GLOW, GLOW_RATE = 0.4, 1.2
--- An icon in a button as wide as FAF's order icons are in theirs, in FAF's pixels of its 48.
local ICON_WIDTH = 38
--- The construction panel's icon of the selection (its tabs' 56 x 32), and the part of it that is the glyph: left, top,
--- right and bottom in its pixels.
local SELECT_ICON = '/game/construction-tab_btn/units-select_icon_bmp.dds'
local SELECT_GLYPH = {11, 8, 46, 24}
local ICON_SIZE = {56, 32}
--- Groups shows the icons it shows and hides: the group icon's art (griffgroups.lua), FAF's strategic icon in one's own
--- colour, three of them together at the size the game draws a selected unit's (one alone in red would read as a stop sign).
local GROUP_ICON = '/textures/ui/common/game/strategicicons/icon_experimental_generic_selected.dds'
local GROUP_ICON_SIDE = 16
--- Where each of the three stands from the button's middle, in FAF's pixels.
local GROUP_ICONS = {{-8, 6}, {8, 6}, {0, -7}}
--- Time pins FAF's own timeline bar of a replay or lets it go, as its Ctrl-T does (replay_timeline.lua Toggle): the bar shows
--- only while the mouse is at the screen's bottom or Ctrl-T pins it, and a phone has no Ctrl. Its icon FAF's clock of a unit's
--- build time (unitview's), as large as FAF's order icons. A FAF without the bar (3840 and before) has no Time.
local TIMELINE = '/lua/ui/game/replay_timeline.lua'
local CLOCK_ICON = '/game/unit-over/icon-clock_large_bmp.dds'
--- The draft's tick, FAF's tick of the faction's skin (multifunction.lua's map options), and its cross, FAF's Cancel
--- (mousetooltips.lua), each as large as FAF's order icons; the tick's art is a small one.
local TICK_ICON = '/game/temp_textures/checkmark.dds'
local CROSS_ICON = '/game/orders-panel/no-parking_bmp.dds'
local TICK_SIDE, CROSS_SIDE = 30, 38

--- How the activity has Box and Plus (Rail.look, two bits each): idle, held while a finger is on it, lit for the next
--- action; and its bits for a finger on the tick, the cross and the undo's button.
local IDLE, HELD, LIT = 0, 1, 2
local TICK_HELD, CROSS_HELD, UNDO_HELD = 16, 32, 64
--- The undo's button as the activity has it (Game.HUD_UNDO_): none, offered, waiting for its undo.
local UNDO_NONE, UNDO_WAITING = 0, 2

--- What griff.lua's Hint says, by Game's HINT_ (GriffAndroid.cpp, GameActivity, the control groups' hook), with its number
--- where it counts: the mockup's words in the app's English.
local Words = {
    [1] = 'No free resource here',
    [2] = 'Taken',
    [3] = 'Draft is full',
    [4] = 'No plan yet',
    [5] = 'No engineer',
    [6] = 'All plans are on buildings',
    [7] = function(number) return number == 1 and '1 plan was on a building' or number .. ' plans were on buildings' end,
    [8] = 'Tick or cross the draft first',
    [9] = function(number) return number == 1 and '1 plan was not ordered' or number .. ' plans were not ordered' end,
    [10] = 'Empty group: rest to store',
    [11] = 'Nothing selected',
    [12] = function(number) return 'Group ' .. number .. ' stored' end,
    [13] = 'Cancelled',
    [14] = 'Undone in part',
    [15] = 'Not undone',
    [16] = 'Not deleted',
}
--- How FAF prints its own messages (userInit.lua PrintText): this size, white, in the screen's middle; a hint goes sooner.
local HINT_SIZE, HINT_SECONDS = 14, 2

--- The pieces, once Create made them: the row, its buttons by name, the undo's, the tick and the cross, the mark; and
--- whether Groups shows the group icons (griffgroups.lua).
local pieces = false
local groupsShown = true

--- A hint where FAF prints its own messages (textdisplay.lua PrintToScreen, which userInit.lua's PrintText calls), as a PC
--- player sees the game's own words: in the screen's middle, fading out as they do.
function Hint(kind, number)
    local words = Words[kind]
    if type(words) == 'function' then
        words = words(number or 0)
    end
    if not words then
        error('no words for hint ' .. tostring(kind))
    end
    import('/lua/ui/game/textdisplay.lua').PrintToScreen({
        text = words,
        size = HINT_SIZE,
        color = 'ffffffff',
        duration = HINT_SECONDS,
        location = 'center',
    })
end

--- Whether the group icons show (Groups), which griffgroups.lua asks each frame.
function GroupsShown()
    return groupsShown
end

--- A button of FAF's orders panel without an order of its own: the empty slot's frame, the glow FAF lays over an order button
--- under the mouse, and a selected order's edge while lit (cut from its art: the four sides of FAF's _btn_up_sel, its middle
--- left for the icon). Under a finger it takes the press for itself, so that nothing reaches the map beneath.
local function OrderButton(parent, name)
    local button = Bitmap(parent, UIUtil.SkinnableFile('/game/orders/basic-empty_bmp.dds'))
    button.name = name
    button.glow = Bitmap(button, UIUtil.UIFile('/game/orders/glow-02_bmp.dds'))
    LayoutHelpers.AtCenterIn(button.glow, button)
    button.glow:DisableHitTest()
    button.glow:SetAlpha(0)
    button.pulse = false
    button.edges = {}
    local cut = EDGE / SIDE
    -- Top, bottom, left and right: the part of the selected art (u0, v0, u1, v1) and where it lies in the button.
    for _, part in {{0, 0, 1, cut}, {0, 1 - cut, 1, 1}, {0, cut, cut, 1 - cut}, {1 - cut, cut, 1, 1 - cut}} do
        -- A local of each side's own for the layout's functions.
        local side = part
        local edge = Bitmap(button, SELECTED)
        edge:SetUV(side[1], side[2], side[3], side[4])
        edge.Left:Set(function() return button.Left() + side[1] * button.Width() end)
        edge.Top:Set(function() return button.Top() + side[2] * button.Height() end)
        edge.Width:Set(function() return (side[3] - side[1]) * button.Width() end)
        edge.Height:Set(function() return (side[4] - side[2]) * button.Height() end)
        edge:DisableHitTest()
        -- By its alpha, as the glow: showing the button shows what it holds.
        edge:SetAlpha(0)
        table.insert(button.edges, edge)
    end
    button.HandleEvent = function(self, event)
        return true
    end
    return button
end

--- How a button looks this frame: while a finger holds it FAF's glow pulsing, from nothing up to its brightest and back, as
--- FAF's over an order button under the mouse; while it is lit the selected order's edge.
local function Look(button, held, lit, delta)
    if held then
        local pulse = button.pulse or {alpha = 0, rising = true}
        local alpha = pulse.alpha + (pulse.rising and 1 or -1) * delta * GLOW_RATE
        if alpha > GLOW then
            alpha, pulse.rising = GLOW, false
        elseif alpha < 0 then
            alpha, pulse.rising = 0, true
        end
        pulse.alpha = alpha
        button.pulse = pulse
        button.glow:SetAlpha(alpha)
    elseif button.pulse then
        button.pulse = false
        button.glow:SetAlpha(0)
    end
    if button.lit ~= lit then
        button.lit = lit
        for _, edge in button.edges do
            edge:SetAlpha(lit and 1 or 0)
        end
    end
end

--- A texture of the faction's skin, or where its skin has none (the construction panel's tab icons are UEF's and Aeon's
--- alone), UEF's, as FAF's own mouse tooltips take its Select icon. Lazily, as UIUtil.SkinnableFile: the session sets the
--- player's skin only at its first update (gamemain.lua OnFirstUpdate), after the HUD is made.
local function SkinOrUef(spec)
    return function()
        local file = UIUtil.UIFile(spec)
        if DiskGetFileInfo(file) then
            return file
        end
        return '/textures/ui/uef/game' .. string.sub(spec, string.len('/game') + 1)
    end
end

--- An icon of FAF's own in a button: the glyph of a texture (`glyph`, in its pixels of `size`), as wide as FAF's order icons
--- are in theirs.
local function Icon(button, texture, glyph, size)
    local icon = Bitmap(button, texture)
    local left, top, right, bottom = unpack(glyph)
    icon:SetUV(left / size[1], top / size[2], right / size[1], bottom / size[2])
    LayoutHelpers.SetDimensions(icon, ICON_WIDTH, ICON_WIDTH * (bottom - top) / (right - left))
    LayoutHelpers.AtCenterIn(icon, button)
    icon:DisableHitTest()
    return icon
end

--- A whole texture of FAF's in a button, `side` of FAF's pixels each way.
local function WholeIcon(button, texture, side)
    local icon = Bitmap(button, texture)
    LayoutHelpers.SetDimensions(icon, side, side)
    LayoutHelpers.AtCenterIn(icon, button)
    icon:DisableHitTest()
    return icon
end

--- A label in FAF's font in a button, as FAF writes on its order buttons (orders.lua ButtonTextFunc): white with a shadow.
local function Label(button, text, size)
    local label = UIUtil.CreateText(button, text, size, UIUtil.bodyFont)
    label:SetColor('ffffffff')
    label:SetDropShadow(true)
    LayoutHelpers.AtCenterIn(label, button)
    label:DisableHitTest()
    return label
end

--- Time, in a replay of a FAF that has the timeline bar; false elsewhere. FAF's function is looked up at each press, as its key
--- binding does.
local function TimelineButton(row)
    if not SessionIsReplay() or not DiskGetFileInfo(TIMELINE) then
        return false
    end
    if type(import(TIMELINE).Toggle) ~= 'function' then
        error('this FAF\'s ' .. TIMELINE .. ' has no Toggle')
    end
    local button = OrderButton(row, 'timeline')
    button.icon = WholeIcon(button, UIUtil.SkinnableFile(CLOCK_ICON), ICON_WIDTH)
    button.HandleEvent = function(self, event)
        if event.Type == 'ButtonPress' or event.Type == 'ButtonDClick' then
            import(TIMELINE).Toggle()
            PlaySound(Sound({Bank = 'Interface', Cue = 'UI_Action_MouseDown'}))
        end
        return true
    end
    return button
end

--- The row (the user's decision of 2026-11-13: at the top left, where FAF's panels leave the map's edge free): Box, with FAF's
--- own icon for selecting (the construction panel's selection tab, FAF's Select of its mouse tooltips); Plus, Shift's +;
--- Groups, with the group icon it shows; in a replay Time, with FAF's clock; and after a gap the undo's button, with FAF's
--- avatar bar of the time it has left at its bottom, as the commander's avatar has its health. The draft's tick and cross,
--- placed each frame by the panels.
function Create(parent)
    local row = Group(parent)
    row:DisableHitTest()
    LayoutHelpers.AtLeftTopIn(row, parent, LEFT, TOP)
    row.Depth:Set(100)

    local box = OrderButton(row, 'box')
    LayoutHelpers.AtLeftTopIn(box, row)
    box.icon = Icon(box, SkinOrUef(SELECT_ICON), SELECT_GLYPH, ICON_SIZE)

    -- Plus is Shift, which FAF's icons have no picture of: its +, as FAF writes on its order buttons.
    local plus = OrderButton(row, 'plus')
    LayoutHelpers.RightOf(plus, box, GAP)
    plus.label = Label(plus, '+', 34)

    local groups = OrderButton(row, 'groups')
    LayoutHelpers.RightOf(groups, plus, GAP)
    groups.icons = {}
    for _, offset in GROUP_ICONS do
        local icon = Bitmap(groups, GROUP_ICON)
        LayoutHelpers.SetDimensions(icon, GROUP_ICON_SIDE, GROUP_ICON_SIDE)
        LayoutHelpers.AtCenterIn(icon, groups, offset[2], offset[1])
        icon:DisableHitTest()
        table.insert(groups.icons, icon)
    end
    groups.HandleEvent = function(self, event)
        if event.Type == 'ButtonPress' or event.Type == 'ButtonDClick' then
            groupsShown = not groupsShown
            PlaySound(Sound({Bank = 'Interface', Cue = 'UI_Action_MouseDown'}))
        end
        return true
    end

    -- A failure costs Time alone, not the rail.
    local made, timeline = pcall(TimelineButton, row)
    if not made then
        WARN('fafre_ui: griff: hud: the timeline\'s button: ' .. tostring(timeline))
        timeline = false
    end
    if timeline then
        LayoutHelpers.RightOf(timeline, groups, GAP)
    end

    local undo = OrderButton(row, 'undo')
    LayoutHelpers.RightOf(undo, timeline or groups, UNDO_GAP)
    undo.label = Label(undo, 'Undo', 12)
    LayoutHelpers.AtVerticalCenterIn(undo.label, undo, -3)
    undo.bar = StatusBar(undo, 0, 1, false, false, UIUtil.SkinnableFile('/game/avatar/health-bar-back_bmp.dds'),
        UIUtil.SkinnableFile('/game/avatar/health-bar.dds'), true, 'Griff undo time')
    LayoutHelpers.AtLeftIn(undo.bar, undo, 9)
    LayoutHelpers.AtRightBottomIn(undo.bar, undo, 9, 9)
    LayoutHelpers.SetHeight(undo.bar, 5)
    undo.bar.Width:Set(function() return undo.bar.Right() - undo.bar.Left() end)
    undo.bar:DisableHitTest(true)
    undo:Hide()

    -- What the row covers, which the tap help reads of every control.
    row.Width:Set(function() return undo.Right() - row.Left() end)
    row.Height:Set(box.Height)

    -- The draft's tick and the cross, placed by the panels each frame either shows (PlaceTickAndCross).
    local tick = OrderButton(parent, 'tick')
    tick.Depth:Set(100)
    LayoutHelpers.AtLeftTopIn(tick, parent)
    tick.icon = WholeIcon(tick, UIUtil.SkinnableFile(TICK_ICON), TICK_SIDE)
    tick:Hide()
    local cross = OrderButton(parent, 'cross')
    cross.Depth:Set(100)
    LayoutHelpers.AtLeftTopIn(cross, parent)
    cross.icon = WholeIcon(cross, UIUtil.SkinnableFile(CROSS_ICON), CROSS_SIDE)
    cross:Hide()

    local mark = Bitmap(parent, UIUtil.SkinnableFile('/game/orders-panel/no-parking_bmp.dds'))
    mark:DisableHitTest()
    mark.Depth:Set(99)
    mark.Left:Set(0)
    mark.Top:Set(0)
    mark:Hide()

    pieces = {row = row, box = box, plus = plus, groups = groups, timeline = timeline, undo = undo, tick = tick, cross = cross,
        mark = mark, said = {}, looks = 0, colour = false}
end

--- Whether a piece's box and one of the boxes the player opened (left, top, right and bottom of each) share more than an edge.
local function Under(piece, opened)
    local left, top, right, bottom = piece.Left(), piece.Top(), piece.Right(), piece.Bottom()
    for at = 1, table.getn(opened), 4 do
        if left < opened[at + 2] and opened[at] < right and top < opened[at + 3] and opened[at + 1] < bottom then
            return true
        end
    end
    return false
end

--- A piece shows where it is wanted and nothing the player opened lies over it; whether it steps aside.
local function Show(piece, wanted, opened)
    local aside = wanted and Under(piece, opened)
    if wanted and not aside then
        piece:Show()
    else
        piece:Hide()
    end
    return aside
end

--- The marked waypoint, under FAF's Cancel icon, while FAF's cursor hovers its very command: lifting the finger takes it out.
local function Mark(hidden)
    local mark = pieces.mark
    local x, y, z, hovered = FafreGriffMarked()
    local view = WorldView.viewLeft
    if hidden or not x or not hovered or not view then
        mark:Hide()
        return
    end
    local at = view:Project(Vector(x, y, z))
    mark.Left:Set(view.Left() + at[1] - mark.Width() / 2)
    mark.Top:Set(view.Top() + at[2] - mark.Height() / 2)
    mark:Show()
end

--- The draft's tick and the cross while either shows: the tick a gap above the construction panel's top (`anchors.top`,
--- the map's bottom where it does not show) and left of the column at the right (`anchors.left`, the map's right), the
--- cross a gap left of it, where it stands also while the tick does not show.
local function PlaceTickAndCross(anchors)
    local view = WorldView.viewLeft
    local tick, cross = pieces.tick, pieces.cross
    local gap = LayoutHelpers.ScaleNumber(UNDO_GAP)
    local bottom = anchors.top >= 0 and anchors.top or (view and view.Bottom() or 0)
    local right = anchors.left >= 0 and anchors.left or (view and view.Right() or 0)
    tick.Left:Set(right - gap - tick.Width())
    tick.Top:Set(bottom - gap - tick.Height())
    cross.Left:Set(tick.Left() - gap - cross.Width())
    cross.Top:Set(tick.Top())
end

--- Each frame of the session's UI: Box and Plus as the activity has them, Groups as it is, the undo's button with its time,
--- the draft's tick while the draft is open and the cross while Griff's Esc has something to put away (`anchors`: where the
--- construction panel and the column at the right begin, griff.lua DraftAnchors), each stepping aside from what the player
--- opened (`opened`, opened.lua's boxes) and all of
--- them while FAF's UI is hidden (`hidden`); Groups' icon in one's own icons' colour (`colour`, griffgroups.lua's); where
--- Box, Plus, the undo's button, the tick and the cross are, for the activity's fingers, where that changed; the marked
--- waypoint. `delta`: the frame's seconds, for the glow's pulse.
function Frame(opened, delta, anchors, hidden, colour)
    if not pieces then
        return
    end
    local looks, undoShown, undoLeft = FafreGriffHud()
    local boxLook, plusLook = math.mod(looks, 4), math.mod(math.floor(looks / 4), 4)
    local function Bit(value)
        return math.mod(math.floor(looks / value), 2) == 1
    end
    -- A finger came onto Box or Plus: FAF's sound of an order button's press.
    local wasBox, wasPlus = math.mod(pieces.looks, 4), math.mod(math.floor(pieces.looks / 4), 4)
    if (boxLook == HELD and wasBox ~= HELD) or (plusLook == HELD and wasPlus ~= HELD) then
        PlaySound(Sound({Bank = 'Interface', Cue = 'UI_Action_MouseDown'}))
    end
    pieces.looks = looks
    Look(pieces.box, boxLook == HELD, boxLook == LIT, delta)
    Look(pieces.plus, plusLook == HELD, plusLook == LIT, delta)
    Look(pieces.groups, false, groupsShown, delta)
    Look(pieces.undo, Bit(UNDO_HELD), false, delta)
    Look(pieces.tick, Bit(TICK_HELD), false, delta)
    Look(pieces.cross, Bit(CROSS_HELD), false, delta)
    if colour and colour ~= '' and pieces.colour ~= colour then
        for _, icon in pieces.groups.icons do
            icon:SetColorMask(colour)
        end
        pieces.colour = colour
    end
    local undo = pieces.undo
    undo.label:SetText(undoShown == UNDO_WAITING and 'Undo...' or 'Undo')
    undo.bar:SetValue(undoShown == UNDO_WAITING and 0 or undoLeft)
    local drafting = FafreGriffDraftPlans() >= 0
    -- The cross is Griff's Esc: it shows while the draft, an armed command mode or the selection is there to put away.
    local escapes = drafting or FafreGriffEscapes()
    if escapes then
        PlaceTickAndCross(anchors)
    end
    local shown = not hidden
    local places = {}
    for _, item in {{'box', true}, {'plus', true}, {'undo', undoShown ~= UNDO_NONE}, {'tick', drafting}, {'cross', escapes}} do
        local piece = pieces[item[1]]
        local wanted = shown and item[2]
        local aside = Show(piece, wanted, opened)
        table.insert(places, piece.Left())
        table.insert(places, piece.Top())
        table.insert(places, piece.Right())
        table.insert(places, piece.Bottom())
        -- Hidden with FAF's UI, a piece the activity knows is aside: its view takes no finger.
        table.insert(places, (aside or (hidden and item[2])) and 1 or 0)
    end
    Show(pieces.groups, shown, opened)
    if pieces.timeline then
        Show(pieces.timeline, shown, opened)
    end
    local same = table.getn(places) == table.getn(pieces.said)
    for at, value in places do
        same = same and value == pieces.said[at]
    end
    if not same then
        pieces.said = places
        FafreGriffHudPlaces(unpack(places))
    end
    Mark(hidden)
end
