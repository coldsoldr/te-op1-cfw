// Host shim for avrlib/resources_manager.h: only the template names that
// grids/resources.h mentions in a typedef.

#ifndef SHIM_AVRLIB_RESOURCES_MANAGER_H_
#define SHIM_AVRLIB_RESOURCES_MANAGER_H_

namespace avrlib {

template<const prog_char* const* strings, const prog_uint16_t* const* tables>
struct ResourcesTables { };

template<typename ResourceId, typename Tables>
class ResourcesManager { };

}  // namespace avrlib

#endif  // SHIM_AVRLIB_RESOURCES_MANAGER_H_
