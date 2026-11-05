
-- The UI package (android/TOUCH_UI.md): a dropdown's rows are a finger's size on the glass, whatever the view's scale, and a
-- dropdown stays on the picture. As that depends on the screen it is on, it is worked out each time one opens.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local ROW_MM = 5

        --- Whether a click at this place is on the box of this control.
        local function On(control, x, y, right)
            return x >= control.Left() and x <= (right or control.Right()) and y >= control.Top() and y <= control.Bottom()
        end

        --- A click beside a dropdown closes it, and FAF has it always below its combo and of its width.
        local function CloseByClickBeside(self, isOpen, close)
            UIMain.RemoveOnMouseClickedFunc(self._globalMouseClickFn)
            self._globalMouseClickFn = function(event)
                if isOpen() and not On(self, event.x, event.y) then
                    local right = self._scrollbar and self._scrollbar.Right() or nil
                    if not On(self._dropdown, event.x, event.y, right) then
                        close()
                    end
                end
            end
            UIMain.AddOnMouseClickedFunc(self._globalMouseClickFn)
        end

        --- The widest row as the list's font has it, which is the title's font scaled by the heights of the two.
        local function WidestRow(self)
            local list = self._list
            local title = self._text
            local widest = 0
            for row = 0, list:GetItemCount() - 1 do
                widest = math.max(widest, title:GetStringAdvance(list:GetItem(row)))
            end
            return widest * list:GetRowHeight() / math.max(1, title.FontAscent() + title.FontDescent() + title.FontExternalLeading())
        end

        local function FitTextDropdown(self)
            local list = self._list
            local dropdown = self._dropdown
            local frame = GetFrame(0)
            Scale.FitRows(list, ROW_MM)
            local row = list:GetRowHeight()
            local border = dropdown.Height() - list.Height()
            local below = math.min(frame.Bottom() - self.Bottom(), Scale.ShownLines())
            local above = math.min(self.Top() - frame.Top(), Scale.ShownLines())
            local count = list:GetItemCount()
            local goesUp = count * row + border > below and above > below
            local visible = math.floor(((goesUp and above or below) - border) / row)
            visible = math.max(1, math.min(count, math.min(self._maxVisibleItems, visible)))

            self._visibleItems:Set(visible)
            list.Height:Set(visible * row)
            if goesUp then
                dropdown.Top:Set(function() return self.Top() - dropdown.Height() end)
            else
                dropdown.Top:Set(self.Bottom)
            end
            if count > visible and not self._scrollbar then
                self._scrollbar = UIUtil.CreateVertScrollbarFor(list, self._scrollbarOffsetRight, nil, self._scrollbarOffsetBottom, self._scrollbarOffsetTop)
            elseif count <= visible and self._scrollbar then
                self._scrollbar:Destroy()
                self._scrollbar = nil
            end
            -- It grows to the left, up to the picture's edge: larger letters need more room than the combo has.
            local margins = LayoutHelpers.ScaleNumber(self._scrollbar and 27 or 5) + dropdown.Width() - list.Width()
            local width = self.Width() - LayoutHelpers.ScaleNumber(5)
            dropdown.Width:Set(math.max(width, math.min(WidestRow(self) + margins, self.Right() - frame.Left())))
            Scale.ShowLines(dropdown.Top(), dropdown.Bottom())
        end

        local InitCombo = Combo.__init
        Combo.__init = function(self, ...)
            InitCombo(self, unpack(arg))
            Scale.Try('a dropdown', function()
                local dropdown = self._dropdown
                local OnHide = dropdown.OnHide
                dropdown.OnHide = function(control, hidden)
                    if not hidden and not self._listhidden and not self:IsDisabled() then
                        Scale.Try('a dropdown that opens', function() FitTextDropdown(self) end)
                    end
                    return OnHide(control, hidden)
                end
                CloseByClickBeside(self, function()
                    return self._list and not self._listhidden
                end, function()
                    self._listhidden = true
                    dropdown:SetHidden(true)
                end)
            end)
        end

        --- A control's children in the order they were made.
        local function ChildrenOf(control)
            local children = {}
            control:ApplyFunction(function(child)
                if child ~= control then
                    table.insert(children, child)
                end
            end)
            return children
        end

        --- A row's picture fills the row: a colour as it is, an icon at its own proportions.
        local function FitPicture(self, row, width, height)
            local picture = ChildrenOf(row)[1]
            local margin = LayoutHelpers.ScaleNumber(2)
            picture.Height:Set(height - 2 * margin)
            if self._isColor then
                picture.Width:Set(width - 2 * margin)
            else
                picture.Width:Set((height - 2 * margin) * picture.BitmapWidth() / picture.BitmapHeight())
            end
        end

        --- A dropdown of pictures has no scroll bar: if its rows do not fit below it they go in columns, to the left.
        local function FitBitmapDropdown(self)
            local rows = {}
            for key = 1, table.getsize(self._list) + 1 do
                if self._list[key] then
                    table.insert(rows, self._list[key])
                end
            end
            local dropdown = self._dropdown
            -- The upper left corner, which is what the dropdown's left goes by.
            local corner = ChildrenOf(dropdown)[1]
            self._columnWidth = self._columnWidth or dropdown.Width()
            local width = self._columnWidth
            local height = math.ceil(Scale.PicturePixels(ROW_MM))
            local gap = LayoutHelpers.ScaleNumber(1)
            local room = math.min(GetFrame(0).Bottom() - self.Bottom(), Scale.ShownLines()) - LayoutHelpers.ScaleNumber(12)
            local perColumn = math.max(1, math.floor(room / (height + gap)))
            local columns = math.ceil(table.getn(rows) / perColumn)
            local left = self._btnLeft.Left() + LayoutHelpers.ScaleNumber(5) - (columns - 1) * width

            corner.Left:Set(left)
            for index, row in rows do
                local x = math.floor((index - 1) / perColumn) * width
                local y = math.mod(index - 1, perColumn) * (height + gap)
                row.Left:Set(function() return dropdown.Left() + x end)
                row.Top:Set(function() return dropdown.Top() + y end)
                row.Width:Set(width)
                row.Height:Set(height)
                FitPicture(self, row, width, height)
            end
            local lines = math.min(table.getn(rows), perColumn)
            self.ddm.Bottom:Set(function() return self.ddm.Top() + lines * (height + gap) end)
            Scale.ShowLines(self.ddm.Top(), self.ddm.Bottom() + LayoutHelpers.ScaleNumber(6))
        end

        local InitBitmapCombo = BitmapCombo.__init
        BitmapCombo.__init = function(self, ...)
            InitBitmapCombo(self, unpack(arg))
            Scale.Try('a dropdown of pictures', function()
                local dropdown = self._dropdown
                local OnHide = dropdown.OnHide
                dropdown.OnHide = function(control, hidden)
                    if not hidden and not self._ddhidden and not self:IsDisabled() then
                        Scale.Try('a dropdown of pictures that opens', function() FitBitmapDropdown(self) end)
                    end
                    return OnHide(control, hidden)
                end
                CloseByClickBeside(self, function()
                    return not self._ddhidden
                end, function()
                    self._ddhidden = true
                    dropdown:SetHidden(true)
                end)
            end)
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/controls/combo.lua: ' .. tostring(problem))
    end
end
