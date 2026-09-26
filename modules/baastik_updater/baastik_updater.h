/*
 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 baastik_updater
  vendor:             Baastik Labs
  version:            1.0.0
  name:               Baastik Labs plugin updater
  description:        Checks a plugin's own GitHub release channel for updates,
                      downloads the installer and launches it.
  license:            Proprietary
  minimumCppStandard: 20
  dependencies:       juce_core, juce_events, juce_gui_basics, juce_data_structures

 END_JUCE_MODULE_DECLARATION
*/

#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "updater/Version.h"
#include "updater/UpdateChecker.h"
#include "updater/UpdateBanner.h"
