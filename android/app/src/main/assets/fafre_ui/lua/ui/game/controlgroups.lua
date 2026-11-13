
-- The UI package (android/TOUCH_UI.md): FAF's 368 lines down lie under the construction panel at the HUD's factor: right below the avatars.
-- With the touch scheme Griff, which has no Ctrl to make a group with, groups 1, 2 and 3 are slots that always show, an empty one
-- dim (buildstage/griff-v3-spec.md T3.3): a tap is FAF's left press, two its double click; a rest, the right press, stores the
-- selection there as Ctrl and the number do (UI_MakeSelectionSet), with Plus it adds the selection as FAF's right press does. FAF's
-- other groups stay FAF's.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local LayoutHelpers = import('/lua/maui/layouthelpers.lua')
        -- The mockup's H in FAF's pixels: the app's factor makes a build icon's 48 of them 7.3 mm on the glass (HudScale).
        local H = 48 / 7.3
        local SLOTS = {'1', '2', '3'}
        -- FAF's first group stands 10 of its pixels into the panel, and the next ones 8 over the one before, 15 from its right.
        local FIRST, OVER, RIGHT = 10, 8, 15
        -- Game's TOAST_: the app's words for them.
        local EMPTY_GROUP, NOTHING_SELECTED, GROUP_STORED = 10, 11, 12
        -- The mockup's colours: a filled slot faf-on with the own colour's edge, an empty one faf-btn with faf-edge's.
        local FILLED, FILLED_EDGE, EMPTY, EMPTY_EDGE = 'ff1d4a52', 'ff2dd4bf', 'ff142330', 'ff3a5566'
        local TEXT, MUTED = 'ffe8eef2', 'ffa8bac5'
        -- Griff's slots by their group's name, once the game's UI has made them.
        local slots = false

        --- How many units a group holds that live.
        local function Count(name)
            local units = {}
            local set = Selection.selectionSets[name]
            if set then
                for unit, _ in set do
                    table.insert(units, unit)
                end
            end
            return table.getn(ValidateUnitsList(units))
        end

        --- A rest on a slot, FAF's right press: the selection is the group from now on (Ctrl and its number's key action), or with
        --- Plus (Shift) it joins the group, as FAF's own right press does. With nothing selected the group stays as it was.
        local function Store(name, add)
            if table.empty(GetSelectedUnits() or {}) then
                FafreGriffToast(NOTHING_SELECTED, 0)
                return
            end
            if add then
                Selection.AppendSelectionToSet(name)
            else
                ConExecute('UI_MakeSelectionSet ' .. name)
            end
            FafreGriffToast(GROUP_STORED, tonumber(name))
        end

        --- A slot's press: FAF's group's, but for the right press, Store's. FAF's double click is a press again, whose second
        --- selection of the same group soon after the first moves the camera to it (selection.lua DoubleTapBehavior).
        local function Pressed(self, event)
            if event.Type ~= 'ButtonPress' and event.Type ~= 'ButtonDClick' then
                return false
            end
            local modifiers = event.Modifiers
            if modifiers.Right then
                Store(self.name, modifiers.Shift)
            elseif Count(self.name) == 0 then
                FafreGriffToast(EMPTY_GROUP, 0)
            elseif modifiers.Shift and modifiers.Ctrl then
                Selection.FactorySelection(self.name)
            elseif modifiers.Shift then
                Selection.AppendSetToSelection(self.name)
            elseif modifiers.Left then
                Selection.ApplySelectionSet(self.name)
            end
            return true
        end

        --- A text of a slot, its middle at this much of the slot's width, in the mockup's font XS.
        local function SlotText(slot, across)
            local text = UIUtil.CreateText(slot, '', 1.95 * H, UIUtil.bodyFont)
            text.Left:Set(function() return slot.Left() + across * slot.Width() - text.Width() / 2 end)
            LayoutHelpers.AtVerticalCenterIn(text, slot)
            text:DisableHitTest()
            return text
        end

        --- A slot (the mockup's drawGroup), 7 x 5.1 H: its edge's colour, its fill a pixel in, its number at 28 % of its width
        --- and its count at 68 %.
        local function MakeSlot(name)
            local slot = Bitmap(controls.container)
            slot.name = name
            LayoutHelpers.SetDimensions(slot, 7 * H, 5.1 * H)
            slot.fill = Bitmap(slot)
            local edge = math.max(1, LayoutHelpers.ScaleNumber(1))
            slot.fill.Left:Set(function() return slot.Left() + edge end)
            slot.fill.Top:Set(function() return slot.Top() + edge end)
            slot.fill.Right:Set(function() return slot.Right() - edge end)
            slot.fill.Bottom:Set(function() return slot.Bottom() - edge end)
            slot.fill:DisableHitTest()
            slot.number = SlotText(slot, 0.28)
            slot.number:SetText(name)
            slot.count = SlotText(slot, 0.68)
            slot.count:SetColor(TEXT)
            slot.HandleEvent = Pressed
            return slot
        end

        --- Each slot as its group is now: filled with its count, or dim and empty. FAF's own controls of these groups make way.
        local function ShowSlots()
            for _, name in SLOTS do
                local slot = slots[name]
                local count = Count(name)
                slot:SetSolidColor(count > 0 and FILLED_EDGE or EMPTY_EDGE)
                slot.fill:SetSolidColor(count > 0 and FILLED or EMPTY)
                slot.number:SetColor(count > 0 and TEXT or MUTED)
                slot.count:SetText(count > 0 and tostring(count) or '')
                local faf = controls.groups[name]
                if faf then
                    faf:SetAlpha(0, true)
                    faf:DisableHitTest(true)
                end
            end
        end

        --- The slots 5.5 H apart from FAF's first group's place, at the column's x + 1.5 H (the mockup's: the column 10 H wide, an H
        --- from the screen's edge), FAF's other groups below them as FAF lays them out, and the panel shown as for FAF's groups.
        local function LayoutSlots()
            local container = controls.container
            local below = false
            for index, name in SLOTS do
                local slot = slots[name]
                LayoutHelpers.AtRightIn(slot, container, 2.5 * H)
                LayoutHelpers.AtTopIn(slot, container, FIRST + (index - 1) * 5.5 * H)
                below = slot
            end
            local first = true
            for _, name in groupOrder do
                local group = controls.groups[name]
                if group and not slots[name] then
                    if first then
                        LayoutHelpers.AtTopIn(group, container, FIRST + table.getn(SLOTS) * 5.5 * H)
                        first = false
                    else
                        LayoutHelpers.Below(group, below, -OVER)
                    end
                    LayoutHelpers.AtRightIn(group, container, RIGHT)
                    below = group
                end
            end
            container.Bottom:Set(below.Bottom)
            container:Show()
            controls.collapseArrow:Show()
        end

        local Layout = SetLayout
        SetLayout = function()
            Layout()
            Scale.Try('the place of the control groups', function()
                local avatars = import('/lua/ui/game/avatars.lua').controls.avatarGroup
                controls.container.Top:Set(function()
                    return avatars.Bottom() + LayoutHelpers.ScaleNumber(24)
                end)
            end)
            if slots then
                Scale.Try('Griff\'s control groups', LayoutSlots)
            end
        end

        local Create = CreateUI
        CreateUI = function(mapGroup)
            Create(mapGroup)
            Scale.Try('Griff\'s control groups', function()
                if not FafreGriffWanted() then
                    return
                end
                slots = {}
                for _, name in SLOTS do
                    slots[name] = MakeSlot(name)
                end
                ShowSlots()
                LayoutSlots()
                -- A group's units die without a word to the panel: FAF's own controls look each second, and so do the slots.
                ForkThread(function()
                    while not IsDestroyed(controls.container) do
                        WaitSeconds(1)
                        local shown, failure = pcall(ShowSlots)
                        if not shown then
                            WARN('fafre_ui: Griff\'s control groups: ' .. tostring(failure))
                            return
                        end
                    end
                end)
            end)
        end

        local Changed = OnSelectionSetChanged
        OnSelectionSetChanged = function(name, units, applied)
            Changed(name, units, applied)
            if slots then
                Scale.Try('Griff\'s control groups', function()
                    ShowSlots()
                    LayoutSlots()
                end)
            end
        end

        -- The slots show whenever the game's UI does, as FAF's groups do once there are any.
        local Shown = Expand
        Expand = function()
            Shown()
            if slots then
                controls.container:Show()
                controls.collapseArrow:Show()
            end
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/game/controlgroups.lua: ' .. tostring(problem))
    end
end
