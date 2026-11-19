#ifndef XGLUA_CL_H
#define XGLUA_CL_H

// cl_util.h defines a function-like fabs() macro that breaks <string>'s
// internals; save and restore it around the standard include.
#ifdef fabs
#define XGLUA_CL_FABS_DEFINED 1
#undef fabs
#endif
#include <string>
#ifdef XGLUA_CL_FABS_DEFINED
#undef XGLUA_CL_FABS_DEFINED
#define fabs(x)	   ((x) > 0 ? (x) : 0 - (x))
#endif

extern "C" {
#include "lua.h"
}

// Client VM lifecycle
void XGLua_CL_Init( void );         // HUD_Init: register commands & XGLNet hook
void XGLua_CL_Startup( void );      // HUD_VidInit: recreate client Lua VM + load scripts
void XGLua_CL_Shutdown( void );     // teardown client VM
void XGLua_CL_Frame( double time ); // HUD_Frame: timer.Tick + hook Think
void XGLua_CL_HUDPaint( void );     // CHud::Redraw: hook HUDPaint

void XGLua_CL_ReportError( lua_State *L, const char *context );

// Client-side memory VFS (CSLua downloads)
void XGLua_CL_MemVFSStore( const char *relpath, const char *data, size_t len );
bool XGLua_CL_MemVFSFind( const char *relpath, std::string &out );
void XGLua_CL_MemVFSClear( void );

// Client libraries
void XGLua_CL_RegisterSurface( lua_State *L );
void XGLua_CL_RegisterNet( lua_State *L );
void XGLua_CL_NetHookMessages( void );
void XGLua_CL_NetReset( void );

extern lua_State *g_xglua_cl_L;

#endif // XGLUA_CL_H
