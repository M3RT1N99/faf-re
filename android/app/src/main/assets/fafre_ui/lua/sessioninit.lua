
-- The UI package (android/TOUCH_UI.md), appended to /lua/sessioninit.lua, which runs in a session's fresh state and
-- only there: the session's HUD takes the app's factor, and the front end keeps FAF's own.
local ok, problem = pcall(function()
    import('/fafre_ui/lua/fafre/scale.lua').StartSession()
end)
if not ok then
    WARN('fafre_ui: sessioninit: ' .. tostring(problem))
end
