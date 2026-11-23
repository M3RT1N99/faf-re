-- The bar (android/TOUCH_UI.md): what a mouse and a keyboard have and fingers have not, as buttons in FAF's UI over the rest
-- of it. In the menus the phone's keyboard (Keys); in a game with the touch scheme that is a finger as the mouse also Order (the
-- left and right buttons change places), Shift, Ctrl and Del (a tap deletes the queued order under it), each on or off, which
-- hold while the game has the fingers (FafreTouchBar). Griff has no bar in a game: its rail is griffhud.lua's, and its keyboard
-- FAF's Menu tab's. A finger on the bar is the touch layer's, as on any of FAF's buttons (android/native/src/host/FingersSdl.cpp).
-- A page the layer shows larger (FafreView) zooms FAF's UI and the bar with it: the bar undoes the view, so that it stays where
-- it is on the glass and as large. Without the touch layer, as on a desktop, there is no bar.

local Bitmap = import('/lua/maui/bitmap.lua').Bitmap
local Group = import('/lua/maui/group.lua').Group
local LayoutHelpers = import('/lua/maui/layouthelpers.lua')
local LazyVar = import('/lua/lazyvar.lua')
local UIUtil = import('/lua/ui/uiutil.lua')

--- A button's side in mm on the glass: in a game a finger's where there is room, in the menus smaller; the gap about each.
local BUTTON_MM, MENU_BUTTON_MM, GAP_MM = 9, 6.5, 0.5
--- In a game the bar stands under FAF's panels at the left edge (138 of FAF's pixels at the HUD's factor, the multifunction
--- panel's bottom) and above the unit view (234 more).
local ROOM_TOP, UNIT_VIEW_TOP = 138, 234
--- The label's height for a button's side, and a button's colours off and on.
local TEXT_PER_BUTTON = 0.27
local OFF, ON = '70202830', 'C0D08020'

--- The label's point size for a button's side on the glass: FAF's text scales its size by the pixel factor, and the view by
--- its scale.
local function TextSize(bar, side)
    return math.max(1, math.floor(side * TEXT_PER_BUTTON / LayoutHelpers.GetPixelScaleFactor() / bar.ScaleY()))
end

--- A button of `side` pixels on the glass at column `column` and row `row` of the bar, which calls `pressed` with itself.
local function Button(bar, label, side, gap, column, row, pressed)
    local button = Bitmap(bar)
    button:SetSolidColor(OFF)
    button.Width:Set(function() return side / bar.ScaleX() end)
    button.Height:Set(function() return side / bar.ScaleY() end)
    button.Left:Set(function() return bar.Left() + (gap + column * (side + 2 * gap)) / bar.ScaleX() end)
    button.Top:Set(function() return bar.Top() + (gap + row * (side + 2 * gap)) / bar.ScaleY() end)
    local text = UIUtil.CreateText(button, label, TextSize(bar, side), UIUtil.bodyFont)
    text:SetColor('ffffffff')
    text:DisableHitTest()
    LayoutHelpers.AtCenterIn(text, button)
    table.insert(bar.labels, {text = text, side = side})
    button.HandleEvent = function(self, event)
        if event.Type == 'ButtonPress' then
            pressed(self)
            return true
        end
        return false
    end
    return button
end

--- A button that turns the touch layer's toggle `name` over, lit while the layer holds it.
local function Toggle(bar, label, side, gap, column, row, name)
    return Button(bar, label, side, gap, column, row, function(self)
        self:SetSolidColor(FafreTouchBar(name) and ON or OFF)
    end)
end

local function Keys(bar, side, gap, column, row)
    return Button(bar, 'Keys', side, gap, column, row, function()
        FafreKeyboard()
    end)
end

--- The view the engine draws FAF's UI with now (FafreView), which the bar undoes; its labels' sizes follow the scale.
local function FollowView(bar)
    local scaleX, scaleY, offsetX, offsetY = FafreView()
    if scaleX == bar.ScaleX() and scaleY == bar.ScaleY() and offsetX == bar.OffsetX() and offsetY == bar.OffsetY() then
        return
    end
    local rescaled = scaleY ~= bar.ScaleY()
    bar.ScaleX:Set(scaleX)
    bar.ScaleY:Set(scaleY)
    bar.OffsetX:Set(offsetX)
    bar.OffsetY:Set(offsetY)
    if rescaled then
        for _, label in bar.labels do
            label.text:SetFont(UIUtil.bodyFont, TextSize(bar, label.side))
        end
    end
end

--- The bar of the menus (`inGame` false) or of a game, over what FAF's UI has made so far; nothing where it has none.
function Create(inGame)
    if not rawget(_G, 'FafreTouch') then
        return
    end
    local scheme, screenMm = FafreTouch()
    if scheme == 'off' or not screenMm or screenMm <= 0 or (inGame and scheme ~= 'mouse') then
        return
    end
    local root = GetFrame(0)
    -- The picture's lines span the screen's short side.
    local mm = root.Height() / screenMm
    local factor = LayoutHelpers.GetPixelScaleFactor()
    local side, top
    if inGame then
        local room = root.Height() - (ROOM_TOP + UNIT_VIEW_TOP) * factor
        side = math.min(BUTTON_MM * mm, room / 2 - GAP_MM * mm)
        top = ROOM_TOP * factor
    else
        side = MENU_BUTTON_MM * mm
        top = 0
    end
    side = math.floor(side)
    local gap = math.max(1, math.floor(GAP_MM / 2 * mm))
    local bar = Group(root, 'fafre_ui: the bar')
    bar:DisableHitTest()
    bar.Depth:Set(root:GetTopmostDepth() + 1)
    bar.labels = {}
    -- The view's numbers as FafreView gives them: a place on the glass is the UI's place times the scale plus the offset.
    bar.ScaleX = LazyVar.Create(1)
    bar.ScaleY = LazyVar.Create(1)
    bar.OffsetX = LazyVar.Create(0)
    bar.OffsetY = LazyVar.Create(0)
    bar.Left:Set(function() return (root.Left() - bar.OffsetX()) / bar.ScaleX() end)
    bar.Top:Set(function() return (root.Top() + top - bar.OffsetY()) / bar.ScaleY() end)
    local columns, rows = 1, 1
    if inGame then
        Toggle(bar, 'Order', side, gap, 0, 0, 'order')
        Toggle(bar, 'Shift', side, gap, 1, 0, 'shift')
        Toggle(bar, 'Ctrl', side, gap, 2, 0, 'ctrl')
        Toggle(bar, 'Del', side, gap, 0, 1, 'delete')
        Keys(bar, side, gap, 1, 1)
        columns, rows = 3, 2
    else
        Keys(bar, side, gap, 0, 0)
    end
    bar.Width:Set(function() return columns * (side + 2 * gap) / bar.ScaleX() end)
    bar.Height:Set(function() return rows * (side + 2 * gap) / bar.ScaleY() end)
    FollowView(bar)
    bar:SetNeedsFrameUpdate(true)
    bar.OnFrame = function(self)
        FollowView(self)
    end
    return bar
end
