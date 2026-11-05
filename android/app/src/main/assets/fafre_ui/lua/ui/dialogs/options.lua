
-- The UI package (android/TOUCH_UI.md): the dialog is shown larger once, and for that has fewer rows at a time, which a finger
-- scrolls. Its frame stays FAF's: of the one picture its top and its bottom, with the same part of the list's art left out of the middle.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        -- A row's height on the glass, in FAF's lines.
        local ROW_MM = 5.8
        local ROW_LINES = 34
        -- FAF's whole dialog with its brackets, the least of it that has rows enough, and its width with them.
        local ALL_LINES = 646
        local LEAST_LINES = 440
        local WIDTH = 780
        -- The brackets above and below, and what lies below the rows.
        local BRACKET_LINES = 24
        local BELOW_ROWS = 100

        local function Factor()
            local frame = GetFrame(0)
            local wanted = Scale.PicturePixels(ROW_MM) / ROW_LINES
            return math.max(frame.Height() / ALL_LINES, math.min(frame.Height() / LEAST_LINES, frame.Width() / WIDTH, wanted))
        end

        local function Reflow(grid)
            local frame = GetFrame(0)
            local row = grid._itemHeight
            local fullHeight = dialog.Height()
            local rowsTop = grid.Top() - dialog.Top()
            local below = LayoutHelpers.ScaleNumber(BELOW_ROWS)
            local brackets = 2 * LayoutHelpers.ScaleNumber(BRACKET_LINES)
            local rows = math.floor((frame.Height() - brackets - rowsTop - below) / row)
            local _, visible = grid:GetVisible()
            if rows >= visible then
                return
            end
            Scale.CutFrame(dialog, UIUtil.UIFile('/scx_menu/options/panel_bmp.dds'), rowsTop + math.floor(rows / 2) * row,
                fullHeight - (rowsTop + rows * row + below))
            grid.Height:Set(rows * row)
            grid:EndBatch()
        end

        -- A row's own handler is for the wheel, and at this size the row no longer counts as small. So that a tap beside a choice or a
        -- slider finds it, the row says what the tap's help goes by for a label with a tooltip: it wraps a handler that does nothing.
        local PlainHandler = import('/lua/maui/control.lua').Control.HandleEvent
        local RealCreateOption = CreateOption
        CreateOption = function(parent, optionItemData)
            local row = RealCreateOption(parent, optionItemData)
            row.oldHandleEvent = PlainHandler
            return row
        end

        local Create = CreateDialog
        CreateDialog = Scale.DialogAt(Factor, function(over, exitBehavior)
            -- FAF's local of the function.
            local RealGrid = Grid
            local grid = false
            Grid = function(...)
                grid = RealGrid(unpack(arg))
                return grid
            end
            local made, problem = pcall(Create, over, exitBehavior)
            Grid = RealGrid
            if not made then
                error(problem, 0)
            end
            Scale.Try('the rows of Options', function()
                Reflow(grid)
            end)
        end)

        -- The UI scale is the HUD's here, which a session reads as it starts: no restart.
        for _, section in import('/lua/options/options.lua').options do
            for _, item in section.items do
                if item.key == 'ui_scale' then
                    item.restart = false
                end
            end
        end
        import('/lua/ui/help/tooltips.lua').Tooltips.options_ui_scale.description = "Changes the size of the game's HUD, from the next game on."
    end)
    if not ok then
        WARN('fafre_ui: ui/dialogs/options.lua: ' .. tostring(problem))
    end
end
