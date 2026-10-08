-- Frame-harness navigation script (port/graphics/capture, `/galscript`): FAF's main menu -> the
-- skirmish lobby, as a click on the menu's Skirmish button would do it.
--
-- The harness runs this file once in the user Lua state on the first frame and then calls
-- GalHarnessStep(frame) once per CScApp::Main, before that frame's paint (GalCapture.cpp). A string
-- returned is logged as the action taken (harness.json "script"); an error ends the run (exit 14).
-- No real input is involved: the window is hidden and the harness drops every input message.
--
-- The action is the button's own: FAF's main menu (lua/ui/menus/main.lua, CreateUI) builds the
-- Skirmish button with `action = function() ButtonSkirmish() end` and defines ButtonSkirmish in the
-- module's environment. ButtonSkirmish asks the tutorial question first (the harness profile has
-- answered it: MenuTutorialPrompt = true), slides the menu out (MenuHide), and then creates the
-- lobby: lobby.CreateLobby('None', ...) and lobby.HostGame(..., singlePlayer = true) on the
-- profile's last scenario or UIUtil.defaultScenario (Seton's Clutch). The lobby draws the map
-- preview (ResourceMapPreview, the scenario's preview image through RD3DTextureResource).

local SKIRMISH_FRAME = 90 -- the menu has settled from frame 45 on (port/graphics/capture/README.md)

local steps = {}

steps[SKIRMISH_FRAME] = function()
    local main = import('/lua/ui/menus/main.lua')
    if type(main.ButtonSkirmish) ~= 'function' then
        error('the main menu module has no ButtonSkirmish (is the menu up?)')
    end
    main.ButtonSkirmish()
    return 'main menu: Skirmish (ButtonSkirmish, the Skirmish button\'s action)'
end

function GalHarnessStep(frame)
    local step = steps[frame]
    if step then
        return step()
    end
end
