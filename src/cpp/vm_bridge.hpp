/**
 * vm_bridge.hpp — C/C++ bridge: extern "C" wrapper for all C core functions.
 *
 * Include this single header from C++ code to access the entire C core API.
 * All C functions are wrapped with extern "C" for proper linkage.
 */
#ifndef VM_BRIDGE_HPP
#define VM_BRIDGE_HPP

/* Windows SDK (must come before our headers on MinGW) */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

/* C core headers with extern "C" linkage */
extern "C" {
#include "../core/vm_common.h"
#include "../core/vm_engine.h"
#include "../core/vm_db.h"
}

/* Undefine C macros that conflict with C++ identifiers.
   These C macros from vm_common.h are used only by the legacy C code.
   The C++ layer defines its own constants with the same names. */
#ifdef TRAY_TIP
#undef TRAY_TIP
#endif
#ifdef TRAY_UID
#undef TRAY_UID
#endif
#ifdef IDM_SHOW
#undef IDM_SHOW
#endif
#ifdef IDM_CLEANUP
#undef IDM_CLEANUP
#endif
#ifdef IDM_EXIT
#undef IDM_EXIT
#endif

/* Dashboard HTML — embedded SPA string constant */
extern "C" {
extern const char *const DASHBOARD_HTML;
}

#endif /* VM_BRIDGE_HPP */
