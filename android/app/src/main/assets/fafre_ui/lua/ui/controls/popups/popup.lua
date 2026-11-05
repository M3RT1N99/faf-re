
-- The UI package (android/TOUCH_UI.md): what opens in the middle of the picture shows, wherever the page stands, from its top if it is higher than the screen.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local InitPopup = Popup.__init
        Popup.__init = function(self, GUI, content)
            InitPopup(self, GUI, content)
            Scale.Try('a popup', function()
                Scale.ShowTopOf(self)
            end)
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/controls/popups/popup.lua: ' .. tostring(problem))
    end
end
