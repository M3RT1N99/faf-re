-- The UI's factor in a session (android/TOUCH_UI.md). The app works out the HUD's factor from the screen. Some of FAF's
-- panels and dialogs need more lines than the HUD's factor leaves: they get a smaller one, through FAF's own
-- SetPixelScaleFactor. FAF's layout functions read that factor when they are evaluated, which is later than when
-- they are set, and that makes two kinds of them:
--
--   Dialog  covers the game and builds more as it is used, from places a hook does not reach: its factor stays for as
--           long as it is open. What the HUD builds meanwhile runs through AtHud.
--   Built   leaves the HUD in use and builds nothing later: its factor stays only while it is built, and all it made
--           is evaluated before the HUD's factor returns.

local LayoutHelpers = import('/lua/maui/layouthelpers.lua')

-- The HUD's factor; false outside a session, where everything here stands aside.
local hudFactor = false
-- The dialogs that are open with a factor of their own, the newest last.
local open = {}
-- What is being built: {factor, made = the controls it has made so far}.
local building = false

local function Apply()
    local newest = open[table.getn(open)]
    local factor = building and building.factor or newest and newest.factor or hudFactor
    if factor then
        LayoutHelpers.SetPixelScaleFactor(factor)
    end
end

local function Close(entry)
    for index, other in open do
        if other == entry then
            table.remove(open, index)
            break
        end
    end
    Apply()
end

--- When the dialog's root is gone, so is the dialog. A thread looks, as a dialog's OnDestroy is its own to set at any time.
local function CloseWhenGone(entry)
    ForkThread(function()
        while not IsDestroyed(entry.root) do
            WaitFrames(1)
        end
        Close(entry)
    end)
end

--- Calls build at the entry's factor and gives back all it returned; an error goes on from here, with the factor as it was.
local function Build(entry, build, arguments)
    local before = building
    building = entry
    Apply()
    local results = {pcall(build, unpack(arguments))}
    building = before
    Apply()
    if not results[1] then
        error(results[2], 0)
    end
    table.remove(results, 1)
    return results
end

--- A hook's work that runs after the hook has loaded: FAF's Lua changes under it, and that may cost a feature, not the game.
function Try(name, work)
    local ok, problem = pcall(work)
    if not ok then
        WARN('fafre_ui: ' .. name .. ': ' .. tostring(problem))
    end
end

--- The most a dialog of this many of FAF's lines can have on the screen, which is never more than the HUD's.
function Fit(lines)
    return math.min(hudFactor, GetFrame(0).Height() / lines)
end

--- `build` opens a dialog that needs `lines` of FAF's lines: the dialog keeps its factor until it is gone. `root`, if it
--- is given, says which control that goes by, where it is not the first that `build` makes.
function Dialog(lines, build, root)
    return function(...)
        if not hudFactor then
            return build(unpack(arg))
        end
        local entry = {factor = Fit(lines), made = {}}
        local results = Build(entry, build, arg)
        entry.root = root and root() or entry.made[1]
        if entry.root then
            table.insert(open, entry)
            Apply()
            CloseWhenGone(entry)
        end
        return unpack(results)
    end
end

--- `build` makes a panel beside the HUD that needs `lines` of FAF's lines, and does not build more later.
function Built(lines, build)
    return function(...)
        if not hudFactor then
            return build(unpack(arg))
        end
        local entry = {factor = Fit(lines), made = {}}
        local results = Build(entry, function()
            local inner = {build(unpack(arg))}
            for _, control in entry.made do
                pcall(function()
                    return control.Left(), control.Top(), control.Right(), control.Bottom(), control.Width(), control.Height()
                end)
            end
            return unpack(inner)
        end, {})
        return unpack(results)
    end
end

--- A window of FAF's whose default place is taken at this factor: it goes to its new one, in FAF's pixels, if the user has
--- not put it somewhere (`goNow`), and its reset button brings it back.
function GiveWindowHome(window, resetButton, goNow, left, top, right, bottom)
    local function GoHome()
        window.Left:Set(LayoutHelpers.ScaleNumber(left))
        window.Top:Set(LayoutHelpers.ScaleNumber(top))
        window.Right:Set(LayoutHelpers.ScaleNumber(right))
        window.Bottom:Set(LayoutHelpers.ScaleNumber(bottom))
    end
    if goNow then
        GoHome()
    end
    local Reset = resetButton.OnClick
    resetButton.OnClick = function(self, modifiers)
        Reset(self, modifiers)
        GoHome()
        window:SaveWindowLocation()
    end
end

--- What `hud` builds is the HUD's, also while a dialog is open.
function AtHud(hud)
    if not hud then
        error('nothing to call')
    end
    return function(...)
        if table.getn(open) == 0 or not hudFactor then
            return hud(unpack(arg))
        end
        return unpack(Build({factor = hudFactor, made = {}}, hud, arg))
    end
end

local function WatchConstructors()
    for _, kind in {'Bitmap', 'Border', 'Edit', 'Group', 'Histogram', 'ItemList', 'Movie', 'Scrollbar', 'Text', 'MapPreview', 'Mesh'} do
        local name = 'InternalCreate' .. kind
        local original = rawget(_G, name)
        if original then
            _G[name] = function(...)
                if building then
                    table.insert(building.made, arg[1])
                end
                return original(unpack(arg))
            end
        end
    end
end

--- The factor of the session's HUD, which the app works out from the screen.
function StartSession()
    local given = GetCommandLineArg('/fafre_hud_scale', 1)
    local factor = given and tonumber(given[1])
    if factor and factor > 0 then
        hudFactor = factor
        LayoutHelpers.SetPixelScaleFactor(factor)
        WatchConstructors()
        _G.OnSync = AtHud(OnSync)
        LOG('fafre_ui: the HUD at ' .. factor)
    end
end
