// Host shim for avrlib/base.h, enough to compile upstream Grids'
// pattern_generator.cc and resources.cc unchanged (the avrlib submodule is
// not checked out in mutableinstruments/). Reference build only.

#ifndef SHIM_AVRLIB_BASE_H_
#define SHIM_AVRLIB_BASE_H_

#include <stdint.h>
#include <string.h>

#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&);               \
  void operator=(const TypeName&)
#endif

#include <avr/pgmspace.h>

#endif  // SHIM_AVRLIB_BASE_H_
