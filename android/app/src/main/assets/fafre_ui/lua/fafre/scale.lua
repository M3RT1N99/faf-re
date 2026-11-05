-- The UI's factor, the fingers and the pages (android/TOUCH_UI.md). The app works out the HUD's factor from the screen. Some of FAF's
-- panels and dialogs need more lines than the HUD's factor leaves: they get a smaller one, through FAF's own
-- SetPixelScaleFactor. FAF's layout functions read that factor when they are evaluated, which is later than when
-- they are set, and that makes two kinds of them:
--
--   Dialog  covers the game and builds more as it is used, from places a hook does not reach: its factor stays for as
--           long as it is open. What the HUD builds meanwhile runs through AtHud. In the front end it holds too, as there is
--           no HUD: the dialog is as large on the glass as a session's.
--   Built   leaves the HUD in use and builds nothing later: its factor stays only while it is built, and all it made
--           is evaluated before the HUD's factor returns.
--
-- A page is a screen that a finger reads larger than the screen and scrolls through (Page). Whose the fingers are follows: the
-- front end's are the menus', a session's are the game's, and the menus' again while a dialog with a factor of its own or a page is open.

local LayoutHelpers = import('/lua/maui/layouthelpers.lua')

-- Whether this state is a session's.
local inSession = false
-- The HUD's factor; false outside a session, where the front end's factor of 1 holds and dialogs stand aside unless they say otherwise.
local hudFactor = false
-- The dialogs that are open with a factor of their own, the newest last.
local open = {}
-- The pages that are open, the newest last: {root = the control that goes by, left, top, right, bottom}.
local pages = {}
-- What is being built: {factor, made = the controls it has made so far}.
local building = false

local function Apply()
    local newest = open[table.getn(open)]
    local factor = building and building.factor or newest and newest.factor or hudFactor or 1
    LayoutHelpers.SetPixelScaleFactor(factor)
end

--- What the fingers do now, which the app goes by when the next one comes down.
local function SayFingers()
    FafreSetFingers(not inSession or table.getn(open) > 0 or table.getn(pages) > 0)
end

local function SayPage()
    local page = pages[table.getn(pages)]
    if page then
        FafreSetPage(page.left, page.top, page.right, page.bottom)
    else
        FafreSetPage()
    end
end

local function RemoveFrom(list, entry)
    for index, other in list do
        if other == entry then
            table.remove(list, index)
            break
        end
    end
end

--- When the dialog's or the page's root goes, so does it, in that moment: what a dialog does as it closes is at the factor from before
--- it. A child of the root says so, as a dialog's OnDestroy is its own to set at any time.
local function WhenGone(root, close)
    local watcher = import('/lua/maui/group.lua').Group(root)
    LayoutHelpers.FillParent(watcher, root)
    watcher:DisableHitTest()
    watcher.OnDestroy = close
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

--- Whatever is due of a control's layout functions runs now, at the factor there is now.
local function Evaluate(control)
    return control.Left(), control.Top(), control.Right(), control.Bottom(), control.Width(), control.Height()
end

--- A hook's work that runs after the hook has loaded: FAF's Lua changes under it, and that may cost a feature, not the game.
function Try(name, work)
    local ok, problem = pcall(work)
    if not ok then
        WARN('fafre_ui: ' .. name .. ': ' .. tostring(problem))
    end
end

--- What the app has worked out for the HUD from the screen, whatever the UI scale says.
local function AppFactor()
    local given = GetCommandLineArg('/fafre_hud_scale', 1)
    return given and tonumber(given[1]) or 1
end

--- How much larger the screen shows the picture than its pixels now, for what wants a size on the glass.
function PageZoom()
    local page = pages[table.getn(pages)]
    return page and math.max(1, GetFrame(0).Width() / (page.right - page.left)) or 1
end

--- The factor at which FAF's sizes come out on the glass as a session's do at the app's factor, on a page that shows the picture
--- `zoom` times as large, which by default is the page there is now.
function GlassFactor(zoom)
    return math.max(1, AppFactor() / (zoom or PageZoom()))
end

--- The most a dialog of this many of FAF's lines, and if it says so of this width, can have on the screen, which is never more than
--- the HUD's size on the glass.
function Fit(lines, width)
    local frame = GetFrame(0)
    local most = math.min(hudFactor or GlassFactor(), frame.Height() / lines)
    return width and math.min(most, frame.Width() / width) or most
end

--- `build` at this factor, which the dialog it opens keeps until it is gone. `root`, if it is given, says which control that goes by.
local function OpenDialog(factor, build, arguments, root)
    local entry = {factor = factor, made = {}}
    local results = Build(entry, build, arguments)
    entry.root = root and root() or entry.made[1]
    if entry.root then
        table.insert(open, entry)
        Apply()
        SayFingers()
        WhenGone(entry.root, function()
            RemoveFrom(open, entry)
            Apply()
            SayFingers()
        end)
    end
    return unpack(results)
end

--- `build` opens a dialog that needs `lines` of FAF's lines, and if it says so `width` of FAF's pixels: the dialog keeps its factor until it
--- is gone. `root`, if it is given, says which control that goes by, where it is not the first that `build` makes.
function Dialog(lines, build, root, width)
    return function(...)
        return OpenDialog(Fit(lines, width), build, arg, root)
    end
end

--- As Dialog, for a dialog whose factor is its own and larger than the HUD's: `factor` says it, and it holds in the front end too.
function DialogAt(factor, build, root)
    return function(...)
        return OpenDialog(factor(), build, arg, root)
    end
end

--- `build` opens a screen that a finger reads on a page (android/TOUCH_UI.md). `find` is given what `build` made and says which
--- control the page goes by and has the place of, and in a table of left, top, right, bottom any side where it has another. Built
--- within a dialog's `build`, it is at the dialog's factor, and what it makes is the dialog's.
function Page(build, find)
    return function(...)
        local entry = {factor = building and building.factor, made = building and building.made or {}, page = true}
        local results = Build(entry, build, arg)
        Try('a page', function()
            local root, box = find(entry.made)
            box = box or {}
            local page = {
                root = root, left = box.left or root.Left(), top = box.top or root.Top(),
                right = box.right or root.Right(), bottom = box.bottom or root.Bottom(),
            }
            table.insert(pages, page)
            SayPage()
            SayFingers()
            WhenGone(root, function()
                RemoveFrom(pages, page)
                SayPage()
                SayFingers()
            end)
        end)
        return unpack(results)
    end
end

--- How many of the picture's pixels make this many mm on the glass now. A build icon's 48 pixels are 7.3 mm at the app's factor.
function PicturePixels(mm)
    return mm * AppFactor() * 48 / 7.3 / PageZoom()
end

--- A list of text has rows as high as its font: FAF's font, larger, for rows of this many mm on the glass.
function FitRows(list, mm)
    local wanted = PicturePixels(mm)
    if list:GetRowHeight() < wanted then
        local points = math.ceil(list._font._pointsize() * wanted / list:GetRowHeight())
        list:SetFont(list._font._family(), (points + 0.5) / LayoutHelpers.GetPixelScaleFactor())
    end
end

--- A dialog's frame, a picture as high as `dialog`, without `cut` of its lines from `upper` down. Of the picture its top and its bottom show, as
--- MAUI's SetUV shows a part of a picture; what lies below keeps its distance from the dialog's bottom. What reaches across is the caller's.
function CutFrame(dialog, file, upper, cut)
    local Bitmap = import('/lua/maui/bitmap.lua').Bitmap
    local fullHeight = dialog.Height()
    local lower = fullHeight - upper - cut
    local line = dialog.Top() + upper
    local function Part(from, to, height)
        local part = Bitmap(dialog, file)
        part:SetUV(0, from, 1, to)
        part.Height:Set(height)
        part.Width:Set(dialog.Width)
        part.Left:Set(dialog.Left)
        -- Under what is on the dialog, whatever came first.
        part.Depth:Set(dialog.Depth)
        part:DisableHitTest()
        return part
    end
    dialog:ApplyFunction(function(control)
        if control ~= dialog and control.Top() >= line then
            local fromBottom = dialog.Bottom() - control.Top()
            control.Top:Set(function() return dialog.Bottom() - fromBottom end)
        end
    end)
    dialog.Height:Set(upper + lower)
    dialog:SetAlpha(0)
    Part(0, upper / fullHeight, upper).Top:Set(dialog.Top)
    Part(1 - lower / fullHeight, 1, lower).Top:Set(function() return dialog.Bottom() - lower end)
end

--- A dialog that stands around one control and is higher than the screen with `margin` of FAF's lines above and below: the control and the
--- dialog's frame lose what is too much, from the middle of `even`, the part of the frame's picture under the control's list that looks the same all the way.
function FitByList(control, file, margin, even)
    local dialog = control:GetParent()
    local cut = dialog.Height() + 2 * LayoutHelpers.ScaleNumber(margin) - GetFrame(0).Height()
    if cut > 0 then
        CutFrame(dialog, file, math.floor((even.Top() + even.Bottom() - cut) / 2 - dialog.Top()), cut)
        control.Height:Set(control.Height() - cut)
    end
end

--- The picture's first column that the screen shows.
function ShownLeft()
    local page = pages[table.getn(pages)]
    return page and page.left or GetFrame(0).Left()
end

--- What the screen shows of the picture's lines at a time, with the margins that ShowLines leaves.
function ShownLines()
    return GetFrame(0).Height() / PageZoom() - 16
end

--- The page goes where these of the picture's lines show, if a page is open, and that page stays where it stood if what is built now brings a page of its own.
function ShowLines(top, bottom)
    if not (building and building.page) then
        FafreShowLines(top, bottom)
    end
end

--- The page goes where this control shows, from its top if it is higher than the screen shows.
function ShowTopOf(control)
    ShowLines(control.Top(), math.min(control.Bottom(), control.Top() + ShownLines()))
end

--- A new front end: the state that declared the last page and fingers has gone with them.
function StartFrontEnd()
    SayFingers()
    SayPage()
end

--- `build` makes something that does not build more later at the factor that `factor` says, for which it need not hold longer.
function BuiltAt(factor, build)
    return function(...)
        local entry = {factor = factor(), made = {}}
        local results = Build(entry, function()
            local inner = {build(unpack(arg))}
            for _, control in entry.made do
                pcall(Evaluate, control)
            end
            return unpack(inner)
        end, {})
        return unpack(results)
    end
end

--- `build` makes a panel beside the HUD that needs `lines` of FAF's lines, and does not build more later.
function Built(lines, build)
    local built = BuiltAt(function() return Fit(lines) end, build)
    return function(...)
        if not hudFactor then
            return build(unpack(arg))
        end
        return built(unpack(arg))
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

--- All under this control, but for what the dialogs that hold a factor made, is evaluated now. ApplyFunction goes one level down.
local function EvaluateUnder(control, dialogs)
    pcall(Evaluate, control)
    control:ApplyFunction(function(child)
        if child ~= control and not dialogs[child] then
            EvaluateUnder(child, dialogs)
        end
    end)
end

--- What `screen` does is the front end's, at its factor, also under a dialog that holds another: FAF's lobby goes on with its messages under
--- the Unit Manager. That dialog's factor is back when this returns, so what is due under `root()` is evaluated here.
function AtFrontEnd(screen, root)
    if not screen then
        error('nothing to call')
    end
    return function(...)
        if LayoutHelpers.GetPixelScaleFactor() == 1 then
            return screen(unpack(arg))
        end
        return unpack(Build({factor = 1, made = {}}, function(...)
            local results = {screen(unpack(arg))}
            Try('what is due of a screen', function()
                local control = root()
                if control and not IsDestroyed(control) then
                    local dialogs = {}
                    for _, entry in open do
                        for _, made in entry.made do
                            dialogs[made] = true
                        end
                    end
                    EvaluateUnder(control, dialogs)
                end
            end)
            return unpack(results)
        end, arg))
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

--- Whether this state is a session's.
function InSession()
    return inSession
end

--- The factor of the session's HUD: what the app works out from the screen, times the user's UI scale, within the lines the HUD needs.
function StartSession()
    inSession = true
    SayPage()
    SayFingers()
    local given = GetCommandLineArg('/fafre_hud_scale', 1)
    local factor = given and tonumber(given[1])
    if factor and factor > 0 then
        local option = import('/lua/user/prefs.lua').GetFromCurrentProfile('options').ui_scale or 1
        local most = GetCommandLineArg('/fafre_hud_most', 1)
        hudFactor = math.min(factor * option, math.max(factor, most and tonumber(most[1]) or factor))
        LayoutHelpers.SetPixelScaleFactor(hudFactor)
        FafreSetHud(hudFactor)
        _G.OnSync = AtHud(OnSync)
        LOG('fafre_ui: the HUD at ' .. hudFactor .. ', the app\'s ' .. factor .. ' and the UI scale ' .. option)
    end
end
