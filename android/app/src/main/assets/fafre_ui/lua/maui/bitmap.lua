
-- The UI package (android/TOUCH_UI.md): a bitmap keeps the size it was made at. FAF works it out again with each texture, at whatever factor holds by then:
-- the button a finger has just left comes back at the size of the dialog that it opened.
do
    local ok, problem = pcall(function()
        local LayoutHelpers = import('/lua/maui/layouthelpers.lua')
        local ResetLayout = Bitmap.ResetLayout
        Bitmap.ResetLayout = function(self)
            ResetLayout(self)
            local factor = LayoutHelpers.GetPixelScaleFactor()
            self.Width:SetFunction(function() return math.floor(self.BitmapWidth() * factor) end)
            self.Height:SetFunction(function() return math.floor(self.BitmapHeight() * factor) end)
        end
    end)
    if not ok then
        WARN('fafre_ui: maui/bitmap.lua: ' .. tostring(problem))
    end
end
