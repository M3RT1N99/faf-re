
-- The UI package (android/TOUCH_UI.md): FAF's 368 lines down lie under the construction panel at the HUD's factor: right below the avatars.
-- With the touch scheme Griff, which has no Ctrl to make a group with, groups 1, 2 and 3 always show in FAF's own column of control
-- groups (buildstage/griff-v3-spec.md T3.3): a group's slot is FAF's own control for it (CreateGroup: the control group's frame with its
-- best unit's icon and its number), an empty one the same frame with its number alone, dimmed. A tap is FAF's left press, two its double
-- click; a rest, the right press, stores the selection there as Ctrl and the number do (UI_MakeSelectionSet), with Plus it adds the
-- selection as FAF's right press does. FAF's other groups stay FAF's, below them as FAF lays them out.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local LayoutHelpers = import('/lua/maui/layouthelpers.lua')
        local SLOTS = {'1', '2', '3'}
        -- FAF's first group stands 10 of its pixels into the panel and 15 from its right, each next one 8 over the one before
        -- (controlgroups_mini.lua LayoutGroups).
        local FIRST, OVER, RIGHT = 10, 8, 15
        -- How much of FAF's frame an empty slot shows.
        local EMPTY_ALPHA = 0.45
        -- griff.lua's hints (Game's HINT_).
        local EMPTY_GROUP, NOTHING_SELECTED, GROUP_STORED = 10, 11, 12
        -- Griff's empty slots by their group's name, once the game's UI has made them.
        local empties = false

        local function Hint(kind, number)
            import('/fafre_ui/lua/fafre/griff.lua').Hint(kind, number)
        end

        --- A rest on a slot, FAF's right press: the selection is the group from now on (Ctrl and its number's key action), or with
        --- Plus (Shift) it joins the group, as FAF's own right press does. With nothing selected the group stays as it was. The
        --- phone buzzes once for a group stored.
        local function Store(name, add)
            if table.empty(GetSelectedUnits() or {}) then
                Hint(NOTHING_SELECTED)
                return
            end
            if add then
                Selection.AppendSelectionToSet(name)
            else
                ConExecute('UI_MakeSelectionSet ' .. name)
            end
            Hint(GROUP_STORED, tonumber(name))
            FafreGriffGroupStored()
        end

        --- The right press on a slot is Store's; FAF's own control's other presses are FAF's (`faf`, its handler), an empty
        --- slot's say how a group is stored.
        local function Pressed(self, event, faf)
            if event.Type ~= 'ButtonPress' and event.Type ~= 'ButtonDClick' then
                return faf and faf(self, event) or false
            end
            if event.Modifiers.Right then
                Store(self.name, event.Modifiers.Shift)
            elseif faf then
                faf(self, event)
            else
                Hint(EMPTY_GROUP)
            end
            return true
        end

        --- An empty slot, as FAF makes a group's control (CreateGroup) but for the unit's icon: the control group's frame, its
        --- number in FAF's font at the right of the icon's place and 5 over the frame's bottom, all of it dimmed.
        local function MakeEmpty(name)
            local slot = Bitmap(controls.container, UIUtil.SkinnableFile('/game/avatar/avatar-control-group_bmp.dds'))
            slot.name = name
            slot.icon = Group(slot)
            LayoutHelpers.SetDimensions(slot.icon, 28, 20)
            LayoutHelpers.AtCenterIn(slot.icon, slot, 0, -4)
            slot.icon:DisableHitTest()
            slot.label = UIUtil.CreateText(slot.icon, name, 18, UIUtil.bodyFont)
            slot.label:SetColor('ffffffff')
            slot.label:SetDropShadow(true)
            LayoutHelpers.AtRightIn(slot.label, slot.icon)
            LayoutHelpers.AtBottomIn(slot.label, slot, 5)
            slot.label:DisableHitTest()
            slot.HandleEvent = function(self, event)
                return Pressed(self, event, nil)
            end
            return slot
        end

        --- FAF's own control of a group of the slots takes Store's right press, once for each control FAF makes.
        local function TakeOver(control)
            if control.fafreSlot then
                return
            end
            control.fafreSlot = true
            local faf = control.HandleEvent
            control.HandleEvent = function(self, event)
                return Pressed(self, event, faf)
            end
        end

        --- What stands in the column for a group: FAF's own control, or for a slot without one its empty slot.
        local function InColumn(name)
            return controls.groups[name] or (empties and empties[name]) or nil
        end

        --- Each slot as its group is now: FAF's own control where FAF made one, else the empty slot. An empty slot that makes way
        --- is unseen and takes no press rather than hidden: FAF shows the panel with all it holds (Show is its children's too), as
        --- its layout does after each change of a group.
        local function ShowSlots()
            for _, name in SLOTS do
                local faf = controls.groups[name]
                local empty = empties[name]
                if faf then
                    TakeOver(faf)
                    empty:SetAlpha(0, true)
                    empty:DisableHitTest()
                else
                    empty:SetAlpha(EMPTY_ALPHA, true)
                    empty:EnableHitTest()
                end
            end
        end

        -- What stood in the column for each slot at its last layout.
        local laidOut = {}

        --- The column as FAF lays it out (controlgroups_mini.lua LayoutGroups), with the slots always in it, and the panel shown as
        --- FAF shows it for its groups.
        local function LayoutSlots()
            local container = controls.container
            local above = false
            for _, name in SLOTS do
                laidOut[name] = InColumn(name)
            end
            for _, name in groupOrder do
                local control = InColumn(name)
                if control then
                    if above then
                        LayoutHelpers.Below(control, above, -OVER)
                    else
                        LayoutHelpers.AtTopIn(control, container, FIRST)
                        LayoutHelpers.AtRightIn(control, container, RIGHT)
                    end
                    above = control
                end
            end
            container.Bottom:Set(above.Bottom)
            container:Show()
            controls.collapseArrow:Show()
        end

        local Layout = SetLayout
        SetLayout = function()
            Layout()
            Scale.Try('the place of the control groups', function()
                local avatars = Scale.Needed(import('/lua/ui/game/avatars.lua').controls.avatarGroup, 'avatars.lua controls.avatarGroup')
                controls.container.Top:Set(function()
                    return avatars.Bottom() + LayoutHelpers.ScaleNumber(24)
                end)
            end)
            if empties then
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
                -- What a slot asks of FAF's selection.lua: without all of it FAF's own groups stay.
                for _, name in {'AppendSelectionToSet', 'FactorySelection', 'AppendSetToSelection', 'ApplySelectionSet'} do
                    Scale.Needed(Selection[name], 'selection.lua ' .. name)
                end
                empties = {}
                for _, name in SLOTS do
                    empties[name] = MakeEmpty(name)
                end
                ShowSlots()
                LayoutSlots()
                -- A group's units die without a word to the panel, and FAF's own check each second takes its control away: so the
                -- slots look each second too, and the column is laid out anew where a slot's control changed, not each second, which
                -- would open a panel the player folded away.
                ForkThread(function()
                    while not IsDestroyed(controls.container) do
                        WaitSeconds(1)
                        local shown, failure = pcall(function()
                            ShowSlots()
                            for _, name in SLOTS do
                                if InColumn(name) ~= laidOut[name] then
                                    LayoutSlots()
                                    return
                                end
                            end
                        end)
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
            if empties then
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
            if empties then
                Scale.Try('Griff\'s control groups', function()
                    controls.container:Show()
                    controls.collapseArrow:Show()
                end)
            end
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/game/controlgroups.lua: ' .. tostring(problem))
    end
end
