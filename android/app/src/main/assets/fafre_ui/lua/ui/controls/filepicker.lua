
-- The UI package (android/TOUCH_UI.md): a file's row is a finger's size on the glass, where FAF has them 20 pixels high at any factor. The dialogs
-- around a picker, Replay, Save and Load, are as large on the glass as the HUD: what does not fit then goes from the middle of the list.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local ROW_MM = 5
        -- The least of a dialog that still has rows enough, its width with its brackets, and its brackets above and below.
        local LEAST_LINES = 480
        local WIDTH = 790
        local BRACKET_LINES = 25
        local newest = false

        local function FitFileRows(picker)
            local rows = picker._filelistObjects
            local height = math.max(rows[1].Height(), math.ceil(Scale.PicturePixels(ROW_MM)))
            local count = math.max(1, math.floor((picker._filelist.Bottom() - rows[1].Top()) / height))
            for index = table.getsize(rows), 1, -1 do
                if index > count then
                    rows[index]:Destroy()
                    rows[index] = nil
                else
                    rows[index].Height:Set(height)
                end
            end
        end

        local InitPicker = FilePicker.__init
        FilePicker.__init = function(self, ...)
            InitPicker(self, unpack(arg))
            newest = self
        end

        -- FAF sets up the list's rows in a thread of the picker, and asks for the tabs just before.
        local SetTabs = FilePicker.SetTabConfiguration
        FilePicker.SetTabConfiguration = function(self, configID)
            SetTabs(self, configID)
            local list = self._filelist
            if list.CreateOptionElements and not list.rowsFit then
                list.rowsFit = true
                local CreateRows = list.CreateOptionElements
                list.CreateOptionElements = function()
                    CreateRows()
                    Scale.Try('the rows of a file picker', function() FitFileRows(self) end)
                end
            end
        end

        --- `build` makes a dialog around a picker, in FAF's frame of Replay.
        function InDialog(build)
            return Scale.Dialog(LEAST_LINES, function(...)
                local results = {build(unpack(arg))}
                Scale.Try('the dialog of a file picker', function()
                    Scale.FitByList(newest, UIUtil.UIFile('/scx_menu/replay/panel_bmp.dds'), BRACKET_LINES, newest._filelist)
                    Scale.ShowTopOf(newest:GetParent())
                end)
                return unpack(results)
            end, nil, WIDTH)
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/controls/filepicker.lua: ' .. tostring(problem))
    end
end
