
-- The UI package (android/TOUCH_UI.md): FAF fits the grid of all units to the whole picture at any factor, and a page has no way sideways: it opens a
-- page of the whole picture over the lobby's, with what is not the grid as large on the glass as the HUD. A finger that rests on a unit reads its tooltip,
-- and lifts as a right click, which FAF takes for a left one: that changes nothing here.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')

        local function LeftClickOnly(create)
            return function(...)
                local icon = create(unpack(arg))
                Scale.Try('a unit of Unit Manager', function()
                    local Handle = icon.HandleEvent
                    icon.HandleEvent = function(self, event)
                        if event.Type == 'ButtonPress' and event.Modifiers.Right then
                            return true
                        end
                        return Handle(self, event)
                    end
                end)
                return icon
            end
        end
        CreateUnitIcon = LeftClickOnly(CreateUnitIcon)
        CreatePresetIcon = LeftClickOnly(CreatePresetIcon)

        CreateDialog = Scale.DialogAt(function()
            return Scale.GlassFactor(1)
        end, Scale.Page(CreateDialog, function()
            local frame = GetFrame(0)
            return GUI.bg, {left = frame.Left(), top = frame.Top(), right = frame.Right(), bottom = frame.Bottom()}
        end), function()
            return GUI.bg
        end)
    end)
    if not ok then
        WARN('fafre_ui: ui/lobby/unitsmanager.lua: ' .. tostring(problem))
    end
end
