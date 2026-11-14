
-- The UI package (android/TOUCH_UI.md): what opens in the middle of the picture shows, wherever the page stands, from its top if it is higher than the screen.
-- In a game nothing of Griff's lies over it meanwhile (fafre/opened.lua): its shadow over the whole screen takes every press outside it, which closes it.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local Opened = import('/fafre_ui/lua/fafre/opened.lua')
        local function Screen()
            local frame = GetFrame(0)
            return frame.Left(), frame.Top(), frame.Right(), frame.Bottom()
        end
        local InitPopup = Scale.Needed(Popup.__init, 'Popup.__init')
        Popup.__init = function(self, GUI, content)
            InitPopup(self, GUI, content)
            Scale.Try('a popup', function()
                Scale.ShowTopOf(self)
            end)
            Opened.Add(self, Screen)
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/controls/popups/popup.lua: ' .. tostring(problem))
    end
end
