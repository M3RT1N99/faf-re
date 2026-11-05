
-- The UI package (android/TOUCH_UI.md): a tap or Back skips the timeline's film on the campaign's first visit, and FAF then calls the operation's briefing with
-- arguments it no longer takes, which leaves a black screen. It goes where the film's end goes.
do
    local ok, problem = pcall(function()
        local Timeline = TimelineFMV
        TimelineFMV = function()
            return Timeline(true)
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/campaign/selectcampaign.lua: ' .. tostring(problem))
    end
end
