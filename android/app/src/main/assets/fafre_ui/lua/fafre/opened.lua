-- What FAF shows that the player opened, which nothing of the touch scheme Griff's may cover (android/TOUCH_UI.md "Griff"): its
-- windows (the chat, its options, the minimap), its popups (each question box), the dropouts of its multifunction panel (the
-- overlay filters, the map's options, the team colours) and the content of its tabs (the Menu, the diplomacy). The hooks of the
-- files that make them say each one here as it is made; griff.lua looks at those that show each frame and reports them where they
-- changed, and the app's pieces over the picture (the rail's buttons, the undo's pill, the draft's tick and cross) step aside
-- from them until they are gone.
-- Nothing here imports FAF's files: a window of the front end says itself here too.

-- Each control said, weakly: what it covers, a function that gives its box, or false for the control's own box.
local said = setmetatable({}, {__mode = 'k'})

--- `control` was opened, and covers its own box; or the box `covers(control)` gives, left, top, right and bottom.
function Add(control, covers)
    said[control] = covers or false
end

local function OwnBox(control)
    return control.Left(), control.Top(), control.Right(), control.Bottom()
end

--- Whether the control shows: it is not destroyed, and neither it nor a control that holds it is hidden.
local function Shows(control)
    if IsDestroyed(control) then
        return false
    end
    local at = control
    while at do
        if at:IsHidden() then
            return false
        end
        at = at:GetParent()
    end
    return true
end

--- The boxes of those that show now, in the picture's pixels: left, top, right and bottom of each in one list. One whose box
--- cannot be worked out is left out, as it is drawn nowhere either.
function Boxes()
    local boxes = {}
    for control, covers in said do
        if Shows(control) then
            local ok, left, top, right, bottom = pcall(covers or OwnBox, control)
            if ok and right > left and bottom > top then
                table.insert(boxes, left)
                table.insert(boxes, top)
                table.insert(boxes, right)
                table.insert(boxes, bottom)
            end
        end
    end
    return boxes
end
