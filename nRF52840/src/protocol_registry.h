#pragma once

#include <strings.h>

#include "protocols/2ano4_25w2r4g.h"
#include "protocols/gdansk_inspire.h"

// Register future nRF protocol plugins in this table.
inline const RadioProtocol *findProtocol(const char *name) {
  if (!name) return nullptr;
  const RadioProtocol *plugins[] = {
      &GdanskInspireProtocol::plugin(),
      &Fcc2ano425w2r4gProtocol::plugin(),
  };
  for (const RadioProtocol *plugin : plugins)
    if (!strcasecmp(name, plugin->name)) return plugin;
  return nullptr;
}

inline const RadioProtocol *defaultProtocol() {
  return &GdanskInspireProtocol::plugin();
}
