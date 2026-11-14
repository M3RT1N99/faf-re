
-- The UI package (android/TOUCH_UI.md): each window, the chat's, its options', the minimap's, is one the player opened, which nothing
-- of Griff's may cover (fafre/opened.lua): its own box, where its title and its borders for a resize lie.
do
    local ok, problem = pcall(function()
        local Opened = import('/fafre_ui/lua/fafre/opened.lua')
        local InitWindow = Window.__init
        Window.__init = function(self, ...)
            InitWindow(self, unpack(arg))
            Opened.Add(self)
        end
    end)
    if not ok then
        WARN('fafre_ui: maui/window.lua: ' .. tostring(problem))
    end
end
