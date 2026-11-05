
-- The UI package (android/TOUCH_UI.md): FAF's file takes its local factor once, as it is first imported, before the Unit Manager's own holds. Its tooltip undoes that
-- factor to get at its height, and a tooltip goes above a unit or below it by that height: it goes by the factor there is.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local CreateTooltip = Create
        Create = function(...)
            Scale.Try('the factor of a tooltip', function()
                PixelScaleFactor = LayoutHelpers.GetPixelScaleFactor()
            end)
            return CreateTooltip(unpack(arg))
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/lobby/unitstooltip.lua: ' .. tostring(problem))
    end
end
