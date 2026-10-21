// tests/ui/native_chrome_probe.h

#pragma once

// Explicit opt-in Windows rendering probe. Never part of the default test suite.
int runNativeChromeProbe();

// Requires two real monitors with different render scales; no global input.
int runNativeScreenTransitionProbe();
