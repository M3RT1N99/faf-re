"""FAF's init with the census folder mounted and hooked, beside the app's UI package. Usage: mount_in_init.py <in> <out>"""
import sys

ANCHOR = "custom_vault_path = InitFileDir"
text = open(sys.argv[1], encoding="latin-1", newline="").read()
if text.count(ANCHOR) != 1:
    sys.exit("expected one vault line")
add = "MountDirectory(InitFileDir .. '/../fafre_census', '/fafre_census')\ntable.insert(hook, '/fafre_census')\n"
text = text.replace(ANCHOR, add + ANCHOR)
open(sys.argv[2], "w", encoding="latin-1", newline="").write(text)
print("init written")
