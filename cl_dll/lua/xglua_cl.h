#ifndef XGLUA_CL_H
#define XGLUA_CL_H

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

// Client libraries
void XGLua_CL_RegisterSurface( lua_State *L );
void XGLua_CL_RegisterNet( lua_State *L );
void XGLua_CL_NetHookMessages( void );
void XGLua_CL_NetReset( void );

extern lua_State *g_xglua_cl_L;

#endif // XGLUA_CL_H
