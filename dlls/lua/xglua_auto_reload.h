#ifndef XGLUA_AUTO_RELOAD_H
#define XGLUA_AUTO_RELOAD_H

// Called only when neither fire button is held and no reload is in progress.
// Lua SWEP primary-fire deadlines use absolute world time.
inline bool XGLua_ShouldAutoReload( int clip, int maxClip, int reserve,
                                    bool disabled, float now, float nextFire )
{
    return !disabled && maxClip > 0 && clip == 0 && reserve > 0 && now > nextFire;
}
#endif
