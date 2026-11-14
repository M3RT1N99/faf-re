
-- The UI package (android/TOUCH_UI.md), for FAF's patch notes from 3841 on (ui/lobby/changelog.lua's hook is for those before): they
-- open on the lobby's page, from their top wherever the page stands, and a finger picks a patch from the list: FAF's 12 points
-- would be 2.3 mm a row on that page. When they open by themselves the page is not there yet, so this goes by what that page will
-- be, not by Scale.FitRows.
do
    local ok, problem = pcall(function()
        local Scale = import('/fafre_ui/lua/fafre/scale.lua')
        local Init = Scale.Needed(ChangelogDialog.__init, 'ChangelogDialog.__init')
        ChangelogDialog.__init = function(self, parent)
            Init(self, parent)
            Scale.Try('the patch notes', function()
                Scale.Needed(self.ContentPatchesList, 'the patch notes\' ContentPatchesList'):SetFont(UIUtil.bodyFont, 18)
                Scale.ShowTopOf(self)
            end)
        end
    end)
    if not ok then
        WARN('fafre_ui: ui/lobby/changelog/changelogdialog.lua: ' .. tostring(problem))
    end
end
