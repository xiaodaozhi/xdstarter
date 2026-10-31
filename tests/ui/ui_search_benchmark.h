#pragma once

// Explicit opt-in synthetic UI probe. Never installs input hooks or opens a
// real application directory. Native display is restricted to hosted runners.
int runUiSearchBenchmark();
// Measures the real host's reveal sequence with synthetic platform adapters.
int runUiRevealBenchmark();
