#pragma once

// <shlobj.h> as the effects' shader cache spells it (gpg/gal/effects/EffectRuntime.cpp): the
// engine's POSIX layer has ShlObj.h, and the spelling matters on case-sensitive hosts.
// #include_next continues the search behind this directory, as Windows.h here does.

#include_next <ShlObj.h>
