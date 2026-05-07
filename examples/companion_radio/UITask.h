#pragma once

// Conditional include based on UI mode
#ifdef HEADLESS_UI
  #include "ui-keyboard/UITask.h"
#else
  #include "ui-new/UITask.h"
#endif
