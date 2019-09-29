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

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-compare"
#endif

#include "nuklear_love.c"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif


#ifdef __cplusplus
}
#endif


#endif /* EOF */
