-- What the touch scheme Griff shows (android/TOUCH_UI.md "Griff"), in FAF's own UI and look, at the HUD's factor: the rail's
-- Box, Plus and Groups and the undo's button in a row at the top left, made as FAF's orders panel makes its buttons (the
-- empty slot's frame, an icon of FAF's own in it, FAF's glow under a finger and its selected order's edge while lit); the
-- hints where FAF prints its own messages (textdisplay.lua, as its PrintText); and the waypoint a resting finger marked for
-- deletion, under FAF's own Cancel icon (mousetooltips.lua). The activity keeps the fingers: one on Box, Plus or the undo's
-- button is its own, as a thumb may hold Plus while another finger taps (it lays a view of its own where this reports each,
-- FafreGriffHudPlaces), and it says how they look (FafreGriffHud). Groups is FAF's click's: a tap toggles it. Each piece
-- steps aside while something the player opened of FAF's lies over it (opened.lua). griff.lua makes the pieces with the
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
local TOP, LEFT, GAP, UNDO_GAP = 145, 10, 2, 12
--- The selected order's edge (the orders' *_btn_up_sel): a line this thick, this far in from the button's edge.
local EDGE, EDGE_IN = 1, 5
--- FAF's glow over an order button under the mouse (orders.lua CreateOrderGlow, at its brightest).
local GLOW = 0.4
--- An icon in a button as wide as FAF's order icons are in theirs, in FAF's pixels of its 48.
local ICON_WIDTH = 38
--- The construction panel's icons of the selection and of attached units (its tabs' 56 x 32), and the part of each that is
--- the glyph: left, top, right and bottom in their pixels.
local SELECT_ICON = '/game/construction-tab_btn/units-select_icon_bmp.dds'
local ATTACHED_ICON = '/game/construction-tab_btn/units-attached_icon_bmp.dds'
local SELECT_GLYPH, ATTACHED_GLYPH = {11, 8, 46, 24}, {11, 10, 46, 25}
local ICON_SIZE = {56, 32}

--- How the activity has Box and Plus (Rail.look): idle, held while a finger is on it, lit for the next action.
local IDLE, HELD, LIT = 0, 1, 2
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

--- The pieces, once Create made them: the row, its buttons by name, the undo's, the mark; and whether Groups shows the group
--- icons (griffgroups.lua).
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

--- A button of FAF's orders panel without an order of its own: the empty slot's frame, the hover glow FAF lays over an
--- order button under the mouse, and the edge of a selected order (its _btn_up_sel), in the faction's colour, while lit.
--- Under a finger it takes the press for itself, so that nothing reaches the map beneath.
local function OrderButton(parent, name)
    local button = Bitmap(parent, UIUtil.SkinnableFile('/game/orders/basic-empty_bmp.dds'))
    button.name = name
    button.glow = Bitmap(button, UIUtil.UIFile('/game/orders/glow-02_bmp.dds'))
    LayoutHelpers.AtCenterIn(button.glow, button)
    button.glow:DisableHitTest()
    button.glow:SetAlpha(0)
    button.edges = {}
    for side = 1, 4 do
        local edge = Bitmap(button)
        edge:SetSolidColor(UIUtil.factionTextColor)
        edge:DisableHitTest()
        edge:SetAlpha(0)
        button.edges[side] = edge
    end
    local top, bottom, left, right = unpack(button.edges)
    LayoutHelpers.AtLeftTopIn(top, button, EDGE_IN, EDGE_IN)
    LayoutHelpers.AtRightIn(top, button, EDGE_IN)
    LayoutHelpers.SetHeight(top, EDGE)
    LayoutHelpers.AtLeftIn(bottom, button, EDGE_IN)
    LayoutHelpers.AtRightBottomIn(bottom, button, EDGE_IN, EDGE_IN)
    LayoutHelpers.SetHeight(bottom, EDGE)
    LayoutHelpers.AtLeftTopIn(left, button, EDGE_IN, EDGE_IN)
    LayoutHelpers.AtBottomIn(left, button, EDGE_IN)
    LayoutHelpers.SetWidth(left, EDGE)
    LayoutHelpers.AtRightIn(right, button, EDGE_IN)
    LayoutHelpers.AtTopIn(right, button, EDGE_IN)
    LayoutHelpers.AtBottomIn(right, button, EDGE_IN)
    LayoutHelpers.SetWidth(right, EDGE)
    button.HandleEvent = function(self, event)
        return true
    end
    return button
end

--- How a button looks: FAF's glow while a finger holds it, the selected order's edge while it is lit.
local function Look(button, held, lit)
    button.glow:SetAlpha(held and GLOW or 0)
    for _, edge in button.edges do
        edge:SetAlpha(lit and 1 or 0)
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
local function Icon(button, spec, glyph, size)
    local icon = Bitmap(button, SkinOrUef(spec))
    local left, top, right, bottom = unpack(glyph)
    icon:SetUV(left / size[1], top / size[2], right / size[1], bottom / size[2])
    LayoutHelpers.SetDimensions(icon, ICON_WIDTH, ICON_WIDTH * (bottom - top) / (right - left))
    LayoutHelpers.AtCenterIn(icon, button)
    icon:DisableHitTest()
    return icon
end

--- A label in FAF's font in a button, as FAF writes on its order buttons (orders.lua ButtonTextFunc): white with a shadow, or
--- in `color`.
local function Label(button, text, size, color)
    local label = UIUtil.CreateText(button, text, size, UIUtil.bodyFont)
    label:SetColor(color or 'ffffffff')
    label:SetDropShadow(true)
    LayoutHelpers.AtCenterIn(label, button)
    label:DisableHitTest()
    return label
end

--- The row (the user's decision of 2026-11-13: at the top left, where FAF's panels leave the map's edge free): Box, with FAF's
--- own icon for selecting (the construction panel's selection tab, FAF's Select of its mouse tooltips); Plus, Shift's +;
--- Groups, with the construction panel's icon of units together; and after a gap the undo's button, with FAF's avatar bar
--- of the time it has left at its bottom, as the commander's avatar has its health.
function Create(parent)
    local row = Group(parent)
    row:DisableHitTest()
    LayoutHelpers.AtLeftTopIn(row, parent, LEFT, TOP)
    row.Depth:Set(100)

    local box = OrderButton(row, 'box')
    LayoutHelpers.AtLeftTopIn(box, row)
    box.icon = Icon(box, SELECT_ICON, SELECT_GLYPH, ICON_SIZE)

    -- Plus is Shift, which FAF's icons have no picture of: its +, in the skin's colour of the icons beside it.
    local plus = OrderButton(row, 'plus')
    LayoutHelpers.RightOf(plus, box, GAP)
    plus.label = Label(plus, '+', 34, UIUtil.fontColor)

    local groups = OrderButton(row, 'groups')
    LayoutHelpers.RightOf(groups, plus, GAP)
    groups.icon = Icon(groups, ATTACHED_ICON, ATTACHED_GLYPH, ICON_SIZE)
    groups.HandleEvent = function(self, event)
        if event.Type == 'ButtonPress' or event.Type == 'ButtonDClick' then
            groupsShown = not groupsShown
            PlaySound(Sound({Bank = 'Interface', Cue = 'UI_Action_MouseDown'}))
        end
        return true
    end

    local undo = OrderButton(row, 'undo')
    LayoutHelpers.RightOf(undo, groups, UNDO_GAP)
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

    local mark = Bitmap(parent, UIUtil.SkinnableFile('/game/orders-panel/no-parking_bmp.dds'))
    mark:DisableHitTest()
    mark.Depth:Set(99)
    mark.Left:Set(0)
    mark.Top:Set(0)
    mark:Hide()

    pieces = {row = row, box = box, plus = plus, groups = groups, undo = undo, mark = mark, said = {}, looks = 0}
    Look(groups, false, groupsShown)
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
local function Mark()
    local mark = pieces.mark
    local x, y, z, hovered = FafreGriffMarked()
    local view = WorldView.viewLeft
    if not x or not hovered or not view then
        mark:Hide()
        return
    end
    local at = view:Project(Vector(x, y, z))
    mark.Left:Set(view.Left() + at[1] - mark.Width() / 2)
    mark.Top:Set(view.Top() + at[2] - mark.Height() / 2)
    mark:Show()
end

--- Each frame of the session's UI: Box and Plus as the activity has them, Groups as it is, the undo's button with its time,
--- each stepping aside from what the player opened (`opened`, opened.lua's boxes); where Box, Plus and the undo's button are,
--- for the activity's fingers, where that changed; the marked waypoint.
function Frame(opened)
    if not pieces then
        return
    end
    local looks, undoShown, undoLeft = FafreGriffHud()
    local boxLook, plusLook = math.mod(looks, 4), math.mod(math.floor(looks / 4), 4)
    -- A finger came onto Box or Plus: FAF's sound of an order button's press.
    local wasBox, wasPlus = math.mod(pieces.looks, 4), math.mod(math.floor(pieces.looks / 4), 4)
    if (boxLook == HELD and wasBox ~= HELD) or (plusLook == HELD and wasPlus ~= HELD) then
        PlaySound(Sound({Bank = 'Interface', Cue = 'UI_Action_MouseDown'}))
    end
    pieces.looks = looks
    Look(pieces.box, boxLook == HELD, boxLook == LIT)
    Look(pieces.plus, plusLook == HELD, plusLook == LIT)
    Look(pieces.groups, false, groupsShown)
    local undo = pieces.undo
    undo.label:SetText(undoShown == UNDO_WAITING and 'Undo...' or 'Undo')
    undo.bar:SetValue(undoShown == UNDO_WAITING and 0 or undoLeft)
    local places = {}
    for _, name in {'box', 'plus', 'undo'} do
        local piece = pieces[name]
        local aside = Show(piece, name ~= 'undo' or undoShown ~= UNDO_NONE, opened)
        table.insert(places, piece.Left())
        table.insert(places, piece.Top())
        table.insert(places, piece.Right())
        table.insert(places, piece.Bottom())
        table.insert(places, aside and 1 or 0)
    end
    Show(pieces.groups, true, opened)
    local same = table.getn(places) == table.getn(pieces.said)
    for at, value in places do
        same = same and value == pieces.said[at]
    end
    if not same then
        pieces.said = places
        FafreGriffHudPlaces(unpack(places))
    end
    Mark()
end
