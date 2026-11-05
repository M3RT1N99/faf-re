
-- Census of the UI's controls, a measuring tool that never ships: appended to /lua/ui/uimain.lua, the
-- first UI file of every UI state. It wraps the engine's control constructors to learn which Lua line
-- made each control, then every second logs each control not seen before, one line per place and
-- size, in the UI's own pixels:
--   CENSUS <ui scale> <place> <kind> x y w h <flags> <name>
-- flags: e = has an event handler of its own, as a finger's tap counts it (android/TOUCH_UI.md), h = takes hits, s = scrollable.
-- A session's state imports this file before it can run threads, so the thread starts with the first control.
do
    local created = setmetatable({}, {__mode = 'k'})
    local started = false

    local function Clean(source)
        local path = string.gsub(source, '\\', '/')
        local _, tail = string.find(path, 'lua.nx2/', 1, true)
        return tail and string.sub(path, tail + 1) or path
    end

    local function Framework(source)
        return string.find(source, 'lua/maui/', 1, true) or string.find(source, 'lua/system/', 1, true)
            or string.find(source, 'uiutil', 1, true) or string.sub(source, 1, 1) == '='
    end

    local function Place()
        local level = 3
        local found = nil
        while level < 16 do
            local info = debug.getinfo(level, 'Sl')
            if not info then
                break
            end
            local source = Clean(info.source or '?')
            if not Framework(source) then
                local here = string.gsub(source, '^lua/', '') .. ':' .. tostring(info.currentline or 0)
                if found then
                    return found .. '<' .. here
                end
                found = here
            end
            level = level + 1
        end
        return found or '?'
    end

    local function Census()
        LOG('CENSUS started')
        local plainHandler = import('/lua/maui/control.lua').Control.HandleEvent
        local plainScroll = import('/lua/maui/control.lua').Control.IsScrollable
        local LayoutHelpers = import('/lua/maui/layouthelpers.lua')
        local seen = {}
        local logged = 0

        local function Row(control, info)
            if control:IsHidden() then
                return
            end
            local w, h = control.Width(), control.Height()
            if w < 1 or h < 1 then
                return
            end
            local flags = ''
            -- a tooltip's wrapper around the plain handler is no handler
            if control.HandleEvent and control.HandleEvent ~= plainHandler and control.oldHandleEvent ~= plainHandler then
                flags = flags .. 'e'
            end
            if not control:IsHitTestDisabled() then
                flags = flags .. 'h'
            end
            if control.IsScrollable and control.IsScrollable ~= plainScroll then
                flags = flags .. 's'
            end
            local scale = LayoutHelpers.GetPixelScaleFactor()
            local key = info.place .. ' ' .. info.kind .. ' ' .. math.floor(w) .. ' ' .. math.floor(h) .. ' ' .. flags .. ' ' .. scale
            if seen[key] then
                return
            end
            seen[key] = true
            logged = logged + 1
            LOG(string.format('CENSUS %.2f %s %s %d %d %d %d %s %s', scale, info.place, info.kind,
                control.Left(), control.Top(), w, h, flags == '' and '-' or flags, control:GetName() or ''))
        end

        while true do
            WaitSeconds(1)
            for control, info in created do
                -- a control not laid out yet, or one that is gone, has nothing to report
                pcall(Row, control, info)
            end
        end
    end

    local kinds = {'Bitmap', 'Border', 'Edit', 'Group', 'Histogram', 'ItemList', 'Movie', 'Scrollbar', 'Text',
        'MapPreview', 'Mesh'}
    for _, kind in kinds do
        local name = 'InternalCreate' .. kind
        local original = rawget(_G, name)
        if original then
            local thisKind = kind
            _G[name] = function(...)
                created[arg[1]] = {kind = thisKind, place = Place()}
                if not started then
                    started = pcall(ForkThread, Census)
                end
                return original(unpack(arg))
            end
        end
    end
end
