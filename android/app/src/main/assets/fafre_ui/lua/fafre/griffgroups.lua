-- Griff's groups (android/TOUCH_UI.md, "The groups"), Supreme Commander 2's on a console: the units one gesture ordered
-- together, and the units a factory makes of one order, are a group, which one round icon shows at their middle on the map
-- while the game draws strategic icons; a tap on it selects the group. The icon is FAF's own strategic icon art: its round
-- icon (its experimentals'), in the colour the game draws one's own icons in, with its selected look while all of the group is
-- selected, as the game draws a selected unit's icon; and the group's count on it in FAF's font, white with a shadow, as FAF
-- writes on its icons. As large as FAF's largest unit icon, a selected land unit's, at FAF's strategic icon scale. The program
-- keeps the groups, places their icons (android/native/src/host/GriffAndroid.cpp, FafreGriffGroups) and takes a tap on one;
-- Griff's Groups button (griffhud.lua) shows and hides them. An icon is a picture on the map's view, as FAF's reclaim labels
-- are, that takes no event: a finger's pan, rest or stroke over it is the map's. griff.lua runs Frame each frame.

local Bitmap = import('/lua/maui/bitmap.lua').Bitmap
local LayoutHelpers = import('/lua/maui/layouthelpers.lua')
local UIUtil = import('/lua/ui/uiutil.lua')
local WorldView = import('/lua/ui/game/worldview.lua')
local Hud = import('/fafre_ui/lua/fafre/griffhud.lua')

local ICON = '/textures/ui/common/game/strategicicons/icon_experimental_generic_rest.dds'
local SELECTED = '/textures/ui/common/game/strategicicons/icon_experimental_generic_selected.dds'
--- The count's height as a part of the icon's side, and how wide it may be at most: a count of three digits or more is
--- written smaller, so that it stays on the disc.
local COUNT = 0.6
local COUNT_WIDE = 0.8

--- The icons made, on the map's view they were made on, and the places the program writes each frame: an icon's middle on
--- the picture, its group's count and whether all of the group is selected (1), four numbers each.
local icons = {}
local view = false
local places = {}

local function Make(parent)
    local icon = Bitmap(parent, ICON)
    icon:DisableHitTest()
    -- The round icon's side in its texture: the selected look rings it, as large as it is in FAF's art.
    icon.disc = icon.BitmapWidth()
    icon.count = UIUtil.CreateText(icon, '', 10, UIUtil.bodyFont)
    icon.count:SetColor('ffffffff')
    icon.count:SetDropShadow(true)
    icon.count:DisableHitTest()
    LayoutHelpers.AtCenterIn(icon.count, icon)
    icon.shown = {}
    return icon
end

--- An icon at its place in the picture's pixels, `side` of them for the round icon, as the program has it.
local function Place(icon, x, y, count, selected, side, colour)
    local shown = icon.shown
    if shown.selected ~= selected or shown.side ~= side then
        icon:SetTexture(selected and SELECTED or ICON)
        local size = side * icon.BitmapWidth() / icon.disc
        icon.Width:Set(size)
        icon.Height:Set(size)
        shown.selected = selected
    end
    if shown.side ~= side or shown.count ~= count then
        -- FAF's text scales its size by the HUD's factor; the icon's is the strategic icons'.
        local text = tostring(count)
        local size = COUNT * side / LayoutHelpers.GetPixelScaleFactor()
        icon.count:SetFont(UIUtil.bodyFont, size)
        local wide = icon.count:GetStringAdvance(text)
        if wide > COUNT_WIDE * side then
            icon.count:SetFont(UIUtil.bodyFont, size * COUNT_WIDE * side / wide)
        end
        icon.count:SetText(text)
        shown.side = side
        shown.count = count
    end
    if shown.colour ~= colour then
        icon:SetColorMask(colour)
        shown.colour = colour
    end
    icon.Left:SetValue(x - icon.Width() / 2)
    icon.Top:SetValue(y - icon.Height() / 2)
    if icon:IsHidden() then
        icon:Show()
    end
end

--- Each frame of the session's UI: the icons where the program places them, while Groups shows them and FAF's UI is not
--- hidden (`hidden`); the program keeps the groups each frame either way, and without the places there is no icon for a tap
--- either. Answers the colour of one's own strategic icons, which Groups' icon takes.
function Frame(hidden)
    local wanted = not hidden and Hud.GroupsShown() and WorldView.viewLeft or false
    -- FAF makes the map's view anew where it splits and joins it: the icons go with the old one.
    if wanted and wanted ~= view then
        for _, icon in icons do
            if not IsDestroyed(icon) then
                icon:Destroy()
            end
        end
        icons = {}
        view = wanted
    end
    local count, side, colour = FafreGriffGroups(wanted and places or nil)
    for at = 1, count do
        local icon = icons[at]
        if not icon then
            icon = Make(view)
            icons[at] = icon
        end
        local from = 4 * at - 4
        Place(icon, places[from + 1], places[from + 2], places[from + 3], places[from + 4] == 1, side, colour)
    end
    for at = count + 1, table.getn(icons) do
        local icon = icons[at]
        if not IsDestroyed(icon) and not icon:IsHidden() then
            icon:Hide()
        end
    end
    return colour
end
