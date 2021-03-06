/*
 * LOVE-Nuklear - MIT licensed; no warranty implied; use at your own risk.
 * authored from 2015-2016 by Micha Mettke
 * adapted to LOVE in 2016 by Kevin Harrison
 * adapted to taiwins in 2019 by Sichem Zhou
 */
#ifndef NK_LOVE_H
#define NK_LOVE_H

#include <lua.h>

#ifdef __cplusplus
extern "C" {
#endif

/* we just include this file in the other projects */
struct nk_love_context;
struct nk_context;

LUALIB_API int luaopen_nuklear(lua_State *lua_State);
LUALIB_API struct nk_love_context *nk_love_new_ui(struct lua_State *L, struct nk_context *ctx);
LUALIB_API void nk_love_destroy(lua_State *L, struct nk_love_context *context);
LUALIB_API struct nk_love_context *nk_love_get_ui(struct lua_State *L);
LUALIB_API void nk_love_getfield_ui(struct lua_State *L);

#include "nuklear_love.c"

#ifdef __cplusplus
}
#endif


#endif /* EOF */
