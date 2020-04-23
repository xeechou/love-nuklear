/*
 * LOVE-Nuklear - MIT licensed; no warranty implied; use at your own risk.
 * authored from 2015-2016 by Micha Mettke
 * adapted to LOVE in 2016 by Kevin Harrison
 * adapted to taiwins in 2019 by Sichem Zhou
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LUA_LIB

#include <lua.h>
#include <lauxlib.h>


#ifndef NK_NUKLEAR_H_
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS

#if defined(__GNUC__) /* needed for supress warnings in nuklear headers */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#elif defined (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif

#include "nuklear/nuklear.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined (__clang__)
#pragma clang diagnostic pop
#endif

#else
#pragma message "nuklear already included"
#endif

/*
 * ===============================================================
 *
 *                          INTERNAL
 *
 * ===============================================================
 */

#define luaL_typerror(L, narg, tname) \
	luaL_error(L, "bad argument %d to %s", narg, tname)

#define NK_LOVE_MAX_POINTS 1024
#define NK_LOVE_EDIT_BUFFER_LEN (1024 * 1024)
#define NK_LOVE_COMBOBOX_MAX_ITEMS 1024
#define NK_LOVE_MAX_FONTS 10
#define NK_LOVE_MAX_RATIOS 1024
#define NK_LOVE_GRADIENT_RESOLUTION 32
#define NK_LOVE_METATABLE "metatable"
#define NK_LOVE_NUKLEAR "nuklear"

static char edit_buffer[NK_LOVE_EDIT_BUFFER_LEN];
static const char *combobox_items[NK_LOVE_COMBOBOX_MAX_ITEMS];
static float points[NK_LOVE_MAX_POINTS];
static float layout_ratios[NK_LOVE_MAX_RATIOS];
static struct nk_user_font fonts[NK_LOVE_MAX_FONTS];
static lua_State *L;

struct nk_love_context {
	struct nk_context *nkctx;
	int font_count; //cleaned up in frame begin
	int layout_ratio_count; //cleaned up in frame begin
	float T[9]; //transform
	float Ti[9]; //inverse, cleaned up in frame begin
	int transform_allowed;
};

static struct nk_love_context *CONTEXT;


static void nk_love_assert(int pass, const char *msg)
{
	if (!pass) {
		lua_Debug ar;
		ar.name = NULL;
		if (lua_getstack(L, 0, &ar))
			lua_getinfo(L, "n", &ar);
		if (ar.name == NULL)
			ar.name = "?";
		luaL_error(L, msg, ar.name);
	}
}

static void nk_love_assert_argc(int pass)
{
	nk_love_assert(pass, "wrong number of arguments to '%s'");
}

static struct nk_love_context *
nk_love_checkcontext(int index)
{
	if (index < 0)
		index += lua_gettop(L) + 1;
	struct nk_love_context *ctx = luaL_checkudata(L, index, NK_LOVE_METATABLE);
	/* if (lua_isuserdata(L, index)) { */
	/*	lua_getfield(L, LUA_REGISTRYINDEX, "nuklear"); */
	/*	lua_getfield(L, -1, "metatable"); */
	/*	lua_getmetatable(L, index); */
	/*	int is_context = lua_equal(L, -1, -2); */
	/*	lua_pop(L, 3); */
	/*	if (is_context) */
	/*		return lua_touserdata(L, index); */
	/* } */
	/* luaL_typerror(L, index, "Nuklear context"); */
	return ctx;
}

static void nk_love_assert_context(int index)
{
	struct nk_love_context *ctx = nk_love_checkcontext(index);
	ctx->transform_allowed = 0;
	/* nk_love_assert(ctx == context, "%s: UI calls must reside between ui:frameBegin and ui:frameEnd"); */
}

static void nk_love_pushregistry(const char *name)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "nuklear"); //1
	lua_getfield(L, -1, "newui"); //2
	struct nk_love_context *context = luaL_checkudata(L, -1, "metatable");
	lua_pop(L, 1); //1
	lua_rawgetp(L, -1, context); //2
	lua_getfield(L, -1, name); //3
	lua_replace(L, -2); //2
	lua_pop(L, 2); //0
}

static int nk_love_is_type(int index, const char *type)
{
	if (index < 0)
		index += lua_gettop(L) + 1;
	if (lua_isuserdata(L, index)) {
		lua_getfield(L, index, "typeOf");
		if (lua_isfunction(L, -1)) {
			lua_pushvalue(L, index);
			lua_pushstring(L, type);
			lua_call(L, 2, 1);
			if (lua_isboolean(L, -1)) {
				int is_type = lua_toboolean(L, -1);
				lua_pop(L, 1);
				return is_type;
			}
		}
	}
	return 0;
}

static float nk_love_get_text_width(nk_handle handle, float height,
	const char *text, int len)
{
	nk_love_pushregistry("font");
	lua_rawgeti(L, -1, handle.id);
	lua_getfield(L, -1, "getWidth");
	lua_replace(L, -3);
	lua_pushlstring(L, text, len);
	lua_call(L, 2, 1);
	float width = lua_tonumber(L, -1);
	lua_pop(L, 1);
	return width;
}

static void nk_love_checkFont(int index, struct nk_user_font *font)
{
	if (index < 0)
		index += lua_gettop(L) + 1;
	if (!nk_love_is_type(index, "Font"))
		luaL_typerror(L, index, "Font");
	nk_love_pushregistry("font");
	lua_pushvalue(L, index);
	int ref = luaL_ref(L, -2);
	lua_getfield(L, index, "getHeight");
	lua_pushvalue(L, index);
	lua_call(L, 1, 1);
	float height = lua_tonumber(L, -1);
	font->userdata = nk_handle_id(ref);
	font->height = height;
	font->width = nk_love_get_text_width;
	lua_pop(L, 2);
}

static void nk_love_checkImage(int index, struct nk_image *image)
{
	if (index < 0)
		index += lua_gettop(L) + 1;
	if (nk_love_is_type(index, "Image") || nk_love_is_type(index, "Canvas")) {
		lua_getglobal(L, "love");
		lua_getfield(L, -1, "graphics");
		lua_getfield(L, -1, "newQuad");
		lua_pushnumber(L, 0);
		lua_pushnumber(L, 0);
		lua_getfield(L, index, "getDimensions");
		lua_pushvalue(L, index);
		lua_call(L, 1, 2);
		lua_pushvalue(L, -2);
		lua_pushvalue(L, -2);
		lua_call(L, 6, 1);
		lua_newtable(L);
		lua_pushvalue(L, index);
		lua_rawseti(L, -2, 1);
		lua_replace(L, -3);
		lua_rawseti(L, -2, 2);
		lua_replace(L, -2);
	} else if (lua_istable(L, index)) {
		lua_createtable(L, 2, 0);
		lua_rawgeti(L, index, 2);
		lua_rawgeti(L, index, 1);
		if ((nk_love_is_type(-1, "Image") || nk_love_is_type(-1, "Canvas")) && nk_love_is_type(-2, "Quad")) {
			lua_rawseti(L, -3, 1);
			lua_rawseti(L, -2, 2);
		} else {
			luaL_argerror(L, index, "expecting {Image, Quad} or {Canvas, Quad}");
		}
	} else {
		luaL_argerror(L, index, "expecting Image or Canvas or {Image, Quad} or {Canvas, Quad}");
	}
	nk_love_pushregistry("image");
	lua_pushvalue(L, -2);
	int ref = luaL_ref(L, -2);
	image->handle = nk_handle_id(ref);
	lua_pop(L, 2);
}

static int nk_love_is_hex(char c)
{
	return (c >= '0' && c <= '9')
			|| (c >= 'a' && c <= 'f')
			|| (c >= 'A' && c <= 'F');
}

static int nk_love_is_color(int index)
{
	if (index < 0)
		index += lua_gettop(L) + 1;
	if (lua_isstring(L, index)) {
		size_t len;
		const char *color_string = lua_tolstring(L, index, &len);
		if ((len == 7 || len == 9) && color_string[0] == '#') {
			int i;
			for (i = 1; i < len; ++i) {
				if (!nk_love_is_hex(color_string[i]))
					return 0;
			}
			return 1;
		}
	}
	return 0;
}

static struct nk_color nk_love_checkcolor(int index)
{
	if (index < 0)
		index += lua_gettop(L) + 1;
	if (!nk_love_is_color(index)) {
		if (lua_isstring(L, index)){
			const char *msg = lua_pushfstring(L, "bad color string '%s'", lua_tostring(L, index));
			luaL_argerror(L, index, msg);
		} else {
			luaL_typerror(L, index, "color string");
		}
	}
	size_t len;
	const char *color_string = lua_tolstring(L, index, &len);
	int r, g, b, a = 255;
	sscanf(color_string, "#%02x%02x%02x", &r, &g, &b);
	if (len == 9) {
		sscanf(color_string + 7, "%02x", &a);
	}
	struct nk_color color = {r, g, b, a};
	return color;
}

static struct nk_colorf nk_love_checkcolorf(int index) {
	return nk_color_cf(nk_love_checkcolor(index));
}

static void nk_love_color(int r, int g, int b, int a, char *color_string)
{
	r = NK_CLAMP(0, r, 255);
	g = NK_CLAMP(0, g, 255);
	b = NK_CLAMP(0, b, 255);
	a = NK_CLAMP(0, a, 255);
	const char *format_string;
	if (a < 255) {
		format_string = "#%02x%02x%02x%02x";
	} else {
		format_string = "#%02x%02x%02x";
	}
	sprintf(color_string, format_string, r, g, b, a);
}

static nk_flags nk_love_parse_window_flags(int flags_begin, int flags_end)
{
	int i;
	if (flags_begin == flags_end && lua_istable(L, flags_begin)) {
		size_t flagCount = lua_rawlen(L, flags_begin);
		nk_love_assert(lua_checkstack(L, flagCount), "%s: failed to allocate stack space");
		for (i = 1; i <= flagCount; ++i) {
			lua_rawgeti(L, flags_begin, i);
		}
		lua_remove(L, flags_begin);
		flags_end = flags_begin + flagCount - 1;
	}
	nk_flags flags = NK_WINDOW_NO_SCROLLBAR;
	for (i = flags_begin; i <= flags_end; ++i) {
		const char *flag = luaL_checkstring(L, i);
		if (!strcmp(flag, "border"))
			flags |= NK_WINDOW_BORDER;
		else if (!strcmp(flag, "movable"))
			flags |= NK_WINDOW_MOVABLE;
		else if (!strcmp(flag, "scalable"))
			flags |= NK_WINDOW_SCALABLE;
		else if (!strcmp(flag, "closable"))
			flags |= NK_WINDOW_CLOSABLE;
		else if (!strcmp(flag, "minimizable"))
			flags |= NK_WINDOW_MINIMIZABLE;
		else if (!strcmp(flag, "scrollbar"))
			flags &= ~NK_WINDOW_NO_SCROLLBAR;
		else if (!strcmp(flag, "title"))
			flags |= NK_WINDOW_TITLE;
		else if (!strcmp(flag, "scroll auto hide"))
			flags |= NK_WINDOW_SCROLL_AUTO_HIDE;
		else if (!strcmp(flag, "background"))
			flags |= NK_WINDOW_BACKGROUND;
		else {
			const char *msg = lua_pushfstring(L, "unrecognized window flag '%s'", flag);
			return luaL_argerror(L, i, msg);
		}
	}
	return flags;
}

static enum nk_symbol_type nk_love_checksymbol(int index)
{
	if (index < 0)
		index += lua_gettop(L) + 1;
	const char *s = luaL_checkstring(L, index);
	if (!strcmp(s, "none")) {
		return NK_SYMBOL_NONE;
	} else if (!strcmp(s, "x")) {
		return NK_SYMBOL_X;
	} else if (!strcmp(s, "underscore")) {
		return NK_SYMBOL_UNDERSCORE;
	} else if (!strcmp(s, "circle solid")) {
		return NK_SYMBOL_CIRCLE_SOLID;
	} else if (!strcmp(s, "circle outline")) {
		return NK_SYMBOL_CIRCLE_OUTLINE;
	} else if (!strcmp(s, "rect solid")) {
		return NK_SYMBOL_RECT_SOLID;
	} else if (!strcmp(s, "rect outline")) {
		return NK_SYMBOL_RECT_OUTLINE;
	} else if (!strcmp(s, "triangle up")) {
		return NK_SYMBOL_TRIANGLE_UP;
	} else if (!strcmp(s, "triangle down")) {
		return NK_SYMBOL_TRIANGLE_DOWN;
	} else if (!strcmp(s, "triangle left")) {
		return NK_SYMBOL_TRIANGLE_LEFT;
	} else if (!strcmp(s, "triangle right")) {
		return NK_SYMBOL_TRIANGLE_RIGHT;
	} else if (!strcmp(s, "plus")) {
		return NK_SYMBOL_PLUS;
	} else if (!strcmp(s, "minus")) {
		return NK_SYMBOL_MINUS;
	} else if (!strcmp(s, "max")) {
		return NK_SYMBOL_MAX;
	} else {
		const char *msg = lua_pushfstring(L, "unrecognized symbol type '%s'", s);
		return luaL_argerror(L, index, msg);
	}
}

static nk_flags nk_love_checkalign(int index)
{
	if (index < 0)
		index += lua_gettop(L) + 1;
	const char *s = luaL_checkstring(L, index);
	if (!strcmp(s, "left")) {
		return NK_TEXT_LEFT;
	} else if (!strcmp(s, "centered")) {
		return NK_TEXT_CENTERED;
	} else if (!strcmp(s, "right")) {
		return NK_TEXT_RIGHT;
	} else if (!strcmp(s, "top left")) {
		return NK_TEXT_ALIGN_TOP | NK_TEXT_ALIGN_LEFT;
	} else if (!strcmp(s, "top centered")) {
		return NK_TEXT_ALIGN_TOP | NK_TEXT_ALIGN_CENTERED;
	} else if (!strcmp(s, "top right")) {
		return NK_TEXT_ALIGN_TOP | NK_TEXT_ALIGN_RIGHT;
	} else if (!strcmp(s, "bottom left")) {
		return NK_TEXT_ALIGN_BOTTOM | NK_TEXT_ALIGN_LEFT;
	} else if (!strcmp(s, "bottom centered")) {
		return NK_TEXT_ALIGN_BOTTOM | NK_TEXT_ALIGN_CENTERED;
	} else if (!strcmp(s, "bottom right")) {
		return NK_TEXT_ALIGN_BOTTOM | NK_TEXT_ALIGN_RIGHT;
	} else {
		const char *msg = lua_pushfstring(L, "unrecognized alignment '%s'", s);
		return luaL_argerror(L, index, msg);
	}
}

static enum nk_buttons nk_love_checkbutton(int index)
{
	if (index < 0)
		index += lua_gettop(L) + 1;
	const char *s = luaL_checkstring(L, index);
	if (!strcmp(s, "left")) {
		return NK_BUTTON_LEFT;
	} else if (!strcmp(s, "right")) {
		return NK_BUTTON_RIGHT;
	} else if (!strcmp(s, "middle")) {
		return NK_BUTTON_MIDDLE;
	} else {
		const char *msg = lua_pushfstring(L, "unrecognized mouse button '%s'", s);
		return luaL_argerror(L, index, msg);
	}
}

static enum nk_keys nk_love_checkkey(int index, int ctrl)
{
	const char *key = luaL_checkstring(L, index);

	if (!strcmp(key, "rshift") || !strcmp(key, "lshift"))
		return NK_KEY_SHIFT;
	else if (!strcmp(key, "delete"))
		return NK_KEY_DEL;
	else if (!strcmp(key, "return"))
		return NK_KEY_ENTER;
	else if (!strcmp(key, "tab"))
		return NK_KEY_TAB;
	else if (!strcmp(key, "backspace"))
		return NK_KEY_BACKSPACE;
	else if (!strcmp(key, "home"))
		return NK_KEY_TEXT_LINE_START;
	else if (!strcmp(key, "end"))
		return NK_KEY_TEXT_LINE_END;
	else if (!strcmp(key, "pagedown"))
		return NK_KEY_SCROLL_DOWN;
	else if (!strcmp(key, "pageup"))
		return NK_KEY_SCROLL_UP;
	else if (!strcmp(key, "insert"))
		return NK_KEY_TEXT_INSERT_MODE;
	else if (!strcmp(key, "z") && ctrl)
		return NK_KEY_TEXT_UNDO;
	else if (!strcmp(key, "r") && ctrl)
		return NK_KEY_TEXT_REDO;
	else if (!strcmp(key, "c") && ctrl)
		return NK_KEY_COPY;
	else if (!strcmp(key, "v") && ctrl)
		return NK_KEY_PASTE;
	else if (!strcmp(key, "x") && ctrl)
		return NK_KEY_CUT;
	else if (!strcmp(key, "b") && ctrl)
		return NK_KEY_TEXT_LINE_START;
	else if (!strcmp(key, "e") && ctrl)
		return NK_KEY_TEXT_LINE_END;
	else if (!strcmp(key, "left")) {
		if (ctrl)
			return NK_KEY_TEXT_WORD_LEFT;
		else
			return NK_KEY_LEFT;

	} else if (!strcmp(key, "right")) {
		if (ctrl)
			return NK_KEY_TEXT_WORD_RIGHT;
		else
			return NK_KEY_RIGHT;
	} else if (!strcmp(key, "up"))
		return NK_KEY_UP;
	else if (!strcmp(key, "down"))
		return NK_KEY_DOWN;
	else
		return luaL_error(L, "invalid key");
}

static enum nk_layout_format nk_love_checkformat(int index)
{
	if (index < 0)
		index += lua_gettop(L) + 1;
	const char *type = luaL_checkstring(L, index);
	if (!strcmp(type, "dynamic")) {
		return NK_DYNAMIC;
	} else if (!strcmp(type, "static")) {
		return NK_STATIC;
	} else {
		const char *msg = lua_pushfstring(L, "unrecognized layout format '%s'", type);
		return luaL_argerror(L, index, msg);
	}
}

static enum nk_tree_type nk_love_checktree(int index)
{
	if (index < 0)
		index += lua_gettop(L) + 1;
	const char *type_string = luaL_checkstring(L, index);
	if (!strcmp(type_string, "node")) {
		return NK_TREE_NODE;
	} else if (!strcmp(type_string, "tab")) {
		return NK_TREE_TAB;
	} else {
		const char *msg = lua_pushfstring(L, "unrecognized tree type '%s'", type_string);
		return luaL_argerror(L, index, msg);
	}
}

static enum nk_collapse_states nk_love_checkstate(int index)
{
	if (index < 0)
		index += lua_gettop(L) + 1;
	const char *state_string = luaL_checkstring(L, index);
	if (!strcmp(state_string, "collapsed")) {
		return NK_MINIMIZED;
	} else if (!strcmp(state_string, "expanded")) {
		return NK_MAXIMIZED;
	} else {
		const char *msg = lua_pushfstring(L, "unrecognized tree state '%s'", state_string);
		return luaL_argerror(L, index, msg);
	}
}

static enum nk_button_behavior nk_love_checkbehavior(int index)
{
	if (index < 0)
		index += lua_gettop(L) + 1;
	const char *behavior_string = luaL_checkstring(L, index);
	if (!strcmp(behavior_string, "default"))
		return NK_BUTTON_DEFAULT;
	else if (!strcmp(behavior_string, "repeater"))
		return NK_BUTTON_REPEATER;
	else {
		const char *msg = lua_pushfstring(L, "unrecognized button behavior '%s'", behavior_string);
		return luaL_argerror(L, index, msg);
	}
}

static enum nk_color_format nk_love_checkcolorformat(int index)
{
	if (index < 0)
		index += lua_gettop(L) + 1;
	const char *format_string = luaL_checkstring(L, index);
	if (!strcmp(format_string, "RGB")) {
		return NK_RGB;
	} else if (!strcmp(format_string, "RGBA")) {
		return NK_RGBA;
	} else {
		const char *msg = lua_pushfstring(L, "unrecognized color format '%s'", format_string);
		return luaL_argerror(L, index, msg);
	}
}

static nk_flags nk_love_checkedittype(int index)
{
	if (index < 0)
		index += lua_gettop(L) + 1;
	const char *type_string = luaL_checkstring(L, index);
	if (!strcmp(type_string, "simple")) {
		return NK_EDIT_SIMPLE;
	} else if (!strcmp(type_string, "field")) {
		return NK_EDIT_FIELD;
	} else if (!strcmp(type_string, "box")) {
		return NK_EDIT_BOX;
	} else {
		const char *msg = lua_pushfstring(L, "unrecognized edit type '%s'", type_string);
		return luaL_argerror(L, index, msg);
 }
}

static enum nk_popup_type nk_love_checkpopup(int index)
{
	if (index < 0)
		index += lua_gettop(L) + 1;
	const char *popup_type = luaL_checkstring(L, index);
	if (!strcmp(popup_type, "dynamic")) {
		return NK_POPUP_DYNAMIC;
	} else if (!strcmp(popup_type, "static")) {
		return NK_POPUP_STATIC;
	} else {
		const char *msg = lua_pushfstring(L, "unrecognized popup type '%s'", popup_type);
		return luaL_argerror(L, index, msg);
	}
}

enum nk_love_draw_mode {NK_LOVE_FILL, NK_LOVE_LINE};

static enum nk_love_draw_mode nk_love_checkdraw(int index)
{
	if (index < 0)
		index += lua_gettop(L) + 1;
	const char *mode = luaL_checkstring(L, index);
	if (!strcmp(mode, "fill")) {
		return NK_LOVE_FILL;
	} else if (!strcmp(mode, "line")) {
		return NK_LOVE_LINE;
	} else {
		const char *msg = lua_pushfstring(L, "unrecognized draw mode '%s'", mode);
		return luaL_argerror(L, index, msg);
	}
}

static int nk_love_checkboolean(lua_State *L, int index)
{
	if (index < 0)
		index += lua_gettop(L) + 1;
	luaL_checktype(L, index, LUA_TBOOLEAN);
	return lua_toboolean(L, index);
}

static void nk_love_transform(float *T, int *x, int *y)
{
	float rx, ry;
	rx = *x * T[0] + *y * T[3] + T[6];
	ry = *x * T[1] + *y * T[4] + T[7];
	*x = (int) rx;
	*y = (int) ry;
}

/*
 * ===============================================================
 *
 *                          GRAPHICS
 *
 * ===============================================================
 */

//this should be ui:configure_graphics(line_sickness, color)
static int nk_love_configureLineWidth(lua_State *L)
{
	int narg = lua_gettop(L);
	if (narg != 2)
		return luaL_error(L, "expect 1 arguments, got %d", narg -1);
	float line_thickness = luaL_checknumber(L, 2);

	lua_getglobal(L, "love");
	lua_pushnumber(L, line_thickness);
	lua_setfield(L, -2, "line_width");
	return 0;
}

static void
nk_love_configureColor(lua_State *L, struct nk_color *rgba)
{
	lua_getglobal(L, "love"); //argc+1
	lua_newtable(L); //argc+2
	lua_pushnumber(L, rgba->r); //argc+3
	lua_setfield(L, -1, "r");
	lua_pushnumber(L, rgba->g); //argc+2
	lua_setfield(L, -1, "g");
	lua_pushnumber(L, rgba->b); //argc+2
	lua_setfield(L, -1, "b");
	lua_pushnumber(L, rgba->a); //argc+2
	lua_setfield(L, -1, "a");
	lua_setfield(L, -2, "color");
}

static int nk_love_color_rgba(lua_State *L)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc == 4 || argc == 5);
	int r = luaL_checkinteger(L, 2);
	int g = luaL_checkinteger(L, 3);
	int b = luaL_checkinteger(L, 4);
	int a = 255;
	if (argc == 5)
		a = luaL_checkinteger(L, 5);
	struct nk_color rgba = nk_rgba(r, g, b ,a);
	nk_love_configureColor(L, &rgba);

	return 0;
}

static int nk_love_color_hsva(lua_State *L)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc == 4 || argc == 5);
	int h = NK_CLAMP(0, luaL_checkinteger(L, 2), 255);
	int s = NK_CLAMP(0, luaL_checkinteger(L, 3), 255);
	int v = NK_CLAMP(0, luaL_checkinteger(L, 4), 255);
	int a = 255;
	if (argc == 5)
		a = NK_CLAMP(0, luaL_checkinteger(L, 5), 255);

	struct nk_color rgba = nk_hsva(h, s, v, a);
	nk_love_configureColor(L, &rgba);
	return 0;
}

static int nk_love_color_parse_rgba(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	struct nk_color rgba = nk_love_checkcolor(1);
	nk_love_configureColor(L, &rgba);
	return 0;
}


static void nk_love_getGraphics(float *line_thickness, struct nk_color *color)
{
	lua_getglobal(L, "love"); //1
	lua_getfield(L, -1, "line_width"); //2
	*line_thickness = lua_tonumber(L, -1);
	lua_pop(L, 1); //1

	lua_getfield(L, -1, "color"); //2
	lua_getfield(L, -2, "r"); //3
	lua_getfield(L, -3, "g"); //4
	lua_getfield(L, -4, "b"); //5
	lua_getfield(L, -5, "a"); //6
	color->r = (int) (lua_tointeger(L, -4));
	color->g = (int) (lua_tointeger(L, -3));
	color->b = (int) (lua_tointeger(L, -2));
	color->a = (int) (lua_tointeger(L, -1));
	lua_pop(L, 6);
}

/*
 * ===============================================================
 *
 *                            INPUT
 *
 * ===============================================================
 */
/*
static void nk_love_clipboard_paste(nk_handle usr, struct nk_text_edit *edit)
{
	(void)usr;
	lua_getglobal(L, "love");
	lua_getfield(L, -1, "system");
	lua_getfield(L, -1, "getClipboardText");
	lua_call(L, 0, 1);
	const char *text = lua_tostring(L, -1);
	if (text) nk_textedit_paste(edit, text, nk_strlen(text));
	lua_pop(L, 3);
}
*/

/*
static void nk_love_clipboard_copy(nk_handle usr, const char *text, int len)
{
	(void)usr;
	char *str = 0;
	if (!len) return;
	str = (char*)malloc((size_t)len+1);
	if (!str) return;
	memcpy(str, text, (size_t)len);
	str[len] = '\0';
	lua_getglobal(L, "love");
	lua_getfield(L, -1, "system");
	lua_getfield(L, -1, "setClipboardText");
	lua_pushstring(L, str);
	free(str);
	lua_call(L, 1, 0);
	lua_pop(L, 2);
}
*/
static int nk_love_is_active(struct nk_context *ctx)
{
	struct nk_window *iter;
	iter = ctx->begin;
	while (iter) {
		/* check if window is being hovered */
		if (iter->flags & NK_WINDOW_MINIMIZED) {
			struct nk_rect header = iter->bounds;
			header.h = ctx->style.font->height + 2 * ctx->style.window.header.padding.y;
			if (nk_input_is_mouse_hovering_rect(&ctx->input, header))
				return 1;
		} else if (nk_input_is_mouse_hovering_rect(&ctx->input, iter->bounds)) {
			return 1;
		}
		/* check if window popup is being hovered */
		if (iter->popup.active && iter->popup.win && nk_input_is_mouse_hovering_rect(&ctx->input, iter->popup.win->bounds))
			return 1;
		if (iter->edit.active & NK_EDIT_ACTIVE)
			return 1;
		iter = iter->next;
	}
	return 0;
}

static int nk_love_keyevent(struct nk_context *ctx, enum nk_keys key,
			    int down)
{
	nk_input_key(ctx, key, down);
	return nk_love_is_active(ctx);
}

static int nk_love_clickevent(struct nk_love_context *ctx, int x, int y,
	enum nk_buttons button, int down)
{
	nk_love_transform(ctx->Ti, &x, &y);
	struct nk_context *nkctx = ctx->nkctx;
	if (button == NK_BUTTON_MAX)
		return 0;
	nk_input_button(nkctx, button, x, y, down);
	return nk_window_is_any_hovered(nkctx);
}

static int nk_love_mousemoved_event(struct nk_love_context *ctx, int x, int y)
{
	nk_love_transform(ctx->Ti, &x, &y);
	struct nk_context *nkctx = ctx->nkctx;
	nk_input_motion(nkctx, x, y);
	return nk_window_is_any_hovered(nkctx);
}

static int nk_love_textinput_event(struct nk_context *ctx, const char *text)
{
	nk_rune rune;
	nk_utf_decode(text, &rune, strlen(text));
	nk_input_unicode(ctx, rune);
	return nk_love_is_active(ctx);
}

static int nk_love_wheelmoved_event(struct nk_context *ctx, int x, int y)
{
	struct nk_vec2 scroll;
	scroll.x = x;
	scroll.y = y;
	nk_input_scroll(ctx, scroll);
	return nk_window_is_any_hovered(ctx);
}

/*
 * ===============================================================
 *
 *                          WRAPPER
 *
 * ===============================================================
 */

//I shaw be creating one of it here
struct nk_love_context *nk_love_get_ui(struct lua_State *L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "nuklear"); //1
	lua_getfield(L, -1, "newui");
	struct nk_love_context *context = luaL_checkudata(L, -1, "metatable");
	lua_pop(L, 2);
	return context;
}

void nk_love_getfield_ui(struct lua_State *L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "nuklear"); //1
	lua_getfield(L, -1, "newui");
	struct nk_love_context *context = luaL_checkudata(L, -1, "metatable");
	lua_remove(L, -2);
	(void)context;
}

struct nk_love_context *nk_love_new_ui(struct lua_State *L, struct nk_context *ctx)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "nuklear"); //1
	struct nk_love_context *context = //2
		lua_newuserdata(L, sizeof(struct nk_love_context));
	luaL_getmetatable(L, "metatable"); //3
	lua_setmetatable(L, -2); //2
	lua_setfield(L, -2, "newui"); //1

	lua_newtable(L); //2
	lua_newtable(L); //3
	lua_setfield(L, -2, "font"); //2
	lua_newtable(L); //3
	lua_setfield(L, -2, "image"); ///2
	lua_newtable(L); //3
	lua_setfield(L, -2, "stack"); //2
	lua_rawsetp(L, -2, context); //1
	lua_pop(L, 1);

	context->nkctx = ctx;
	context->font_count = 1;
	context->layout_ratio_count = 0;
	//context->nkctx->clip.copy = nk_love_clipboard_copy;
	//context->nkctx->clip.paste = nk_love_clipboard_paste;
	//context->nkctx->clip.userdata = nk_handle_ptr(0);
	return context;
}


void nk_love_destroy(lua_State *L, struct nk_love_context *context)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "nuklear"); //1
	lua_pushnil(L); //2
	lua_setfield(L, -2, "newui"); //1
	lua_pushnil(L); //2
	lua_rawsetp(L, -2, context); //1
	lua_pop(L, 1);
}

static int nk_love_keypressed(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 3);
	struct nk_context *ctx = nk_love_checkcontext(1)->nkctx;
	int ctrl = nk_love_checkboolean(L, 3);
	enum nk_keys key = nk_love_checkkey(2, ctrl);
	int consume = nk_love_keyevent(ctx, key, 1);
	lua_pushboolean(L, consume);
	return 1;
}

static int nk_love_keyreleased(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 3);
	struct nk_context *ctx = nk_love_checkcontext(1)->nkctx;
	int ctrl = nk_love_checkboolean(L, 3);
	enum nk_keys key = nk_love_checkkey(2, ctrl);
	int consume = nk_love_keyevent(ctx, key, 0);
	lua_pushboolean(L, consume);
	return 1;
}

static int nk_love_mousepressed(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 4);
	struct nk_love_context *ctx = nk_love_checkcontext(1);
	int x = luaL_checkinteger(L, 2);
	int y = luaL_checkinteger(L, 3);
	int button = nk_love_checkbutton(4);
	int consume = nk_love_clickevent(ctx, x, y, button, 1);
	lua_pushboolean(L, consume);
	return 1;
}

static int nk_love_mousereleased(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 4);
	struct nk_love_context *ctx = nk_love_checkcontext(1);
	int x = luaL_checkinteger(L, 2);
	int y = luaL_checkinteger(L, 3);
	int button = nk_love_checkbutton(4);
	int consume = nk_love_clickevent(ctx, x, y, button, 0);
	lua_pushboolean(L, consume);
	return 1;
}

static int nk_love_mousemoved(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 3);
	struct nk_love_context *ctx = nk_love_checkcontext(1);
	int x = luaL_checkinteger(L, 2);
	int y = luaL_checkinteger(L, 3);
	int consume = nk_love_mousemoved_event(ctx, x, y);
	lua_pushboolean(L, consume);
	return 1;
}

static int nk_love_textinput(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	struct nk_context *ctx = nk_love_checkcontext(1)->nkctx;
	const char *text = luaL_checkstring(L, 2);
	int consume = nk_love_textinput_event(ctx, text);
	lua_pushboolean(L, consume);
	return 1;
}

static int nk_love_wheelmoved(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 3);
	struct nk_context *ctx = nk_love_checkcontext(1)->nkctx;
	int x = luaL_checkinteger(L, 2);
	int y = luaL_checkinteger(L, 3);
	int consume = nk_love_wheelmoved_event(ctx, x, y);
	lua_pushboolean(L, consume);
	return 1;
}

static void nk_love_preserve(struct nk_style_item *item)
{
	if (item->type == NK_STYLE_ITEM_IMAGE) {
		lua_rawgeti(L, -1, item->data.image.handle.id);
		nk_love_checkImage(-1, &item->data.image);
		lua_pop(L, 1);
	}
}

static void nk_love_preserve_all(void)
{
	nk_love_preserve(&CONTEXT->nkctx->style.button.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.button.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.button.active);

	nk_love_preserve(&CONTEXT->nkctx->style.contextual_button.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.contextual_button.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.contextual_button.active);

	nk_love_preserve(&CONTEXT->nkctx->style.menu_button.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.menu_button.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.menu_button.active);

	nk_love_preserve(&CONTEXT->nkctx->style.option.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.option.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.option.active);
	nk_love_preserve(&CONTEXT->nkctx->style.option.cursor_normal);
	nk_love_preserve(&CONTEXT->nkctx->style.option.cursor_hover);

	nk_love_preserve(&CONTEXT->nkctx->style.checkbox.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.checkbox.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.checkbox.active);
	nk_love_preserve(&CONTEXT->nkctx->style.checkbox.cursor_normal);
	nk_love_preserve(&CONTEXT->nkctx->style.checkbox.cursor_hover);

	nk_love_preserve(&CONTEXT->nkctx->style.selectable.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.selectable.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.selectable.pressed);
	nk_love_preserve(&CONTEXT->nkctx->style.selectable.normal_active);
	nk_love_preserve(&CONTEXT->nkctx->style.selectable.hover_active);
	nk_love_preserve(&CONTEXT->nkctx->style.selectable.pressed_active);

	nk_love_preserve(&CONTEXT->nkctx->style.slider.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.slider.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.slider.active);
	nk_love_preserve(&CONTEXT->nkctx->style.slider.cursor_normal);
	nk_love_preserve(&CONTEXT->nkctx->style.slider.cursor_hover);
	nk_love_preserve(&CONTEXT->nkctx->style.slider.cursor_active);

	nk_love_preserve(&CONTEXT->nkctx->style.progress.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.progress.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.progress.active);
	nk_love_preserve(&CONTEXT->nkctx->style.progress.cursor_normal);
	nk_love_preserve(&CONTEXT->nkctx->style.progress.cursor_hover);
	nk_love_preserve(&CONTEXT->nkctx->style.progress.cursor_active);

	nk_love_preserve(&CONTEXT->nkctx->style.property.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.property.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.property.active);
	nk_love_preserve(&CONTEXT->nkctx->style.property.edit.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.property.edit.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.property.edit.active);
	nk_love_preserve(&CONTEXT->nkctx->style.property.inc_button.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.property.inc_button.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.property.inc_button.active);
	nk_love_preserve(&CONTEXT->nkctx->style.property.dec_button.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.property.dec_button.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.property.dec_button.active);

	nk_love_preserve(&CONTEXT->nkctx->style.edit.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.edit.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.edit.active);
	nk_love_preserve(&CONTEXT->nkctx->style.edit.scrollbar.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.edit.scrollbar.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.edit.scrollbar.active);
	nk_love_preserve(&CONTEXT->nkctx->style.edit.scrollbar.cursor_normal);
	nk_love_preserve(&CONTEXT->nkctx->style.edit.scrollbar.cursor_hover);
	nk_love_preserve(&CONTEXT->nkctx->style.edit.scrollbar.cursor_active);

	nk_love_preserve(&CONTEXT->nkctx->style.chart.background);

	nk_love_preserve(&CONTEXT->nkctx->style.scrollh.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.scrollh.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.scrollh.active);
	nk_love_preserve(&CONTEXT->nkctx->style.scrollh.cursor_normal);
	nk_love_preserve(&CONTEXT->nkctx->style.scrollh.cursor_hover);
	nk_love_preserve(&CONTEXT->nkctx->style.scrollh.cursor_active);

	nk_love_preserve(&CONTEXT->nkctx->style.scrollv.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.scrollv.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.scrollv.active);
	nk_love_preserve(&CONTEXT->nkctx->style.scrollv.cursor_normal);
	nk_love_preserve(&CONTEXT->nkctx->style.scrollv.cursor_hover);
	nk_love_preserve(&CONTEXT->nkctx->style.scrollv.cursor_active);

	nk_love_preserve(&CONTEXT->nkctx->style.tab.background);
	nk_love_preserve(&CONTEXT->nkctx->style.tab.tab_maximize_button.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.tab.tab_maximize_button.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.tab.tab_maximize_button.active);
	nk_love_preserve(&CONTEXT->nkctx->style.tab.tab_minimize_button.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.tab.tab_minimize_button.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.tab.tab_minimize_button.active);
	nk_love_preserve(&CONTEXT->nkctx->style.tab.node_maximize_button.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.tab.node_maximize_button.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.tab.node_maximize_button.active);
	nk_love_preserve(&CONTEXT->nkctx->style.tab.node_minimize_button.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.tab.node_minimize_button.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.tab.node_minimize_button.active);

	nk_love_preserve(&CONTEXT->nkctx->style.combo.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.combo.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.combo.active);
	nk_love_preserve(&CONTEXT->nkctx->style.combo.button.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.combo.button.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.combo.button.active);

	nk_love_preserve(&CONTEXT->nkctx->style.window.fixed_background);
	nk_love_preserve(&CONTEXT->nkctx->style.window.scaler);
	nk_love_preserve(&CONTEXT->nkctx->style.window.header.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.window.header.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.window.header.active);
	nk_love_preserve(&CONTEXT->nkctx->style.window.header.close_button.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.window.header.close_button.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.window.header.close_button.active);
	nk_love_preserve(&CONTEXT->nkctx->style.window.header.minimize_button.normal);
	nk_love_preserve(&CONTEXT->nkctx->style.window.header.minimize_button.hover);
	nk_love_preserve(&CONTEXT->nkctx->style.window.header.minimize_button.active);
}

//TODO check about image and font
static int nk_love_frame_begin(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	nk_love_assert(CONTEXT == NULL, "missing ui::frameEnd from last frame");
	CONTEXT = nk_love_checkcontext(1);
	nk_input_end(CONTEXT->nkctx);
	lua_getfield(L, LUA_REGISTRYINDEX, "nuklear");
	lua_pushlightuserdata(L, CONTEXT);
	lua_gettable(L, -2);
	lua_getfield(L, -1, "image");
	lua_newtable(L);
	lua_setfield(L, -3, "image");
	nk_love_preserve_all();
	lua_pop(L, 1);
	lua_getfield(L, -1, "font");
	lua_newtable(L);
	lua_setfield(L, -3, "font");
	CONTEXT->font_count = 0;
	lua_rawgeti(L, -1, CONTEXT->nkctx->style.font->userdata.id);
	nk_love_checkFont(-1, &fonts[CONTEXT->font_count]);
	lua_pop(L, 1);
	CONTEXT->nkctx->style.font = &fonts[CONTEXT->font_count++];
	int i;
	for (i = 0; i < CONTEXT->nkctx->stacks.fonts.head; ++i) {
		struct nk_config_stack_user_font_element *element = &CONTEXT->nkctx->stacks.fonts.elements[i];
		lua_rawgeti(L, -1, element->old_value->userdata.id);
		nk_love_checkFont(-1, &fonts[CONTEXT->font_count]);
		lua_pop(L, 1);
		CONTEXT->nkctx->stacks.fonts.elements[i].old_value = &fonts[CONTEXT->font_count++];
	}
	lua_pop(L, 1);
	CONTEXT->layout_ratio_count = 0;
	for (i = 0; i < 9; ++i)
		CONTEXT->T[i] = CONTEXT->Ti[i] = (i % 3 == i / 3);
	CONTEXT->transform_allowed = 1;
	lua_newtable(L);
	lua_setfield(L, -2, "transform");
	return 0;
}

static int nk_love_frame_end(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	nk_love_assert_context(1);
	nk_input_begin(CONTEXT->nkctx);
	CONTEXT = NULL;
	return 0;
}

static int nk_love_frame(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	if (!lua_isfunction(L, -1))
		luaL_typerror(L, lua_gettop(L), "function");
	lua_getfield(L, 1, "frameBegin");
	lua_pushvalue(L, 1);
	lua_call(L, 1, 0);
	lua_pushvalue(L, 1);
	lua_call(L, 1, 0);
	lua_getfield(L, 1, "frameEnd");
	lua_insert(L, 1);
	lua_call(L, 1, 0);
	return 0;
}

/*
 * ===============================================================
 *
 *                          TRANSFORM
 *
 * ===============================================================
 */

/*
cos -sin  0 |  cos  sin  0
sin  cos  0 | -sin  cos  0
0    0    1 |  0    0    1
*/
static int nk_love_rotate(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	struct nk_love_context *context = nk_love_checkcontext(1);
	float angle = luaL_checknumber(L, 2);
	nk_love_pushregistry("transform");
	size_t len = lua_rawlen(L, -1);
	lua_newtable(L);
	lua_pushstring(L, "rotate");
	lua_rawseti(L, -2, 1);
	lua_pushnumber(L, angle);
	lua_rawseti(L, -2, 2);
	lua_rawseti(L, -2, len + 1);
	float *T = context->T, *Ti = context->Ti;
	float c = cosf(angle);
	float s = sinf(angle);
	int i;
	float R[9];
	R[0] = T[0] * c + T[3] * s;
	R[1] = T[1] * c + T[4] * s;
	R[2] = T[2] * c + T[5] * s;
	R[3] = T[0] * -s + T[3] * c;
	R[4] = T[1] * -s + T[4] * c;
	R[5] = T[2] * -s + T[5] * c;
	R[6] = T[6];
	R[7] = T[7];
	R[8] = T[8];
	for (i = 0; i < 9; ++i)
		T[i] = R[i];
	R[0] = c * Ti[0] + s * Ti[1];
	R[1] = -s * Ti[0] + c * Ti[1];
	R[2] = Ti[2];
	R[3] = c * Ti[3] + s * Ti[4];
	R[4] = -s * Ti[3] + c * Ti[4];
	R[5] = Ti[5];
	R[6] = c * Ti[6] + s * Ti[7];
	R[7] = -s * Ti[6] + c * Ti[7];
	R[8] = Ti[8];
	for (i = 0; i < 9; ++i)
		Ti[i] = R[i];
	return 0;
}

/*
sx 0  0 | 1/sx 0    0
0  sy 0 | 0    1/sy 0
0  0  1 | 0    0    1
*/
static int nk_love_scale(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) >= 2 && lua_gettop(L) <= 3);
	struct nk_love_context *context = nk_love_checkcontext(1);
	float sx = luaL_checknumber(L, 2);
	float sy = luaL_optnumber(L, 3, sx);
	nk_love_pushregistry("transform");
	size_t len = lua_rawlen(L, -1);
	lua_newtable(L);
	lua_pushstring(L, "scale");
	lua_rawseti(L, -2, 1);
	lua_pushnumber(L, sx);
	lua_rawseti(L, -2, 2);
	lua_pushnumber(L, sy);
	lua_rawseti(L, -2, 3);
	lua_rawseti(L, -2, len + 1);
	float *T = context->T, *Ti = context->Ti;
	T[0] *= sx;
	T[1] *= sx;
	T[2] *= sx;
	T[3] *= sy;
	T[4] *= sy;
	T[5] *= sy;
	Ti[0] /= sx;
	Ti[1] /= sy;
	Ti[3] /= sx;
	Ti[4] /= sy;
	Ti[6] /= sx;
	Ti[7] /= sy;
	return 0;
}

/*
1  kx 0 | 1/(1-kx*ky)  kx/(kx*ky-1) 0
ky 1  0 | ky/(kx*ky-1) 1/(1-kx*ky)  0
0  0  1 | 0            0            1
*/
static int nk_love_shear(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 3);
	struct nk_love_context *context = nk_love_checkcontext(1);
	float kx = luaL_checknumber(L, 2);
	float ky = luaL_checknumber(L, 3);
	nk_love_pushregistry("transform");
	size_t len = lua_rawlen(L, -1);
	lua_newtable(L);
	lua_pushstring(L, "shear");
	lua_rawseti(L, -2, 1);
	lua_pushnumber(L, kx);
	lua_rawseti(L, -2, 2);
	lua_pushnumber(L, ky);
	lua_rawseti(L, -2, 3);
	lua_rawseti(L, -2, len + 1);
	float *T = context->T, *Ti = context->Ti;
	float R[9];
	R[0] = T[0] + T[3] * ky;
	R[1] = T[1] + T[4] * ky;
	R[2] = T[2] + T[5] * ky;
	R[3] = T[0] * kx + T[3];
	R[4] = T[1] * kx + T[4];
	R[5] = T[2] * kx + T[5];
	R[6] = T[6];
	R[7] = T[7];
	R[8] = T[8];
	int i;
	for (i = 0; i < 9; ++i)
		T[i] = R[i];
	float a = 1.0f / (1 - kx * ky);
	float b = 1.0f / (kx * ky - 1);
	R[0] = a * Ti[0] + kx * b * Ti[1];
	R[1] = ky * b * Ti[0] + a * Ti[1];
	R[2] = Ti[2];
	R[3] = a * Ti[3] + kx * b * Ti[4];
	R[4] = ky * b * Ti[3] + a * Ti[4];
	R[5] = Ti[5];
	R[6] = a * Ti[6] + kx * b * Ti[7];
	R[7] = ky * b * Ti[6] + a * Ti[7];
	R[8] = Ti[8];
	for (i = 0; i < 9; ++i)
		Ti[i] = R[i];
	return 0;
}

/*
1 0 dx | 1 0 -dx
0 1 dy | 0 1 -dy
0 0 1  | 0 0  1
*/
static int nk_love_translate(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 3);
	struct nk_love_context *context = nk_love_checkcontext(1);
	float dx = luaL_checknumber(L, 2);
	float dy = luaL_checknumber(L, 3);
	nk_love_pushregistry("transform");
	size_t len = lua_rawlen(L, -1);
	lua_newtable(L);
	lua_pushstring(L, "translate");
	lua_rawseti(L, -2, 1);
	lua_pushnumber(L, dx);
	lua_rawseti(L, -2, 2);
	lua_pushnumber(L, dy);
	lua_rawseti(L, -2, 3);
	lua_rawseti(L, -2, len + 1);
	float *T = context->T, *Ti = context->Ti;
	float R[9];
	T[6] += T[0] * dx + T[3] * dy;
	T[7] += T[1] * dx + T[4] * dy;
	T[8] += T[2] * dx + T[5] * dy;
	R[0] = Ti[0] - dx * Ti[2];
	R[1] = Ti[1] - dy * Ti[2];
	R[2] = Ti[2];
	R[3] = Ti[3] - dx * Ti[5];
	R[4] = Ti[4] - dy * Ti[5];
	R[5] = Ti[5];
	R[6] = Ti[6] - dx * Ti[8];
	R[7] = Ti[7] - dy * Ti[8];
	R[8] = Ti[8];
	int i;
	for (i = 0; i < 9; ++i)
		Ti[i] = R[i];
	return 0;
}

/*
 * ===============================================================
 *
 *                          WINDOW
 *
 * ===============================================================
 */

static int nk_love_window_begin(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) >= 1);
	const char *name, *title;
	int bounds_begin;
	if (lua_isnumber(L, 3)) {
		nk_love_assert_argc(lua_gettop(L) >= 6);
		name = title = luaL_checkstring(L, 2);
		bounds_begin = 3;
	} else {
		nk_love_assert_argc(lua_gettop(L) >= 7);
		name = luaL_checkstring(L, 2);
		title = luaL_checkstring(L, 3);
		bounds_begin = 4;
	}
	nk_flags flags = nk_love_parse_window_flags(bounds_begin + 4, lua_gettop(L));
	float x = luaL_checknumber(L, bounds_begin);
	float y = luaL_checknumber(L, bounds_begin + 1);
	float width = luaL_checknumber(L, bounds_begin + 2);
	float height = luaL_checknumber(L, bounds_begin + 3);
	int open = nk_begin_titled(nk_love_checkcontext(1)->nkctx, name, title, nk_rect(x, y, width, height), flags);
	lua_pushboolean(L, open);
	return 1;
}

static int nk_love_window_end(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	nk_love_assert_context(1);
	nk_end(nk_love_checkcontext(1)->nkctx);
	return 0;
}

static int nk_love_window(lua_State *L)
{
	nk_love_assert(lua_checkstack(L, 2), "%s: failed to allocate stack space");
	if (!lua_isfunction(L, -1))
		luaL_typerror(L, lua_gettop(L), "function");
	lua_insert(L, 2);
	lua_pushvalue(L, 1);
	lua_insert(L, 3);
	lua_getfield(L, 1, "windowBegin");
	lua_insert(L, 3);
	lua_call(L, lua_gettop(L) - 3, 1);
	int open = lua_toboolean(L, -1);
	lua_pop(L, 1);
	if (open) {
		lua_pushvalue(L, 1);
		lua_call(L, 1, 0);
	} else {
		lua_pop(L, 1);
	}
	lua_getfield(L, -1, "windowEnd");
	lua_insert(L, 1);
	lua_call(L, 1, 0);
	return 0;
}

static int nk_love_window_get_bounds(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_rect rect = nk_window_get_bounds(nk_love_checkcontext(1)->nkctx);
	lua_pushnumber(L, rect.x);
	lua_pushnumber(L, rect.y);
	lua_pushnumber(L, rect.w);
	lua_pushnumber(L, rect.h);
	return 4;
}

static int nk_love_window_get_position(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	nk_love_assert_context(1);
	struct nk_vec2 pos = nk_window_get_position(nk_love_checkcontext(1)->nkctx);
	lua_pushnumber(L, pos.x);
	lua_pushnumber(L, pos.y);
	return 2;
}

static int nk_love_window_get_size(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	nk_love_assert_context(1);
	struct nk_vec2 size = nk_window_get_size(nk_love_checkcontext(1)->nkctx);
	lua_pushnumber(L, size.x);
	lua_pushnumber(L, size.y);
	return 2;
}

static int nk_love_window_get_scroll(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_uint offset_x, offset_y;
	nk_window_get_scroll(context->nkctx, &offset_x, &offset_y);
	lua_pushinteger(L, offset_x);
	lua_pushinteger(L, offset_y);
	return 2;
}

static int nk_love_window_get_content_region(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	nk_love_assert_context(1);
	struct nk_rect rect = nk_window_get_content_region(nk_love_checkcontext(1)->nkctx);
	lua_pushnumber(L, rect.x);
	lua_pushnumber(L, rect.y);
	lua_pushnumber(L, rect.w);
	lua_pushnumber(L, rect.h);
	return 4;
}

static int nk_love_window_has_focus(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	nk_love_assert_context(1);
	int has_focus = nk_window_has_focus(nk_love_checkcontext(1)->nkctx);
	lua_pushboolean(L, has_focus);
	return 1;
}

static int nk_love_window_is_collapsed(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	nk_love_assert_context(1);
	const char *name = luaL_checkstring(L, 2);
	int is_collapsed = nk_window_is_collapsed(nk_love_checkcontext(1)->nkctx, name);
	lua_pushboolean(L, is_collapsed);
	return 1;
}

static int nk_love_window_is_closed(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	nk_love_assert_context(1);
	const char *name = luaL_checkstring(L, 2);
	int is_closed = nk_window_is_closed(nk_love_checkcontext(1)->nkctx, name);
	lua_pushboolean(L, is_closed);
	return 1;
}

static int nk_love_window_is_hidden(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	nk_love_assert_context(1);
	const char *name = luaL_checkstring(L, 2);
	int is_hidden = nk_window_is_hidden(nk_love_checkcontext(1)->nkctx, name);
	lua_pushboolean(L, is_hidden);
	return 1;
}

static int nk_love_window_is_active(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	nk_love_assert_context(1);
	const char *name = luaL_checkstring(L, 2);
	int is_active = nk_window_is_active(nk_love_checkcontext(1)->nkctx, name);
	lua_pushboolean(L, is_active);
	return 1;
}

static int nk_love_window_is_hovered(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	nk_love_assert_context(1);
	int is_hovered = nk_window_is_hovered(nk_love_checkcontext(1)->nkctx);
	lua_pushboolean(L, is_hovered);
	return 1;
}

static int nk_love_window_is_any_hovered(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	nk_love_assert_context(1);
	int is_any_hovered = nk_window_is_any_hovered(nk_love_checkcontext(1)->nkctx);
	lua_pushboolean(L, is_any_hovered);
	return 1;
}

static int nk_love_item_is_any_active(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	nk_love_assert_context(1);
	lua_pushboolean(L, nk_love_is_active(nk_love_checkcontext(1)->nkctx));
	return 1;
}

static int nk_love_window_set_bounds(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 6);
	nk_love_assert_context(1);
	const char *name = luaL_checkstring(L, 2);
	struct nk_rect bounds;
	bounds.x = luaL_checknumber(L, 3);
	bounds.y = luaL_checknumber(L, 4);
	bounds.w = luaL_checknumber(L, 5);
	bounds.h = luaL_checknumber(L, 6);
	nk_window_set_bounds(nk_love_checkcontext(1)->nkctx, name, bounds);
	return 0;
}

static int nk_love_window_set_position(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 4);
	nk_love_assert_context(1);
	const char *name = luaL_checkstring(L, 2);
	struct nk_vec2 pos;
	pos.x = luaL_checknumber(L, 3);
	pos.y = luaL_checknumber(L, 4);
	nk_window_set_position(nk_love_checkcontext(1)->nkctx, name, pos);
	return 0;
}

static int nk_love_window_set_size(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 4);
	nk_love_assert_context(1);
	const char *name = luaL_checkstring(L, 2);
	struct nk_vec2 size;
	size.x = luaL_checknumber(L, 3);
	size.y = luaL_checknumber(L, 4);
	nk_window_set_size(nk_love_checkcontext(1)->nkctx, name, size);
	return 0;
}

static int nk_love_window_set_focus(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	nk_love_assert_context(1);
	const char *name = luaL_checkstring(L, 2);
	nk_window_set_focus(nk_love_checkcontext(1)->nkctx, name);
	return 0;
}

static int nk_love_window_set_scroll(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 3);
	struct nk_love_context *context =
		nk_love_checkcontext(1);
	nk_uint offset_x, offset_y;
	offset_x = luaL_checkinteger(L, 2);
	offset_y = luaL_checkinteger(L, 3);
	nk_window_set_scroll(context->nkctx, offset_x, offset_y);
	return 0;
}

static int nk_love_window_close(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	nk_love_assert_context(1);
	const char *name = luaL_checkstring(L, 2);
	nk_window_close(nk_love_checkcontext(1)->nkctx, name);
	return 0;
}

static int nk_love_window_collapse(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	nk_love_assert_context(1);
	const char *name = luaL_checkstring(L, 2);
	nk_window_collapse(nk_love_checkcontext(1)->nkctx, name, NK_MINIMIZED);
	return 0;
}

static int nk_love_window_expand(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	nk_love_assert_context(1);
	const char *name = luaL_checkstring(L, 2);
	nk_window_collapse(nk_love_checkcontext(1)->nkctx, name, NK_MAXIMIZED);
	return 0;
}

static int nk_love_window_show(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	nk_love_assert_context(1);
	const char *name = luaL_checkstring(L, 2);
	nk_window_show(nk_love_checkcontext(1)->nkctx, name, NK_SHOWN);
	return 0;
}

static int nk_love_window_hide(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	nk_love_assert_context(1);
	const char *name = luaL_checkstring(L, 2);
	nk_window_show(nk_love_checkcontext(1)->nkctx, name, NK_HIDDEN);
	return 0;
}

/*
 * ===============================================================
 *
 *                           LAYOUT
 *
 * ===============================================================
 */

static int nk_love_layout_row(lua_State *L)
{
	int argc = lua_gettop(L);
	if (argc == 5 && lua_isfunction(L, 5)) {
		nk_love_assert(lua_checkstack(L, 3), "%s: failed to allocate stack space");
		lua_pushvalue(L, 1);
		lua_insert(L, 2);
		lua_pushvalue(L, 1);
		lua_insert(L, 3);
		lua_insert(L, 2);
		lua_getfield(L, 1, "layoutRowBegin");
		lua_insert(L, 4);
		lua_call(L, 4, 0);
		lua_call(L, 1, 0);
		lua_getfield(L, 1, "layoutRowEnd");
		lua_insert(L, 1);
		lua_call(L, 1, 0);
	} else {
		nk_love_assert_argc(argc >= 4 && argc <= 5);
		nk_love_assert_context(1);
		enum nk_layout_format format = nk_love_checkformat(2);
		float height = luaL_checknumber(L, 3);
		int use_ratios = 0;
		if (format == NK_DYNAMIC) {
			nk_love_assert_argc(argc == 4);
			if (lua_isnumber(L, 4)) {
				int cols = luaL_checkinteger(L, 4);
				nk_layout_row_dynamic(nk_love_checkcontext(1)->nkctx, height, cols);
			} else {
				if (!lua_istable(L, 4))
					luaL_argerror(L, 4, "should be a number or table");
				use_ratios = 1;
			}
		} else if (format == NK_STATIC) {
			if (argc == 5) {
				int item_width = luaL_checkinteger(L, 4);
				int cols = luaL_checkinteger(L, 5);
				nk_layout_row_static(nk_love_checkcontext(1)->nkctx, height, item_width, cols);
			} else {
				if (!lua_istable(L, 4))
					luaL_argerror(L, 4, "should be a number or table");
				use_ratios = 1;
			}
		}
		if (use_ratios) {
			int cols = lua_rawlen(L, -1);
			int i, j;
			for (i = 1, j = nk_love_checkcontext(1)->layout_ratio_count; i <= cols && j < NK_LOVE_MAX_RATIOS; ++i, ++j) {
				lua_rawgeti(L, -1, i);
				if (!lua_isnumber(L, -1))
					luaL_argerror(L, lua_gettop(L) - 1, "should contain numbers only");
				layout_ratios[j] = lua_tonumber(L, -1);
				lua_pop(L, 1);
			}
			nk_layout_row(nk_love_checkcontext(1)->nkctx, format, height, cols, layout_ratios + nk_love_checkcontext(1)->layout_ratio_count);
			nk_love_checkcontext(1)->layout_ratio_count += cols;
		}
	}
	return 0;
}

static int nk_love_layout_row_begin(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 4);
	nk_love_assert_context(1);
	enum nk_layout_format format = nk_love_checkformat(2);
	float height = luaL_checknumber(L, 3);
	int cols = luaL_checkinteger(L, 4);
	nk_layout_row_begin(nk_love_checkcontext(1)->nkctx, format, height, cols);
	return 0;
}

static int nk_love_layout_row_push(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	nk_love_assert_context(1);
	float value = luaL_checknumber(L, 2);
	nk_layout_row_push(nk_love_checkcontext(1)->nkctx, value);
	return 0;
}

static int nk_love_layout_row_end(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	nk_love_assert_context(1);
	nk_layout_row_end(nk_love_checkcontext(1)->nkctx);
	return 0;
}

static int nk_love_layout_template_begin(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	nk_love_assert_context(1);
	float height = luaL_checknumber(L, 2);
	nk_layout_row_template_begin(nk_love_checkcontext(1)->nkctx, height);
	return 0;
}

static int nk_love_layout_template_push(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2 || lua_gettop(L) == 3);
	nk_love_assert_context(1);
	const char *mode = luaL_checkstring(L, 2);
	if (!strcmp(mode, "dynamic")) {
		nk_love_assert_argc(lua_gettop(L) == 2);
		nk_layout_row_template_push_dynamic(nk_love_checkcontext(1)->nkctx);
	} else {
		nk_love_assert_argc(lua_gettop(L) == 3);
		float width = luaL_checknumber(L, 3);
		if (!strcmp(mode, "variable")) {
			nk_layout_row_template_push_variable(nk_love_checkcontext(1)->nkctx, width);
		} else if (!strcmp(mode, "static")) {
			nk_layout_row_template_push_static(nk_love_checkcontext(1)->nkctx, width);
		} else {
			return luaL_argerror(L, 2, "expecting 'dynamic', 'variable', or 'static' modes");
		}
	}
	return 0;
}

static int nk_love_layout_template_end(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	nk_love_assert_context(1);
	nk_layout_row_template_end(nk_love_checkcontext(1)->nkctx);
	return 0;
}

static int nk_love_layout_template(lua_State *L)
{
	nk_love_assert(lua_checkstack(L, 3), "%s: failed to allocate stack space");
	nk_love_assert_argc(lua_gettop(L) == 3);
	if (!lua_isfunction(L, -1))
		luaL_typerror(L, lua_gettop(L), "function");
	lua_pushvalue(L, 1);
	lua_insert(L, 2);
	lua_pushvalue(L, 1);
	lua_insert(L, 3);
	lua_insert(L, 2);
	lua_getfield(L, 1, "layoutTemplateBegin");
	lua_insert(L, 4);
	lua_call(L, 2, 0);
	lua_call(L, 1, 0);
	lua_getfield(L, 1, "layoutTemplateEnd");
	lua_insert(L, 1);
	lua_call(L, 1, 0);
	return 0;
}

static int nk_love_layout_space_begin(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 4);
	nk_love_assert_context(1);
	enum nk_layout_format format = nk_love_checkformat(2);
	float height = luaL_checknumber(L, 3);
	int widget_count = luaL_checkinteger(L, 4);
	nk_layout_space_begin(nk_love_checkcontext(1)->nkctx, format, height, widget_count);
	return 0;
}

static int nk_love_layout_space_push(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 5);
	nk_love_assert_context(1);
	float x = luaL_checknumber(L, 2);
	float y = luaL_checknumber(L, 3);
	float width = luaL_checknumber(L, 4);
	float height = luaL_checknumber(L, 5);
	nk_layout_space_push(nk_love_checkcontext(1)->nkctx, nk_rect(x, y, width, height));
	return 0;
}

static int nk_love_layout_space_end(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	nk_love_assert_context(1);
	nk_layout_space_end(nk_love_checkcontext(1)->nkctx);
	return 0;
}

static int nk_love_layout_space(lua_State *L)
{
	nk_love_assert(lua_checkstack(L, 3), "%s: failed to allocate stack space");
	nk_love_assert_argc(lua_gettop(L) == 5);
	if (!lua_isfunction(L, -1))
		luaL_typerror(L, lua_gettop(L), "function");
	lua_pushvalue(L, 1);
	lua_insert(L, 2);
	lua_pushvalue(L, 1);
	lua_insert(L, 3);
	lua_insert(L, 2);
	lua_getfield(L, 1, "layoutSpaceBegin");
	lua_insert(L, 4);
	lua_call(L, 4, 0);
	lua_call(L, 1, 0);
	lua_getfield(L, 1, "layoutSpaceEnd");
	lua_insert(L, 1);
	lua_call(L, 1, 0);
	return 0;
}

static int nk_love_layout_space_bounds(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	nk_love_assert_context(1);
	struct nk_rect bounds = nk_layout_space_bounds(nk_love_checkcontext(1)->nkctx);
	lua_pushnumber(L, bounds.x);
	lua_pushnumber(L, bounds.y);
	lua_pushnumber(L, bounds.w);
	lua_pushnumber(L, bounds.h);
	return 4;
}

static int nk_love_layout_space_to_screen(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 3);
	nk_love_assert_context(1);
	struct nk_vec2 local;
	local.x = luaL_checknumber(L, 2);
	local.y = luaL_checknumber(L, 3);
	struct nk_vec2 screen = nk_layout_space_to_screen(nk_love_checkcontext(1)->nkctx, local);
	lua_pushnumber(L, screen.x);
	lua_pushnumber(L, screen.y);
	return 2;
}

static int nk_love_layout_space_to_local(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 3);
	nk_love_assert_context(1);
	struct nk_vec2 screen;
	screen.x = luaL_checknumber(L, 2);
	screen.y = luaL_checknumber(L, 3);
	struct nk_vec2 local = nk_layout_space_to_local(nk_love_checkcontext(1)->nkctx, screen);
	lua_pushnumber(L, local.x);
	lua_pushnumber(L, local.y);
	return 2;
}

static int nk_love_layout_space_rect_to_screen(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 5);
	nk_love_assert_context(1);
	struct nk_rect local;
	local.x = luaL_checknumber(L, 2);
	local.y = luaL_checknumber(L, 3);
	local.w = luaL_checknumber(L, 4);
	local.h = luaL_checknumber(L, 5);
	struct nk_rect screen = nk_layout_space_rect_to_screen(nk_love_checkcontext(1)->nkctx, local);
	lua_pushnumber(L, screen.x);
	lua_pushnumber(L, screen.y);
	lua_pushnumber(L, screen.w);
	lua_pushnumber(L, screen.h);
	return 4;
}

static int nk_love_layout_space_rect_to_local(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 5);
	nk_love_assert_context(1);
	struct nk_rect screen;
	screen.x = luaL_checknumber(L, 2);
	screen.y = luaL_checknumber(L, 3);
	screen.w = luaL_checknumber(L, 4);
	screen.h = luaL_checknumber(L, 5);
	struct nk_rect local = nk_layout_space_rect_to_screen(nk_love_checkcontext(1)->nkctx, screen);
	lua_pushnumber(L, local.x);
	lua_pushnumber(L, local.y);
	lua_pushnumber(L, local.w);
	lua_pushnumber(L, local.h);
	return 4;
}

static int nk_love_layout_ratio_from_pixel(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	nk_love_assert_context(1);
	float pixel_width = luaL_checknumber(L, 2);
	float ratio = nk_layout_ratio_from_pixel(nk_love_checkcontext(1)->nkctx, pixel_width);
	lua_pushnumber(L, ratio);
	return 1;
}

/*
 * ===============================================================
 *
 *                          WIDGETS
 *
 * ===============================================================
 */

static int nk_love_group_begin(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) >= 2);
	nk_love_assert_context(1);
	const char *title = luaL_checkstring(L, 2);
	nk_flags flags = nk_love_parse_window_flags(3, lua_gettop(L));
	int open = nk_group_begin(nk_love_checkcontext(1)->nkctx, title, flags);
	lua_pushboolean(L, open);
	return 1;
}

static int nk_love_group_end(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	nk_love_assert_context(1);
	nk_group_end(nk_love_checkcontext(1)->nkctx);
	return 0;
}

static int nk_love_group(lua_State *L)
{
	nk_love_assert(lua_checkstack(L, 3), "%s: failed to allocate stack space");
	nk_love_assert_argc(lua_gettop(L) >= 3);
	if (!lua_isfunction(L, -1))
		luaL_typerror(L, lua_gettop(L), "function");
	lua_pushvalue(L, 1);
	lua_insert(L, 2);
	lua_pushvalue(L, 1);
	lua_insert(L, 3);
	lua_insert(L, 2);
	lua_getfield(L, 1, "groupBegin");
	lua_insert(L, 4);
	lua_call(L, lua_gettop(L) - 4, 1);
	int open = lua_toboolean(L, -1);
	lua_pop(L, 1);
	if (open) {
		lua_call(L, 1, 0);
		lua_getfield(L, 1, "groupEnd");
		lua_insert(L, 1);
		lua_call(L, 1, 0);
	} else {
		lua_pop(L, 3);
	}
	return 0;
}

static int nk_love_group_get_scroll(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);

	struct nk_love_context *context = nk_love_checkcontext(1);
	const char *id = luaL_checkstring(L, 2);
	nk_uint x_offset, y_offset;
	nk_group_get_scroll(context->nkctx, id, &x_offset, &y_offset);
	lua_pushinteger(L, x_offset);
	lua_pushinteger(L, y_offset);
	return 2;
}

static int nk_love_group_set_scroll(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 4);
	struct nk_love_context *context = nk_love_checkcontext(1);
	const char *id = luaL_checkstring(L, 2);
	nk_uint x_offset = luaL_checkinteger(L, 3);
	nk_uint y_offset = luaL_checkinteger(L, 4);
	nk_group_set_scroll(context->nkctx, id, x_offset, y_offset);
	return 0;
}

static int nk_love_tree_push(lua_State *L)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc >= 3 && argc <= 5);
	nk_love_assert_context(1);
	struct nk_love_context *ctx = nk_love_checkcontext(1);
	enum nk_tree_type type = nk_love_checktree(2);
	const char *title = luaL_checkstring(L, 3);
	struct nk_image image;
	int use_image = 0;
	if (argc >= 4 && !lua_isnil(L, 4)) {
		nk_love_checkImage(4, &image);
		use_image = 1;
	}
	enum nk_collapse_states state = NK_MINIMIZED;
	if (argc >= 5 && !lua_isnil(L, 5))
		state = nk_love_checkstate(5);
	lua_Debug ar;
	lua_getstack(L, 1, &ar);
	lua_getinfo(L, "l", &ar);
	int id = ar.currentline;
	int open = 0;
	if (use_image)
		open = nk_tree_image_push_hashed(ctx->nkctx, type, image, title, state, title, strlen(title), id);
	else
		open = nk_tree_push_hashed(ctx->nkctx, type, title, state, title, strlen(title), id);
	lua_pushboolean(L, open);
	return 1;
}

static int nk_love_tree_pop(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	nk_love_assert_context(1);
	nk_tree_pop(nk_love_checkcontext(1)->nkctx);
	return 0;
}

static int nk_love_tree(lua_State *L)
{
	nk_love_assert(lua_checkstack(L, 3), "%s: failed to allocate stack space");
	nk_love_assert_argc(lua_gettop(L) >= 4);
	if (!lua_isfunction(L, -1))
		luaL_typerror(L, lua_gettop(L), "function");
	lua_pushvalue(L, 1);
	lua_insert(L, 2);
	lua_pushvalue(L, 1);
	lua_insert(L, 3);
	lua_insert(L, 2);
	lua_getfield(L, 1, "treePush");
	lua_insert(L, 4);
	lua_call(L, lua_gettop(L) - 4, 1);
	int open = lua_toboolean(L, -1);
	lua_pop(L, 1);
	if (open) {
		lua_call(L, 1, 0);
		lua_getfield(L, 1, "treePop");
		lua_insert(L, 1);
		lua_call(L, 1, 0);
	}
	return 0;
}

static int nk_love_tree_state_push(lua_State *L)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc >= 3 && argc <= 5);
	struct nk_love_context *context = nk_love_checkcontext(1);
	enum nk_tree_type type = nk_love_checktree(2);
	const char *title = luaL_checkstring(L, 3);
	struct nk_image image;
	int use_image = 0;
	if (argc >= 4 && !lua_isnil(L, 4)) {
		nk_love_checkImage(4, &image);
		use_image = 1;
	}
	enum nk_collapse_states state = NK_MINIMIZED;
	if (argc >= 5)
		state = nk_love_checkstate(5);

	int open = 0;
	if (use_image)
		open = nk_tree_state_image_push(context->nkctx, type, image, title, &state);
	else
		open = nk_tree_state_push(context->nkctx, type, title, &state);
	lua_pushboolean(L, open);
	return 1;
}

static int nk_love_tree_state_pop(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_tree_state_pop(context->nkctx);
	return 0;
}

static int nk_love_tree_state(lua_State *L)
{
	nk_love_assert(lua_checkstack(L, 3), "%s: failed to allocate stack space");
	nk_love_assert_argc(lua_gettop(L) >= 4);
	if (!lua_isfunction(L, -1))
		luaL_typerror(L, lua_gettop(L), "function");
	lua_pushvalue(L, 1);
	lua_insert(L, 2);
	lua_pushvalue(L, 1);
	lua_insert(L, 3);
	lua_insert(L, 2);
	lua_getfield(L, 1, "treeStatePush");
	lua_insert(L, 4);
	lua_call(L, lua_gettop(L) - 4, 1);
	int open = lua_toboolean(L, -1);
	lua_pop(L, 1);
	if (open) {
		lua_call(L, 1, 0);
		lua_getfield(L, 1, "treeStatePop");
		lua_insert(L, 1);
		lua_call(L, 1, 0);
	}
	return 0;
}


static int nk_love_label(lua_State *L)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc >= 2 && argc <= 4);
	struct nk_love_context *context = nk_love_checkcontext(1);
	const char *text = luaL_checkstring(L, 2);
	nk_flags align = NK_TEXT_LEFT;
	int wrap = 0;
	struct nk_color color;
	int use_color = 0;
	if (argc >= 3) {
		const char *align_string = luaL_checkstring(L, 3);
		if (!strcmp(align_string, "wrap"))
			wrap = 1;
		else
			align = nk_love_checkalign(3);
		if (argc >= 4) {
			color = nk_love_checkcolor(4);
			use_color = 1;
		}
	}
	if (use_color) {
		if (wrap)
			nk_label_colored_wrap(context->nkctx, text, color);
		else
			nk_label_colored(context->nkctx, text, align, color);
	} else {
		if (wrap)
			nk_label_wrap(context->nkctx, text);
		else
			nk_label(context->nkctx, text, align);
	}
	return 0;
}

static int nk_love_image(lua_State *L)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc == 2 || argc == 6);
	struct nk_love_context *context = nk_love_checkcontext(1);
	struct nk_image image;
	nk_love_checkImage(2, &image);
	if (argc == 2) {
		nk_image(context->nkctx, image);
	} else {
		float x = luaL_checknumber(L, 3);
		float y = luaL_checknumber(L, 4);
		float w = luaL_checknumber(L, 5);
		float h = luaL_checknumber(L, 6);
		float line_thickness;
		struct nk_color color;
		nk_love_getGraphics(&line_thickness, &color);
		nk_draw_image(&context->nkctx->current->buffer, nk_rect(x, y, w, h), &image, color);
	}
	return 0;
}

static int nk_love_text(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 6);
	struct nk_love_context *context = nk_love_checkcontext(1);
	const char *text = luaL_checkstring(L, 2);
	float x = luaL_checknumber(L, 3);
	float y = luaL_checknumber(L, 4);
	float w = luaL_checknumber(L, 5);
	float h = luaL_checknumber(L, 6);
	const struct nk_user_font *font = context->nkctx->style.font;
	nk_love_checkFont(-1, &fonts[context->font_count]);
	float line_thickness;
	struct nk_color color;
	nk_love_getGraphics(&line_thickness, &color);
	nk_draw_text(&context->nkctx->current->buffer, nk_rect(x, y, w, h), text,
		     strlen(text), font, nk_rgba(0, 0, 0, 0), color);
	return 0;
}


static int nk_love_button(lua_State *L)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc >= 2 && argc <= 3);
	struct nk_love_context *context = nk_love_checkcontext(1);
	const char *title = NULL;
	if (!lua_isnil(L, 2))
		title = luaL_checkstring(L, 2);
	int use_color = 0, use_image = 0;
	struct nk_color color;
	enum nk_symbol_type symbol = NK_SYMBOL_NONE;
	struct nk_image image;
	if (argc >= 3 && !lua_isnil(L, 3)) {
		if (lua_isstring(L, 3)) {
			if (nk_love_is_color(3)) {
				color = nk_love_checkcolor(3);
				use_color = 1;
			} else {
				symbol = nk_love_checksymbol(3);
			}
		} else {
			nk_love_checkImage(3, &image);
			use_image = 1;
		}
	}
	nk_flags align = context->nkctx->style.button.text_alignment;
	int activated = 0;
	if (title != NULL) {
		if (use_color)
			nk_love_assert(0, "%s: color buttons can't have titles");
		else if (symbol != NK_SYMBOL_NONE)
			activated = nk_button_symbol_label(context->nkctx, symbol, title, align);
		else if (use_image)
			activated = nk_button_image_label(context->nkctx, image, title, align);
		else
			activated = nk_button_label(context->nkctx, title);
	} else {
		if (use_color)
			activated = nk_button_color(context->nkctx, color);
		else if (symbol != NK_SYMBOL_NONE)
			activated = nk_button_symbol(context->nkctx, symbol);
		else if (use_image)
			activated = nk_button_image(context->nkctx, image);
		else
			nk_love_assert(0, "%s: must specify a title, color, symbol, and/or image");
	}
	lua_pushboolean(L, activated);
	return 1;
}

static int nk_love_button_set_behavior(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	struct nk_love_context *context = nk_love_checkcontext(1);
	enum nk_button_behavior behavior = nk_love_checkbehavior(2);
	nk_button_set_behavior(context->nkctx, behavior);
	return 0;
}

static int nk_love_button_push_behavior(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	struct nk_love_context *context = nk_love_checkcontext(1);
	enum nk_button_behavior behavior = nk_love_checkbehavior(2);
	nk_button_push_behavior(context->nkctx, behavior);
	return 0;
}

static int nk_love_button_pop_behavior(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_button_pop_behavior(context->nkctx);
	return 0;
}

static int nk_love_checkbox(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 3);
	struct nk_love_context *context = nk_love_checkcontext(1);
	const char *text = luaL_checkstring(L, 2);
	if (lua_isboolean(L, 3)) {
		int value = lua_toboolean(L, 3);
		value = nk_check_label(context->nkctx, text, value);
		lua_pushboolean(L, value);
	} else if (lua_istable(L, 3)) {
		lua_getfield(L, 3, "value");
		if (!lua_isboolean(L, -1))
			luaL_argerror(L, 3, "should have a boolean value");
		int value = lua_toboolean(L, -1);
		int changed = nk_checkbox_label(context->nkctx, text, &value);
		if (changed) {
			lua_pushboolean(L, value);
			lua_setfield(L, 3, "value");
		}
		lua_pushboolean(L, changed);
	} else {
		luaL_typerror(L, 3, "boolean or table");
	}
	return 1;
}

static int nk_love_radio(lua_State *L)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc == 3 || argc == 4);
	struct nk_love_context *context = nk_love_checkcontext(1);
	const char *name = luaL_checkstring(L, 2);
	const char *text = name;
	if (argc == 4)
		text = luaL_checkstring(L, 3);
	if (lua_isstring(L, -1)) {
		const char *value = lua_tostring(L, -1);
		int active = !strcmp(value, name);
		active = nk_option_label(context->nkctx, text, active);
		if (active)
			lua_pushstring(L, name);
		else
			lua_pushstring(L, value);
	} else if (lua_istable(L, -1)) {
		lua_getfield(L, -1, "value");
		if (!lua_isstring(L, -1))
			luaL_argerror(L, argc, "should have a string value");
		const char *value = lua_tostring(L, -1);
		int active = !strcmp(value, name);
		int changed = nk_radio_label(context->nkctx, text, &active);
		if (changed && active) {
			lua_pushstring(L, name);
			lua_setfield(L, -3, "value");
		}
		lua_pushboolean(L, changed);
	} else {
		luaL_typerror(L, argc, "string or table");
	}
	return 1;
}

static int nk_love_selectable(lua_State *L)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc >= 3 && argc <= 5);
	struct nk_love_context *context = nk_love_checkcontext(1);
	const char *text = luaL_checkstring(L, 2);
	struct nk_image image;
	int use_image = 0;
	if (argc >= 4 && !lua_isnil(L, 3)) {
		nk_love_checkImage(3, &image);
		use_image = 1;
	}
	nk_flags align = NK_TEXT_LEFT;
	if (argc >= 5)
		align = nk_love_checkalign(4);
	if (lua_isboolean(L, -1)) {
		int value = lua_toboolean(L, -1);
		if (use_image)
			value = nk_select_image_label(context->nkctx, image, text, align, value);
		else
			value = nk_select_label(context->nkctx, text, align, value);
		lua_pushboolean(L, value);
	} else if (lua_istable(L, -1)) {
		lua_getfield(L, -1, "value");
		if (!lua_isboolean(L, -1))
			luaL_argerror(L, argc, "should have a boolean value");
		int value = lua_toboolean(L, -1);
		int changed;
		if (use_image)
			changed = nk_selectable_image_label(context->nkctx, image, text, align, &value);
		else
			changed = nk_selectable_label(context->nkctx, text, align, &value);
		if (changed) {
			lua_pushboolean(L, value);
			lua_setfield(L, -3, "value");
		}
		lua_pushboolean(L, changed);
	} else {
		luaL_typerror(L, argc, "boolean or table");
	}
	return 1;
}

static int nk_love_slider(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 5);
	struct nk_love_context *context = nk_love_checkcontext(1);
	float min = luaL_checknumber(L, 2);
	float max = luaL_checknumber(L, 4);
	float step = luaL_checknumber(L, 5);
	if (lua_isnumber(L, 3)) {
		float value = lua_tonumber(L, 3);
		value = nk_slide_float(context->nkctx, min, value, max, step);
		lua_pushnumber(L, value);
	} else if (lua_istable(L, 3)) {
		lua_getfield(L, 3, "value");
		if (!lua_isnumber(L, -1))
			luaL_argerror(L, 3, "should have a number value");
		float value = lua_tonumber(L, -1);
		int changed = nk_slider_float(context->nkctx, min, &value, max, step);
		if (changed) {
			lua_pushnumber(L, value);
			lua_setfield(L, 3, "value");
		}
		lua_pushboolean(L, changed);
	} else {
		luaL_typerror(L, 3, "number or table");
	}
	return 1;
}

static int nk_love_progress(lua_State *L)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc >= 3 || argc <= 4);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_size max = luaL_checkinteger(L, 3);
	int modifiable = 0;
	if (argc >= 4 && !lua_isnil(L, 4))
		modifiable = nk_love_checkboolean(L, 4);
	if (lua_isnumber(L, 2)) {
		nk_size value = lua_tonumber(L, 2);
		value = nk_prog(context->nkctx, value, max, modifiable);
		lua_pushnumber(L, value);
	} else if (lua_istable(L, 2)) {
		lua_getfield(L, 2, "value");
		if (!lua_isnumber(L, -1))
			luaL_argerror(L, 2, "should have a number value");
		nk_size value = (nk_size) lua_tonumber(L, -1);
		int changed = nk_progress(context->nkctx, &value, max, modifiable);
		if (changed) {
			lua_pushnumber(L, value);
			lua_setfield(L, 2, "value");
		}
		lua_pushboolean(L, changed);
	} else {
		luaL_typerror(L, 2, "number or table");
	}
	return 1;
}

static int nk_love_color_picker(lua_State *L)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc >= 2 && argc <= 3);
	struct nk_love_context *context = nk_love_checkcontext(1);
	enum nk_color_format format = NK_RGB;
	if (argc >= 3)
		format = nk_love_checkcolorformat(3);
	if (lua_isstring(L, 2)) {
		struct nk_colorf color = nk_love_checkcolorf(2);
		color = nk_color_picker(context->nkctx, color, format);
		char new_color_string[10];
		nk_love_color((int) (color.r * 255), (int) (color.g * 255),
				(int) (color.b * 255), (int) (color.a * 255), new_color_string);
		lua_pushstring(L, new_color_string);
	} else if (lua_istable(L, 2)) {
		lua_getfield(L, 2, "value");
		if (!nk_love_is_color(-1))
			luaL_argerror(L, 2, "should have a color string value");
		struct nk_colorf color = nk_love_checkcolorf(-1);
		int changed = nk_color_pick(context->nkctx, &color, format);
		if (changed) {
			char new_color_string[10];
			nk_love_color((int) (color.r * 255), (int) (color.g * 255),
					(int) (color.b * 255), (int) (color.a * 255), new_color_string);
			lua_pushstring(L, new_color_string);
			lua_setfield(L, 2, "value");
		}
		lua_pushboolean(L, changed);
	} else {
		luaL_typerror(L, 2, "string or table");
	}
	return 1;
}

static int nk_love_property(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 7);
	struct nk_love_context *context = nk_love_checkcontext(1);
	const char *name = luaL_checkstring(L, 2);
	double min = luaL_checknumber(L, 3);
	double max = luaL_checknumber(L, 5);
	double step = luaL_checknumber(L, 6);
	float inc_per_pixel = luaL_checknumber(L, 7);
	if (lua_isnumber(L, 4)) {
		double value = lua_tonumber(L, 4);
		value = nk_propertyd(context->nkctx, name, min, value, max, step, inc_per_pixel);
		lua_pushnumber(L, value);
	} else if (lua_istable(L, 4)) {
		lua_getfield(L, 4, "value");
		if (!lua_isnumber(L, -1))
			luaL_argerror(L, 4, "should have a number value");
		double value = lua_tonumber(L, -1);
		double old = value;
		nk_property_double(context->nkctx, name, min, &value, max, step, inc_per_pixel);
		int changed = value != old;
		if (changed) {
			lua_pushnumber(L, value);
			lua_setfield(L, 4, "value");
		}
		lua_pushboolean(L, changed);
	} else {
		luaL_typerror(L, 4, "number or table");
	}
	return 1;
}

static int nk_love_edit(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 3);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_flags flags = nk_love_checkedittype(2);
	if (!lua_istable(L, 3))
		luaL_typerror(L, 3, "table");
	lua_getfield(L, 3, "value");
	if (!lua_isstring(L, -1))
		luaL_argerror(L, 3, "should have a string value");
	const char *value = lua_tostring(L, -1);
	size_t len = NK_CLAMP(0, strlen(value), NK_LOVE_EDIT_BUFFER_LEN - 1);
	memcpy(edit_buffer, value, len);
	edit_buffer[len] = '\0';
	nk_flags event = nk_edit_string_zero_terminated(context->nkctx, flags, edit_buffer, NK_LOVE_EDIT_BUFFER_LEN - 1, nk_filter_default);
	lua_pushstring(L, edit_buffer);
	lua_pushvalue(L, -1);
	lua_setfield(L, 3, "value");
	//TODO check this out, it may not be correct
	int changed = !lua_rawequal(L, -1, -2);
	if (event & NK_EDIT_COMMITED)
		lua_pushstring(L, "commited");
	else if (event & NK_EDIT_ACTIVATED)
		lua_pushstring(L, "activated");
	else if (event & NK_EDIT_DEACTIVATED)
		lua_pushstring(L, "deactivated");
	else if (event & NK_EDIT_ACTIVE)
		lua_pushstring(L, "active");
	else if (event & NK_EDIT_INACTIVE)
		lua_pushstring(L, "inactive");
	else
		lua_pushnil(L);
	lua_pushboolean(L, changed);
	return 2;
}

int nk_love_edit_focus(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_edit_focus(context->nkctx, NK_EDIT_DEFAULT);
	return 0;
}

int nk_love_edit_unfocus(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_edit_unfocus(context->nkctx);
	return 0;
}

static int nk_love_popup_begin(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) >= 7);
	struct nk_love_context *context = nk_love_checkcontext(1);
	enum nk_popup_type type = nk_love_checkpopup(2);
	const char *title = luaL_checkstring(L, 3);
	struct nk_rect bounds;
	bounds.x = luaL_checknumber(L, 4);
	bounds.y = luaL_checknumber(L, 5);
	bounds.w = luaL_checknumber(L, 6);
	bounds.h = luaL_checknumber(L, 7);
	nk_flags flags = nk_love_parse_window_flags(8, lua_gettop(L));
	int open = nk_popup_begin(context->nkctx, type, title, flags, bounds);
	lua_pushboolean(L, open);
	return 1;
}

static int nk_love_popup_close(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_popup_close(context->nkctx);
	return 0;
}

static int nk_love_popup_end(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_popup_end(context->nkctx);
	return 0;
}

static int nk_love_popup(lua_State *L)
{
	nk_love_assert(lua_checkstack(L, 3), "%s: failed to allocate stack space");
	nk_love_assert_argc(lua_gettop(L) >= 8);
	if (!lua_isfunction(L, -1))
		luaL_typerror(L, lua_gettop(L), "function");
	lua_pushvalue(L, 1);
	lua_insert(L, 2);
	lua_pushvalue(L, 1);
	lua_insert(L, 3);
	lua_insert(L, 2);
	lua_getfield(L, 1, "popupBegin");
	lua_insert(L, 4);
	lua_call(L, lua_gettop(L) - 4, 1);
	int open = lua_toboolean(L, -1);
	lua_pop(L, 1);
	if (open) {
		lua_call(L, 1, 0);
		lua_getfield(L, 1, "popupEnd");
		lua_insert(L, 1);
		lua_call(L, 1, 0);
	}
	return 0;
}

static int nk_love_popup_get_scroll(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_uint offset_x, offset_y;
	nk_popup_get_scroll(context->nkctx, &offset_x, &offset_y);
	lua_pushinteger(L, offset_x);
	lua_pushinteger(L, offset_y);
	return 2;
}

static int nk_love_popup_set_scroll(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 3);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_uint offset_x, offset_y;
	offset_x = luaL_checkinteger(L, 2);
	offset_y = luaL_checkinteger(L, 3);
	nk_popup_set_scroll(context->nkctx, offset_x, offset_y);
	return 0;
}

static int nk_love_combobox(lua_State *L)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc >= 3 && argc <= 6);
	struct nk_love_context *context = nk_love_checkcontext(1);
	if (lua_isfunction(L, -1)) {
		nk_love_assert(lua_checkstack(L, 3), "%s: failed to allocate stack space");
		lua_pushvalue(L, 1);
		lua_insert(L, 2);
		lua_pushvalue(L, 1);
		lua_insert(L, 3);
		lua_insert(L, 2);
		lua_getfield(L, 1, "comboboxBegin");
		lua_insert(L, 4);
		lua_call(L, lua_gettop(L) - 4, 1);
		int open = lua_toboolean(L, -1);
		lua_pop(L, 1);
		if (open) {
			lua_call(L, 1, 0);
			lua_getfield(L, 1, "comboboxEnd");
			lua_insert(L, 1);
			lua_call(L, 1, 0);
		}
		return 0;
	}
	if (!lua_istable(L, 3))
		luaL_typerror(L, 3, "table");
	int i;
	for (i = 0; i < NK_LOVE_COMBOBOX_MAX_ITEMS && lua_checkstack(L, 4); ++i) {
		lua_rawgeti(L, 3, i + 1);
		if (lua_isstring(L, -1))
			combobox_items[i] = lua_tostring(L, -1);
		else if (lua_isnil(L, -1))
			break;
		else
			luaL_argerror(L, 3, "items must be strings");
	}
	struct nk_rect bounds = nk_widget_bounds(context->nkctx);
	int item_height = bounds.h;
	if (argc >= 4 && !lua_isnil(L, 4))
		item_height = luaL_checkinteger(L, 4);
	struct nk_vec2 size = nk_vec2(bounds.w, item_height * 8);
	if (argc >= 5 && !lua_isnil(L, 5))
		size.x = luaL_checknumber(L, 5);
	if (argc >= 6 && !lua_isnil(L, 6))
		size.y = luaL_checknumber(L, 6);
	if (lua_isnumber(L, 2)) {
		int value = lua_tointeger(L, 2) - 1;
		value = nk_combo(context->nkctx, combobox_items, i, value, item_height, size);
		lua_pushnumber(L, value + 1);
	} else if (lua_istable(L, 2)) {
		lua_getfield(L, 2, "value");
		if (!lua_isnumber(L, -1))
			luaL_argerror(L, 2, "should have a number value");
		int value = luaL_checkinteger(L, -1) - 1;
		int old = value;
		nk_combobox(context->nkctx, combobox_items, i, &value, item_height, size);
		int changed = value != old;
		if (changed) {
			lua_pushnumber(L, value + 1);
			lua_setfield(L, 2, "value");
		}
		lua_pushboolean(L, changed);
	} else {
		luaL_typerror(L, 2, "number or table");
	}
	return 1;
}

static int nk_love_combobox_begin(lua_State *L)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc >= 2 && argc <= 5);
	struct nk_love_context *context = nk_love_checkcontext(1);
	const char *text = NULL;
	if (!lua_isnil(L, 2))
		text = luaL_checkstring(L, 2);
	struct nk_color color;
	int use_color = 0;
	enum nk_symbol_type symbol = NK_SYMBOL_NONE;
	struct nk_image image;
	int use_image = 0;
	if (argc >= 3 && !lua_isnil(L, 3)) {
		if (lua_isstring(L, 3)) {
			if (nk_love_is_color(3)) {
				color = nk_love_checkcolor(3);
				use_color = 1;
			} else {
				symbol = nk_love_checksymbol(3);
			}
		} else {
			nk_love_checkImage(3, &image);
			use_image = 1;
		}
	}
	struct nk_rect bounds = nk_widget_bounds(context->nkctx);
	struct nk_vec2 size = nk_vec2(bounds.w, bounds.h * 8);
	if (argc >= 4 && !lua_isnil(L, 4))
		size.x = luaL_checknumber(L, 4);
	if (argc >= 5 && !lua_isnil(L, 5))
		size.y = luaL_checknumber(L, 5);
	int open = 0;
	if (text != NULL) {
		if (use_color)
			nk_love_assert(0, "%s: color comboboxes can't have titles");
		else if (symbol != NK_SYMBOL_NONE)
			open = nk_combo_begin_symbol_label(context->nkctx, text, symbol, size);
		else if (use_image)
			open = nk_combo_begin_image_label(context->nkctx, text, image, size);
		else
			open = nk_combo_begin_label(context->nkctx, text, size);
	} else {
		if (use_color)
			open = nk_combo_begin_color(context->nkctx, color, size);
		else if (symbol != NK_SYMBOL_NONE)
			open = nk_combo_begin_symbol(context->nkctx, symbol, size);
		else if (use_image)
			open = nk_combo_begin_image(context->nkctx, image, size);
		else
			nk_love_assert(0, "%s: must specify color, symbol, image, and/or title");
	}
	lua_pushboolean(L, open);
	return 1;
}

static int nk_love_combobox_item(lua_State *L)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc >= 2 && argc <= 4);
	struct nk_love_context *context = nk_love_checkcontext(1);
	const char *text = luaL_checkstring(L, 2);
	enum nk_symbol_type symbol = NK_SYMBOL_NONE;
	struct nk_image image;
	int use_image = 0;
	if (argc >= 3 && !lua_isnil(L, 3)) {
		if (lua_isstring(L, 3)) {
			symbol = nk_love_checksymbol(3);
		} else {
			nk_love_checkImage(3, &image);
			use_image = 1;
		}
	}
	nk_flags align = NK_TEXT_LEFT;
	if (argc >= 4 && !lua_isnil(L, 4))
		align = nk_love_checkalign(4);
	int activated = 0;
	if (symbol != NK_SYMBOL_NONE)
		activated = nk_combo_item_symbol_label(context->nkctx, symbol, text, align);
	else if (use_image)
		activated = nk_combo_item_image_label(context->nkctx, image, text, align);
	else
		activated = nk_combo_item_label(context->nkctx, text, align);
	lua_pushboolean(L, activated);
	return 1;
}

static int nk_love_combobox_close(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_combo_close(context->nkctx);
	return 0;
}

static int nk_love_combobox_end(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_combo_end(context->nkctx);
	return 0;
}

static int nk_love_contextual_begin(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) >= 7);
	struct nk_love_context *context = nk_love_checkcontext(1);
	struct nk_vec2 size;
	size.x = luaL_checknumber(L, 2);
	size.y = luaL_checknumber(L, 3);
	struct nk_rect trigger;
	trigger.x = luaL_checknumber(L, 4);
	trigger.y = luaL_checknumber(L, 5);
	trigger.w = luaL_checknumber(L, 6);
	trigger.h = luaL_checknumber(L, 7);
	nk_flags flags = nk_love_parse_window_flags(8, lua_gettop(L));
	int open = nk_contextual_begin(context->nkctx, flags, size, trigger);
	lua_pushboolean(L, open);
	return 1;
}

static int nk_love_contextual_item(lua_State *L)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc >= 2 && argc <= 4);
	struct nk_love_context *context = nk_love_checkcontext(1);
	const char *text = luaL_checkstring(L, 2);
	enum nk_symbol_type symbol = NK_SYMBOL_NONE;
	struct nk_image image;
	int use_image = 0;
	if (argc >= 3 && !lua_isnil(L, 3)) {
		if (lua_isstring(L, 3)) {
			symbol = nk_love_checksymbol(3);
		} else {
			nk_love_checkImage(3, &image);
			use_image = 1;
		}
	}
	nk_flags align = NK_TEXT_LEFT;
	if (argc >= 4 && !lua_isnil(L, 4))
		align = nk_love_checkalign(4);
	int activated;
	if (symbol != NK_SYMBOL_NONE)
		activated = nk_contextual_item_symbol_label(context->nkctx, symbol, text, align);
	else if (use_image)
		activated = nk_contextual_item_image_label(context->nkctx, image, text, align);
	else
		activated = nk_contextual_item_label(context->nkctx, text, align);
	lua_pushboolean(L, activated);
	return 1;
}

static int nk_love_contextual_close(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_contextual_close(context->nkctx);
	return 0;
}

static int nk_love_contextual_end(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_contextual_end(context->nkctx);
	return 0;
}

static int nk_love_contextual(lua_State *L)
{
	nk_love_assert(lua_checkstack(L, 3), "%s: failed to allocate stack space");
	nk_love_assert_argc(lua_gettop(L) >= 8);
	if (!lua_isfunction(L, -1))
		luaL_typerror(L, lua_gettop(L), "function");
	lua_pushvalue(L, 1);
	lua_insert(L, 2);
	lua_pushvalue(L, 1);
	lua_insert(L, 3);
	lua_insert(L, 2);
	lua_getfield(L, 1, "contextualBegin");
	lua_insert(L, 4);
	lua_call(L, lua_gettop(L) - 4, 1);
	int open = lua_toboolean(L, -1);
	lua_pop(L, 1);
	if (open) {
		lua_call(L, 1, 0);
		lua_getfield(L, 1, "contextualEnd");
		lua_insert(L, 1);
		lua_call(L, 1, 0);
	}
	return 0;
}

static int nk_love_tooltip_begin(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	struct nk_love_context *context = nk_love_checkcontext(1);
	float width = luaL_checknumber(L, 2);
	int open = nk_tooltip_begin(context->nkctx, width);
	lua_pushnumber(L, open);
	return 1;
}

static int nk_love_tooltip_end(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_tooltip_end(context->nkctx);
	return 0;
}

static int nk_love_tooltip(lua_State *L)
{
	if (lua_gettop(L) == 3) {
		nk_love_assert(lua_checkstack(L, 3), "%s: failed to allocate stack space");
		if (!lua_isfunction(L, -1))
			luaL_typerror(L, lua_gettop(L), "function");
		lua_pushvalue(L, 1);
		lua_insert(L, 2);
		lua_pushvalue(L, 1);
		lua_insert(L, 3);
		lua_insert(L, 2);
		lua_getfield(L, 1, "tooltipBegin");
		lua_insert(L, 4);
		lua_call(L, 2, 1);
		int open = lua_toboolean(L, -1);
		lua_pop(L, 1);
		if (open) {
			lua_call(L, 1, 0);
			lua_getfield(L, 1, "tooltipEnd");
			lua_insert(L, 1);
			lua_call(L, 1, 0);
		}
	} else {
		nk_love_assert_argc(lua_gettop(L) == 2);
		struct nk_love_context *context = nk_love_checkcontext(1);
		const char *text = luaL_checkstring(L, 2);
		nk_tooltip(context->nkctx, text);
	}
	return 0;
}

static int nk_love_menubar_begin(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_menubar_begin(context->nkctx);
	return 0;
}

static int nk_love_menubar_end(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_menubar_end(context->nkctx);
	return 0;
}

static int nk_love_menubar(lua_State *L)
{
	nk_love_assert(lua_checkstack(L, 3), "%s: failed to allocate stack space");
	nk_love_assert_argc(lua_gettop(L) == 2);
	if (!lua_isfunction(L, -1))
		luaL_typerror(L, lua_gettop(L), "function");
	lua_pushvalue(L, 1);
	lua_insert(L, 2);
	lua_pushvalue(L, 1);
	lua_insert(L, 3);
	lua_insert(L, 2);
	lua_getfield(L, 1, "menubarBegin");
	lua_insert(L, 4);
	lua_call(L, 1, 0);
	lua_call(L, 1, 0);
	lua_getfield(L, 1, "menubarEnd");
	lua_insert(L, 1);
	lua_call(L, 1, 0);
	return 0;
}

static int nk_love_menu_begin(lua_State *L)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc >= 5 && argc <= 6);
	struct nk_love_context *context = nk_love_checkcontext(1);
	const char *text = luaL_checkstring(L, 2);
	enum nk_symbol_type symbol = NK_SYMBOL_NONE;
	struct nk_image image;
	int use_image = 0;
	if (lua_isstring(L, 3)) {
		symbol = nk_love_checksymbol(3);
	} else if (!lua_isnil(L, 3)) {
		nk_love_checkImage(3, &image);
		use_image = 1;
	}
	struct nk_vec2 size;
	size.x = luaL_checknumber(L, 4);
	size.y = luaL_checknumber(L, 5);
	nk_flags align = NK_TEXT_LEFT;
	if (argc >= 6 && !lua_isnil(L, 6))
		align = nk_love_checkalign(6);
	int open;
	if (symbol != NK_SYMBOL_NONE)
		open = nk_menu_begin_symbol_label(context->nkctx, text, align, symbol, size);
	else if (use_image)
		open = nk_menu_begin_image_label(context->nkctx, text, align, image, size);
	else
		open = nk_menu_begin_label(context->nkctx, text, align, size);
	lua_pushboolean(L, open);
	return 1;
}

static int nk_love_menu_item(lua_State *L)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc >= 2 && argc <= 4);
	struct nk_love_context *context = nk_love_checkcontext(1);
	const char *text = luaL_checkstring(L, 2);
	enum nk_symbol_type symbol = NK_SYMBOL_NONE;
	struct nk_image image;
	int use_image = 0;
	if (argc >= 3 && !lua_isnil(L, 3)) {
		if (lua_isstring(L, 3)) {
			symbol = nk_love_checksymbol(3);
		} else {
			nk_love_checkImage(3, &image);
			use_image = 1;
		}
	}
	nk_flags align = NK_TEXT_LEFT;
	if (argc >= 4 && !lua_isnil(L, 4))
		align = nk_love_checkalign(4);
	int activated;
	if (symbol != NK_SYMBOL_NONE)
		activated = nk_menu_item_symbol_label(context->nkctx, symbol, text, align);
	else if (use_image)
		activated = nk_menu_item_image_label(context->nkctx, image, text, align);
	else
		activated = nk_menu_item_label(context->nkctx, text, align);
	lua_pushboolean(L, activated);
	return 1;
}

static int nk_love_menu_close(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_menu_close(context->nkctx);
	return 0;
}

static int nk_love_menu_end(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	nk_menu_end(context->nkctx);
	return 0;
}

static int nk_love_menu(lua_State *L)
{
	nk_love_assert(lua_checkstack(L, 3), "%s: failed to allocate stack space");
	nk_love_assert_argc(lua_gettop(L) == 6 || lua_gettop(L) == 7);
	if (!lua_isfunction(L, -1))
		luaL_typerror(L, lua_gettop(L), "function");
	lua_pushvalue(L, 1);
	lua_insert(L, 2);
	lua_pushvalue(L, 1);
	lua_insert(L, 3);
	lua_insert(L, 2);
	lua_getfield(L, 1, "menuBegin");
	lua_insert(L, 4);
	lua_call(L, lua_gettop(L) - 4, 1);
	int open = lua_toboolean(L, -1);
	lua_pop(L, 1);
	if (open) {
		lua_call(L, 1, 0);
		lua_getfield(L, 1, "menuEnd");
		lua_insert(L, 1);
		lua_call(L, 1, 0);
	}
	return 0;
}

/*
 * ===============================================================
 *
 *                            STYLE
 *
 * ===============================================================
 */

static int nk_love_style_default(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *ctx = nk_love_checkcontext(1);
	nk_style_default(ctx->nkctx);
	return 0;
}

#define NK_LOVE_LOAD_COLOR(type) \
	lua_getfield(L, -1, (type)); \
	if (!nk_love_is_color(-1)) { \
		const char *msg = lua_pushfstring(L, "%%s: table missing color value for '%s'", type); \
		nk_love_assert(0, msg); \
	} \
	colors[index++] = nk_love_checkcolor(-1); \
	lua_pop(L, 1)

static int nk_love_style_load_colors(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	struct nk_love_context *ctx = nk_love_checkcontext(1);
	if (!lua_istable(L, 2))
		luaL_typerror(L, 2, "table");
	struct nk_color colors[NK_COLOR_COUNT];
	int index = 0;
	NK_LOVE_LOAD_COLOR("text");
	NK_LOVE_LOAD_COLOR("window");
	NK_LOVE_LOAD_COLOR("header");
	NK_LOVE_LOAD_COLOR("border");
	NK_LOVE_LOAD_COLOR("button");
	NK_LOVE_LOAD_COLOR("button hover");
	NK_LOVE_LOAD_COLOR("button active");
	NK_LOVE_LOAD_COLOR("toggle");
	NK_LOVE_LOAD_COLOR("toggle hover");
	NK_LOVE_LOAD_COLOR("toggle cursor");
	NK_LOVE_LOAD_COLOR("select");
	NK_LOVE_LOAD_COLOR("select active");
	NK_LOVE_LOAD_COLOR("slider");
	NK_LOVE_LOAD_COLOR("slider cursor");
	NK_LOVE_LOAD_COLOR("slider cursor hover");
	NK_LOVE_LOAD_COLOR("slider cursor active");
	NK_LOVE_LOAD_COLOR("property");
	NK_LOVE_LOAD_COLOR("edit");
	NK_LOVE_LOAD_COLOR("edit cursor");
	NK_LOVE_LOAD_COLOR("combo");
	NK_LOVE_LOAD_COLOR("chart");
	NK_LOVE_LOAD_COLOR("chart color");
	NK_LOVE_LOAD_COLOR("chart color highlight");
	NK_LOVE_LOAD_COLOR("scrollbar");
	NK_LOVE_LOAD_COLOR("scrollbar cursor");
	NK_LOVE_LOAD_COLOR("scrollbar cursor hover");
	NK_LOVE_LOAD_COLOR("scrollbar cursor active");
	NK_LOVE_LOAD_COLOR("tab header");
	nk_style_from_table(ctx->nkctx, colors);
	return 0;
}

static int nk_love_style_set_font(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	struct nk_love_context *ctx = nk_love_checkcontext(1);
	nk_love_checkFont(2, &fonts[ctx->font_count]);
	nk_style_set_font(ctx->nkctx, &fonts[ctx->font_count++]);
	return 0;
}

static int nk_love_style_push_color(struct nk_color *field)
{
	struct nk_love_context *ctx = nk_love_checkcontext(1);
	if (!nk_love_is_color(-1)) {
		const char *msg = lua_pushfstring(L, "%%s: bad color string '%s'", lua_tostring(L, -1));
		nk_love_assert(0, msg);
	}
	struct nk_color color = nk_love_checkcolor(-1);
	int success = nk_style_push_color(ctx->nkctx, field, color);
	if (success) {
		lua_pushstring(L, "color");
		size_t stack_size = lua_rawlen(L, 2);
		lua_rawseti(L, 2, stack_size + 1);
	}
	return success;
}

static int nk_love_style_push_vec2(struct nk_vec2 *field)
{
	struct nk_love_context *ctx = nk_love_checkcontext(1);
	static const char *msg = "%s: vec2 fields must have x and y components";
	nk_love_assert(lua_istable(L, -1), msg);
	lua_getfield(L, -1, "x");
	nk_love_assert(lua_isnumber(L, -1), msg);
	lua_getfield(L, -2, "y");
	nk_love_assert(lua_isnumber(L, -1), msg);
	struct nk_vec2 vec2;
	vec2.x = lua_tonumber(L, -2);
	vec2.y = lua_tonumber(L, -1);
	lua_pop(L, 2);
	int success = nk_style_push_vec2(ctx->nkctx, field, vec2);
	if (success) {
		lua_pushstring(L, "vec2");
		size_t stack_size = lua_rawlen(L, 2);
		lua_rawseti(L, 2, stack_size + 1);
	}
	return success;
}

static int nk_love_style_push_item(struct nk_style_item *field)
{
	struct nk_love_context *ctx = nk_love_checkcontext(1);
	struct nk_style_item item;
	if (lua_isstring(L, -1)) {
		if (!nk_love_is_color(-1)) {
			const char *msg = lua_pushfstring(L, "%%s: bad color string '%s'", lua_tostring(L, -1));
			nk_love_assert(0, msg);
		}
		item.type = NK_STYLE_ITEM_COLOR;
		item.data.color = nk_love_checkcolor(-1);
	} else {
		item.type = NK_STYLE_ITEM_IMAGE;
		nk_love_checkImage(-1, &item.data.image);
	}
	int success = nk_style_push_style_item(ctx->nkctx, field, item);
	if (success) {
		lua_pushstring(L, "item");
		size_t stack_size = lua_rawlen(L, 2);
		lua_rawseti(L, 2, stack_size + 1);
	}
	return success;
}

static int nk_love_style_push_align(nk_flags *field)
{
	struct nk_love_context *ctx = nk_love_checkcontext(1);
	nk_flags align = nk_love_checkalign(-1);
	int success = nk_style_push_flags(ctx->nkctx, field, align);
	if (success) {
		lua_pushstring(L, "flags");
		size_t stack_size = lua_rawlen(L, 2);
		lua_rawseti(L, 2, stack_size + 1);
	}
	return success;
}

static int nk_love_style_push_float(float *field)
{
	struct nk_love_context *ctx = nk_love_checkcontext(1);
	float f = luaL_checknumber(L, -1);
	int success = nk_style_push_float(ctx->nkctx, field, f);
	if (success) {
		lua_pushstring(L, "float");
		size_t stack_size = lua_rawlen(L, 2);
		lua_rawseti(L, 2, stack_size + 1);
	}
	return success;
}

static int nk_love_style_push_font(const struct nk_user_font **field)
{
	struct nk_love_context *ctx = nk_love_checkcontext(1);
	nk_love_checkFont(-1, &fonts[CONTEXT->font_count]);
	int success = nk_style_push_font(ctx->nkctx, &fonts[CONTEXT->font_count++]);
	if (success) {
		lua_pushstring(L, "font");
		size_t stack_size = lua_rawlen(L, 2);
		lua_rawseti(L, 2, stack_size + 1);
	}
	return success;
}

#define NK_LOVE_STYLE_PUSH(name, type, field) \
	nk_love_assert(lua_istable(L, -1), "%s: " name " field must be a table"); \
	lua_getfield(L, -1, name); \
	if (!lua_isnil(L, -1)) \
		nk_love_style_push_##type(field); \
	lua_pop(L, 1);

static void nk_love_style_push_text(struct nk_style_text *style)
{
	nk_love_assert(lua_istable(L, -1), "%s: text style must be a table");
	NK_LOVE_STYLE_PUSH("color", color, &style->color);
	NK_LOVE_STYLE_PUSH("padding", vec2, &style->padding);
}

static void nk_love_style_push_button(struct nk_style_button *style)
{
	nk_love_assert(lua_istable(L, -1), "%s: button style must be a table");
	NK_LOVE_STYLE_PUSH("normal", item, &style->normal);
	NK_LOVE_STYLE_PUSH("hover", item, &style->hover);
	NK_LOVE_STYLE_PUSH("active", item, &style->active);
	NK_LOVE_STYLE_PUSH("border color", color, &style->border_color);
	NK_LOVE_STYLE_PUSH("text background", color, &style->text_background);
	NK_LOVE_STYLE_PUSH("text normal", color, &style->text_normal);
	NK_LOVE_STYLE_PUSH("text hover", color, &style->text_hover);
	NK_LOVE_STYLE_PUSH("text active", color, &style->text_active);
	NK_LOVE_STYLE_PUSH("text alignment", align, &style->text_alignment);
	NK_LOVE_STYLE_PUSH("border", float, &style->border);
	NK_LOVE_STYLE_PUSH("rounding", float, &style->rounding);
	NK_LOVE_STYLE_PUSH("padding", vec2, &style->padding);
	NK_LOVE_STYLE_PUSH("image padding", vec2, &style->image_padding);
	NK_LOVE_STYLE_PUSH("touch padding", vec2, &style->touch_padding);
}

static void nk_love_style_push_scrollbar(struct nk_style_scrollbar *style)
{
	nk_love_assert(lua_istable(L, -1), "%s: scrollbar style must be a table");
	NK_LOVE_STYLE_PUSH("normal", item, &style->normal);
	NK_LOVE_STYLE_PUSH("hover", item, &style->hover);
	NK_LOVE_STYLE_PUSH("active", item, &style->active);
	NK_LOVE_STYLE_PUSH("border color", color, &style->border_color);
	NK_LOVE_STYLE_PUSH("cursor normal", item, &style->cursor_normal);
	NK_LOVE_STYLE_PUSH("cursor hover", item, &style->cursor_hover);
	NK_LOVE_STYLE_PUSH("cursor active", item, &style->active);
	NK_LOVE_STYLE_PUSH("cursor border color", color, &style->cursor_border_color);
	NK_LOVE_STYLE_PUSH("border", float, &style->border);
	NK_LOVE_STYLE_PUSH("rounding", float, &style->rounding);
	NK_LOVE_STYLE_PUSH("border cursor", float, &style->border_cursor);
	NK_LOVE_STYLE_PUSH("rounding cursor", float, &style->rounding_cursor);
	NK_LOVE_STYLE_PUSH("padding", vec2, &style->padding);
}

static void nk_love_style_push_edit(struct nk_style_edit *style)
{
	nk_love_assert(lua_istable(L, -1), "%s: edit style must be a table");
	NK_LOVE_STYLE_PUSH("normal", item, &style->normal);
	NK_LOVE_STYLE_PUSH("hover", item, &style->hover);
	NK_LOVE_STYLE_PUSH("active", item, &style->active);
	NK_LOVE_STYLE_PUSH("border color", color, &style->border_color);
	NK_LOVE_STYLE_PUSH("scrollbar", scrollbar, &style->scrollbar);
	NK_LOVE_STYLE_PUSH("cursor normal", color, &style->cursor_normal);
	NK_LOVE_STYLE_PUSH("cursor hover", color, &style->cursor_hover);
	NK_LOVE_STYLE_PUSH("cursor text normal", color, &style->cursor_text_normal);
	NK_LOVE_STYLE_PUSH("cursor text hover", color, &style->cursor_text_hover);
	NK_LOVE_STYLE_PUSH("text normal", color, &style->text_normal);
	NK_LOVE_STYLE_PUSH("text hover", color, &style->text_hover);
	NK_LOVE_STYLE_PUSH("text active", color, &style->text_active);
	NK_LOVE_STYLE_PUSH("selected normal", color, &style->selected_normal);
	NK_LOVE_STYLE_PUSH("selected hover", color, &style->selected_hover);
	NK_LOVE_STYLE_PUSH("selected text normal", color, &style->text_normal);
	NK_LOVE_STYLE_PUSH("selected text hover", color, &style->selected_text_hover);
	NK_LOVE_STYLE_PUSH("border", float, &style->border);
	NK_LOVE_STYLE_PUSH("rounding", float, &style->rounding);
	NK_LOVE_STYLE_PUSH("cursor size", float, &style->cursor_size);
	NK_LOVE_STYLE_PUSH("scrollbar size", vec2, &style->scrollbar_size);
	NK_LOVE_STYLE_PUSH("padding", vec2, &style->padding);
	NK_LOVE_STYLE_PUSH("row padding", float, &style->row_padding);
}

static void nk_love_style_push_toggle(struct nk_style_toggle *style)
{
	nk_love_assert(lua_istable(L, -1), "%s: toggle style must be a table");
	NK_LOVE_STYLE_PUSH("normal", item, &style->normal);
	NK_LOVE_STYLE_PUSH("hover", item, &style->hover);
	NK_LOVE_STYLE_PUSH("active", item, &style->active);
	NK_LOVE_STYLE_PUSH("border color", color, &style->border_color);
	NK_LOVE_STYLE_PUSH("cursor normal", item, &style->cursor_normal);
	NK_LOVE_STYLE_PUSH("cursor hover", item, &style->cursor_hover);
	NK_LOVE_STYLE_PUSH("text normal", color, &style->text_normal);
	NK_LOVE_STYLE_PUSH("text hover", color, &style->text_hover);
	NK_LOVE_STYLE_PUSH("text active", color, &style->text_active);
	NK_LOVE_STYLE_PUSH("text background", color, &style->text_background);
	NK_LOVE_STYLE_PUSH("text alignment", align, &style->text_alignment);
	NK_LOVE_STYLE_PUSH("padding", vec2, &style->padding);
	NK_LOVE_STYLE_PUSH("touch padding", vec2, &style->touch_padding);
	NK_LOVE_STYLE_PUSH("spacing", float, &style->spacing);
	NK_LOVE_STYLE_PUSH("border", float, &style->border);
}

static void nk_love_style_push_selectable(struct nk_style_selectable *style)
{
	nk_love_assert(lua_istable(L, -1), "%s: selectable style must be a table");
	NK_LOVE_STYLE_PUSH("normal", item, &style->normal);
	NK_LOVE_STYLE_PUSH("hover", item, &style->hover);
	NK_LOVE_STYLE_PUSH("pressed", item, &style->pressed);
	NK_LOVE_STYLE_PUSH("normal active", item, &style->normal_active);
	NK_LOVE_STYLE_PUSH("hover active", item, &style->hover_active);
	NK_LOVE_STYLE_PUSH("pressed active", item, &style->pressed_active);
	NK_LOVE_STYLE_PUSH("text normal", color, &style->text_normal);
	NK_LOVE_STYLE_PUSH("text hover", color, &style->text_hover);
	NK_LOVE_STYLE_PUSH("text pressed", color, &style->text_pressed);
	NK_LOVE_STYLE_PUSH("text normal active", color, &style->text_normal_active);
	NK_LOVE_STYLE_PUSH("text hover active", color, &style->text_hover_active);
	NK_LOVE_STYLE_PUSH("text pressed active", color, &style->text_pressed_active);
	NK_LOVE_STYLE_PUSH("text background", color, &style->text_background);
	NK_LOVE_STYLE_PUSH("text alignment", align, &style->text_alignment);
	NK_LOVE_STYLE_PUSH("rounding", float, &style->rounding);
	NK_LOVE_STYLE_PUSH("padding", vec2, &style->padding);
	NK_LOVE_STYLE_PUSH("touch padding", vec2, &style->touch_padding);
	NK_LOVE_STYLE_PUSH("image padding", vec2, &style->image_padding);
}

static void nk_love_style_push_slider(struct nk_style_slider *style)
{
	nk_love_assert(lua_istable(L, -1), "%s: slider style must be a table");
	NK_LOVE_STYLE_PUSH("normal", item, &style->normal);
	NK_LOVE_STYLE_PUSH("hover", item, &style->hover);
	NK_LOVE_STYLE_PUSH("active", item, &style->active);
	NK_LOVE_STYLE_PUSH("border color", color, &style->border_color);
	NK_LOVE_STYLE_PUSH("bar normal", color, &style->bar_normal);
	NK_LOVE_STYLE_PUSH("bar active", color, &style->bar_active);
	NK_LOVE_STYLE_PUSH("bar filled", color, &style->bar_filled);
	NK_LOVE_STYLE_PUSH("cursor normal", item, &style->cursor_normal);
	NK_LOVE_STYLE_PUSH("cursor hover", item, &style->cursor_hover);
	NK_LOVE_STYLE_PUSH("cursor active", item, &style->cursor_active);
	NK_LOVE_STYLE_PUSH("border", float, &style->border);
	NK_LOVE_STYLE_PUSH("rounding", float, &style->rounding);
	NK_LOVE_STYLE_PUSH("bar height", float, &style->bar_height);
	NK_LOVE_STYLE_PUSH("padding", vec2, &style->padding);
	NK_LOVE_STYLE_PUSH("spacing", vec2, &style->spacing);
	NK_LOVE_STYLE_PUSH("cursor size", vec2, &style->cursor_size);
}

static void nk_love_style_push_progress(struct nk_style_progress *style)
{
	nk_love_assert(lua_istable(L, -1), "%s: progress style must be a table");
	NK_LOVE_STYLE_PUSH("normal", item, &style->normal);
	NK_LOVE_STYLE_PUSH("hover", item, &style->hover);
	NK_LOVE_STYLE_PUSH("active", item, &style->active);
	NK_LOVE_STYLE_PUSH("border color", color, &style->border_color);
	NK_LOVE_STYLE_PUSH("cursor normal", item, &style->cursor_normal);
	NK_LOVE_STYLE_PUSH("cursor hover", item, &style->cursor_hover);
	NK_LOVE_STYLE_PUSH("cursor active", item, &style->cursor_active);
	NK_LOVE_STYLE_PUSH("cursor border color", color, &style->cursor_border_color);
	NK_LOVE_STYLE_PUSH("rounding", float, &style->rounding);
	NK_LOVE_STYLE_PUSH("border", float, &style->border);
	NK_LOVE_STYLE_PUSH("cursor border", float, &style->cursor_border);
	NK_LOVE_STYLE_PUSH("cursor rounding", float, &style->cursor_rounding);
	NK_LOVE_STYLE_PUSH("padding", vec2, &style->padding);
}

static void nk_love_style_push_property(struct nk_style_property *style)
{
	nk_love_assert(lua_istable(L, -1), "%s: property style must be a table");
	NK_LOVE_STYLE_PUSH("normal", item, &style->normal);
	NK_LOVE_STYLE_PUSH("hover", item, &style->hover);
	NK_LOVE_STYLE_PUSH("active", item, &style->active);
	NK_LOVE_STYLE_PUSH("border color", color, &style->border_color);
	NK_LOVE_STYLE_PUSH("label normal", color, &style->label_normal);
	NK_LOVE_STYLE_PUSH("label hover", color, &style->label_hover);
	NK_LOVE_STYLE_PUSH("label active", color, &style->label_active);
	NK_LOVE_STYLE_PUSH("border", float, &style->border);
	NK_LOVE_STYLE_PUSH("rounding", float, &style->rounding);
	NK_LOVE_STYLE_PUSH("padding", vec2, &style->padding);
	NK_LOVE_STYLE_PUSH("edit", edit, &style->edit);
	NK_LOVE_STYLE_PUSH("inc button", button, &style->inc_button);
	NK_LOVE_STYLE_PUSH("dec button", button, &style->dec_button);
}

static void nk_love_style_push_chart(struct nk_style_chart *style)
{
	nk_love_assert(lua_istable(L, -1), "%s: chart style must be a table");
	NK_LOVE_STYLE_PUSH("background", item, &style->background);
	NK_LOVE_STYLE_PUSH("border color", color, &style->border_color);
	NK_LOVE_STYLE_PUSH("selected color", color, &style->selected_color);
	NK_LOVE_STYLE_PUSH("color", color, &style->color);
	NK_LOVE_STYLE_PUSH("border", float, &style->border);
	NK_LOVE_STYLE_PUSH("rounding", float, &style->rounding);
	NK_LOVE_STYLE_PUSH("padding", vec2, &style->padding);
}

static void nk_love_style_push_tab(struct nk_style_tab *style)
{
	nk_love_assert(lua_istable(L, -1), "%s: tab style must be a table");
	NK_LOVE_STYLE_PUSH("background", item, &style->background);
	NK_LOVE_STYLE_PUSH("border color", color, &style->border_color);
	NK_LOVE_STYLE_PUSH("text", color, &style->text);
	NK_LOVE_STYLE_PUSH("tab maximize button", button, &style->tab_maximize_button);
	NK_LOVE_STYLE_PUSH("tab minimize button", button, &style->tab_minimize_button);
	NK_LOVE_STYLE_PUSH("node maximize button", button, &style->node_maximize_button);
	NK_LOVE_STYLE_PUSH("node minimize button", button, &style->node_minimize_button);
	NK_LOVE_STYLE_PUSH("border", float, &style->border);
	NK_LOVE_STYLE_PUSH("rounding", float, &style->rounding);
	NK_LOVE_STYLE_PUSH("indent", float, &style->indent);
	NK_LOVE_STYLE_PUSH("padding", vec2, &style->padding);
	NK_LOVE_STYLE_PUSH("spacing", vec2, &style->spacing);
}

static void nk_love_style_push_combo(struct nk_style_combo *style)
{
	nk_love_assert(lua_istable(L, -1), "%s: combo style must be a table");
	NK_LOVE_STYLE_PUSH("normal", item, &style->normal);
	NK_LOVE_STYLE_PUSH("hover", item, &style->hover);
	NK_LOVE_STYLE_PUSH("active", item, &style->active);
	NK_LOVE_STYLE_PUSH("border color", color, &style->border_color);
	NK_LOVE_STYLE_PUSH("label normal", color, &style->label_normal);
	NK_LOVE_STYLE_PUSH("label hover", color, &style->label_hover);
	NK_LOVE_STYLE_PUSH("label active", color, &style->label_active);
	NK_LOVE_STYLE_PUSH("symbol normal", color, &style->symbol_normal);
	NK_LOVE_STYLE_PUSH("symbol hover", color, &style->symbol_hover);
	NK_LOVE_STYLE_PUSH("symbol active", color, &style->symbol_active);
	NK_LOVE_STYLE_PUSH("button", button, &style->button);
	NK_LOVE_STYLE_PUSH("border", float, &style->border);
	NK_LOVE_STYLE_PUSH("rounding", float, &style->rounding);
	NK_LOVE_STYLE_PUSH("content padding", vec2, &style->content_padding);
	NK_LOVE_STYLE_PUSH("button padding", vec2, &style->button_padding);
	NK_LOVE_STYLE_PUSH("spacing", vec2, &style->spacing);
}

static void nk_love_style_push_window_header(struct nk_style_window_header *style)
{
	nk_love_assert(lua_istable(L, -1), "%s: window header style must be a table");
	NK_LOVE_STYLE_PUSH("normal", item, &style->normal);
	NK_LOVE_STYLE_PUSH("hover", item, &style->hover);
	NK_LOVE_STYLE_PUSH("active", item, &style->active);
	NK_LOVE_STYLE_PUSH("close button", button, &style->close_button);
	NK_LOVE_STYLE_PUSH("minimize button", button, &style->minimize_button);
	NK_LOVE_STYLE_PUSH("label normal", color, &style->label_normal);
	NK_LOVE_STYLE_PUSH("label hover", color, &style->label_hover);
	NK_LOVE_STYLE_PUSH("label active", color, &style->label_active);
	NK_LOVE_STYLE_PUSH("padding", vec2, &style->padding);
	NK_LOVE_STYLE_PUSH("label padding", vec2, &style->label_padding);
	NK_LOVE_STYLE_PUSH("spacing", vec2, &style->spacing);
}

static void nk_love_style_push_window(struct nk_style_window *style)
{
	nk_love_assert(lua_istable(L, -1), "%s: window style must be a table");
	NK_LOVE_STYLE_PUSH("header", window_header, &style->header);
	NK_LOVE_STYLE_PUSH("fixed background", item, &style->fixed_background);
	NK_LOVE_STYLE_PUSH("background", color, &style->background);
	NK_LOVE_STYLE_PUSH("border color", color, &style->border_color);
	NK_LOVE_STYLE_PUSH("popup border color", color, &style->popup_border_color);
	NK_LOVE_STYLE_PUSH("combo border color", color, &style->combo_border_color);
	NK_LOVE_STYLE_PUSH("contextual border color", color, &style->contextual_border_color);
	NK_LOVE_STYLE_PUSH("menu border color", color, &style->menu_border_color);
	NK_LOVE_STYLE_PUSH("group border color", color, &style->group_border_color);
	NK_LOVE_STYLE_PUSH("tooltip border color", color, &style->tooltip_border_color);
	NK_LOVE_STYLE_PUSH("scaler", item, &style->scaler);
	NK_LOVE_STYLE_PUSH("border", float, &style->border);
	NK_LOVE_STYLE_PUSH("combo border", float, &style->combo_border);
	NK_LOVE_STYLE_PUSH("contextual border", float, &style->contextual_border);
	NK_LOVE_STYLE_PUSH("menu border", float, &style->menu_border);
	NK_LOVE_STYLE_PUSH("group border", float, &style->group_border);
	NK_LOVE_STYLE_PUSH("tooltip border", float, &style->tooltip_border);
	NK_LOVE_STYLE_PUSH("popup border", float, &style->popup_border);
	NK_LOVE_STYLE_PUSH("rounding", float, &style->rounding);
	NK_LOVE_STYLE_PUSH("spacing", vec2, &style->spacing);
	NK_LOVE_STYLE_PUSH("scrollbar size", vec2, &style->scrollbar_size);
	NK_LOVE_STYLE_PUSH("min size", vec2, &style->min_size);
	NK_LOVE_STYLE_PUSH("padding", vec2, &style->padding);
	NK_LOVE_STYLE_PUSH("group padding", vec2, &style->group_padding);
	NK_LOVE_STYLE_PUSH("popup padding", vec2, &style->popup_padding);
	NK_LOVE_STYLE_PUSH("combo padding", vec2, &style->combo_padding);
	NK_LOVE_STYLE_PUSH("contextual padding", vec2, &style->contextual_padding);
	NK_LOVE_STYLE_PUSH("menu padding", vec2, &style->menu_padding);
	NK_LOVE_STYLE_PUSH("tooltip padding", vec2, &style->tooltip_padding);
}

static int nk_love_style_push(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	struct nk_love_context *ctx = nk_love_checkcontext(1);
	if (!lua_istable(L, 2))
		luaL_typerror(L, 2, "table");
	lua_newtable(L);
	lua_insert(L, 2);
	NK_LOVE_STYLE_PUSH("font", font, &ctx->nkctx->style.font);
	NK_LOVE_STYLE_PUSH("text", text, &ctx->nkctx->style.text);
	NK_LOVE_STYLE_PUSH("button", button, &ctx->nkctx->style.button);
	NK_LOVE_STYLE_PUSH("contextual button", button, &ctx->nkctx->style.contextual_button);
	NK_LOVE_STYLE_PUSH("menu button", button, &ctx->nkctx->style.menu_button);
	NK_LOVE_STYLE_PUSH("option", toggle, &ctx->nkctx->style.option);
	NK_LOVE_STYLE_PUSH("checkbox", toggle, &ctx->nkctx->style.checkbox);
	NK_LOVE_STYLE_PUSH("selectable", selectable, &ctx->nkctx->style.selectable);
	NK_LOVE_STYLE_PUSH("slider", slider, &ctx->nkctx->style.slider);
	NK_LOVE_STYLE_PUSH("progress", progress, &ctx->nkctx->style.progress);
	NK_LOVE_STYLE_PUSH("property", property, &ctx->nkctx->style.property);
	NK_LOVE_STYLE_PUSH("edit", edit, &ctx->nkctx->style.edit);
	NK_LOVE_STYLE_PUSH("chart", chart, &ctx->nkctx->style.chart);
	NK_LOVE_STYLE_PUSH("scrollh", scrollbar, &ctx->nkctx->style.scrollh);
	NK_LOVE_STYLE_PUSH("scrollv", scrollbar, &ctx->nkctx->style.scrollv);
	NK_LOVE_STYLE_PUSH("tab", tab, &ctx->nkctx->style.tab);
	NK_LOVE_STYLE_PUSH("combo", combo, &ctx->nkctx->style.combo);
	NK_LOVE_STYLE_PUSH("window", window, &ctx->nkctx->style.window);
	lua_pop(L, 1);
	lua_getfield(L, LUA_REGISTRYINDEX, "nuklear");
	lua_pushlightuserdata(L, ctx);
	lua_gettable(L, -2);
	lua_getfield(L, -1, "stack");
	size_t stack_size = lua_rawlen(L, -1);
	lua_pushvalue(L, 2);
	lua_rawseti(L, -2, stack_size + 1);
	return 0;
}

static int nk_love_style_pop(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *ctx = nk_love_checkcontext(1);
	lua_getfield(L, LUA_REGISTRYINDEX, "nuklear");
	lua_pushlightuserdata(L, ctx);
	lua_gettable(L, -2);
	lua_getfield(L, -1, "stack");
	size_t stack_size = lua_rawlen(L, -1);
	lua_rawgeti(L, -1, stack_size);
	lua_pushnil(L);
	lua_rawseti(L, -3, stack_size);
	stack_size = lua_rawlen(L, -1);
	size_t i;
	for (i = stack_size; i > 0; --i) {
		lua_rawgeti(L, -1, i);
		const char *type = lua_tostring(L, -1);
		if (!strcmp(type, "color")) {
			nk_style_pop_color(ctx->nkctx);
		} else if (!strcmp(type, "vec2")) {
			nk_style_pop_vec2(ctx->nkctx);
		} else if (!strcmp(type, "item")) {
			nk_style_pop_style_item(ctx->nkctx);
		} else if (!strcmp(type, "flags")) {
			nk_style_pop_flags(ctx->nkctx);
		} else if (!strcmp(type, "float")) {
			nk_style_pop_float(ctx->nkctx);
		} else if (!strcmp(type, "font")) {
			nk_style_pop_font(ctx->nkctx);
		} else {
			const char *msg = lua_pushfstring(L, "%%s: bad style item type '%s'", lua_tostring(L, -1));
			nk_love_assert(0, msg);
		}
		lua_pop(L, 1);
	}
	return 0;
}

static int nk_love_style(lua_State *L)
{
	nk_love_assert(lua_checkstack(L, 3), "%s: failed to allocate stack space");
	nk_love_assert_argc(lua_gettop(L) == 3);
	if (!lua_isfunction(L, -1))
		luaL_typerror(L, lua_gettop(L), "function");
	lua_pushvalue(L, 1);
	lua_insert(L, 2);
	lua_pushvalue(L, 1);
	lua_insert(L, 3);
	lua_insert(L, 2);
	lua_getfield(L, 1, "stylePush");
	lua_insert(L, 4);
	lua_call(L, 2, 0);
	lua_call(L, 1, 0);
	lua_getfield(L, 1, "stylePop");
	lua_insert(L, 1);
	lua_call(L, 1, 0);
	return 0;
}

/*
 * ===============================================================
 *
 *                        CUSTOM WIDGETS
 *
 * ===============================================================
 */

static int nk_love_widget_bounds(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	struct nk_rect bounds = nk_widget_bounds(context->nkctx);
	lua_pushnumber(L, bounds.x);
	lua_pushnumber(L, bounds.y);
	lua_pushnumber(L, bounds.w);
	lua_pushnumber(L, bounds.h);
	return 4;
}

static int nk_love_widget_position(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	struct nk_vec2 pos = nk_widget_position(context->nkctx);
	lua_pushnumber(L, pos.x);
	lua_pushnumber(L, pos.y);
	return 2;
}

static int nk_love_widget_size(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	struct nk_vec2 pos = nk_widget_size(context->nkctx);
	lua_pushnumber(L, pos.x);
	lua_pushnumber(L, pos.y);
	return 2;
}

static int nk_love_widget_width(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	float width = nk_widget_width(context->nkctx);
	lua_pushnumber(L, width);
	return 1;
}

static int nk_love_widget_height(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	float height = nk_widget_height(context->nkctx);
	lua_pushnumber(L, height);
	return 1;
}

static int nk_love_widget_is_hovered(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	int hovered = nk_widget_is_hovered(context->nkctx);
	lua_pushboolean(L, hovered);
	return 1;
}

static int nk_love_widget_has_mouse(lua_State *L, int down)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc >= 1 && argc <= 2);
	struct nk_love_context *context = nk_love_checkcontext(1);
	enum nk_buttons button = NK_BUTTON_LEFT;
	if (argc >= 2 && !lua_isnil(L, 2))
		button = nk_love_checkbutton(2);
	int ret = nk_widget_has_mouse_click_down(context->nkctx, button, down);
	lua_pushboolean(L, ret);
	return 1;
}

static int nk_love_widget_has_mouse_pressed(lua_State *L)
{
	return nk_love_widget_has_mouse(L, nk_true);
}

static int nk_love_widget_has_mouse_released(lua_State *L)
{
	return nk_love_widget_has_mouse(L, nk_false);
}

static int nk_love_widget_is_mouse(lua_State *L, int down)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc >= 1 && argc <= 2);
	struct nk_love_context *context = nk_love_checkcontext(1);
	enum nk_buttons button = NK_BUTTON_LEFT;
	if (argc >= 2 && !lua_isnil(L, 2))
		button = nk_love_checkbutton(2);
	struct nk_rect bounds = nk_widget_bounds(context->nkctx);
	int ret = nk_input_is_mouse_click_down_in_rect(&context->nkctx->input, button, bounds, down);
	lua_pushboolean(L, ret);
	return 1;
}

static int nk_love_widget_is_mouse_pressed(lua_State *L)
{
	return nk_love_widget_is_mouse(L, nk_true);
}

static int nk_love_widget_is_mouse_released(lua_State *L)
{
	return nk_love_widget_is_mouse(L, nk_false);
}

static int nk_love_spacing(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 2);
	struct nk_love_context *context = nk_love_checkcontext(1);
	int cols = luaL_checkinteger(L, 2);
	nk_spacing(context->nkctx, cols);
	return 0;
}

static int nk_love_line(lua_State *L)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc >= 5 && argc % 2 == 1);
	struct nk_love_context *context = nk_love_checkcontext(1);
	int i;
	for (i = 0; i < argc - 1; ++i) {
		nk_love_assert(lua_isnumber(L, i + 2), "%s: point coordinates should be numbers");
		points[i] = lua_tonumber(L, i + 2);
	}
	float line_thickness;
	struct nk_color color;
	nk_love_getGraphics(&line_thickness, &color);
	nk_stroke_polyline(&context->nkctx->current->buffer, points, (argc - 1) / 2, line_thickness, color);
	return 0;
}

static int nk_love_curve(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 9);
	struct nk_love_context *context = nk_love_checkcontext(1);
	float ax = luaL_checknumber(L, 2);
	float ay = luaL_checknumber(L, 3);
	float ctrl0x = luaL_checknumber(L, 4);
	float ctrl0y = luaL_checknumber(L, 5);
	float ctrl1x = luaL_checknumber(L, 6);
	float ctrl1y = luaL_checknumber(L, 7);
	float bx = luaL_checknumber(L, 8);
	float by = luaL_checknumber(L, 9);
	float line_thickness;
	struct nk_color color;
	nk_love_getGraphics(&line_thickness, &color);
	nk_stroke_curve(&context->nkctx->current->buffer, ax, ay, ctrl0x, ctrl0y, ctrl1x, ctrl1y, bx, by, line_thickness, color);
	return 0;
}

static int nk_love_polygon(lua_State *L)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc >= 8 && argc % 2 == 0);
	struct nk_love_context *context = nk_love_checkcontext(1);
	enum nk_love_draw_mode mode = nk_love_checkdraw(2);
	int i;
	for (i = 0; i < argc - 2; ++i) {
		nk_love_assert(lua_isnumber(L, i + 3), "%s: point coordinates should be numbers");
		points[i] = lua_tonumber(L, i + 3);
	}
	float line_thickness;
	struct nk_color color;
	nk_love_getGraphics(&line_thickness, &color);
	if (mode == NK_LOVE_FILL)
		nk_fill_polygon(&context->nkctx->current->buffer, points, (argc - 2) / 2, color);
	else if (mode == NK_LOVE_LINE)
		nk_stroke_polygon(&context->nkctx->current->buffer, points, (argc - 2) / 2, line_thickness, color);
	return 0;
}

static int nk_love_circle(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 5);
	struct nk_love_context *context = nk_love_checkcontext(1);
	enum nk_love_draw_mode mode = nk_love_checkdraw(2);
	float x = luaL_checknumber(L, 3);
	float y = luaL_checknumber(L, 4);
	float r = luaL_checknumber(L, 5);
	float line_thickness;
	struct nk_color color;
	nk_love_getGraphics(&line_thickness, &color);
	if (mode == NK_LOVE_FILL)
		nk_fill_circle(&context->nkctx->current->buffer, nk_rect(x - r, y - r, r * 2, r * 2), color);
	else if (mode == NK_LOVE_LINE)
		nk_stroke_circle(&context->nkctx->current->buffer, nk_rect(x - r, y - r, r * 2, r * 2), line_thickness, color);
	return 0;
}

static int nk_love_ellipse(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 6);
	struct nk_love_context *context = nk_love_checkcontext(1);
	enum nk_love_draw_mode mode = nk_love_checkdraw(2);
	float x = luaL_checknumber(L, 3);
	float y = luaL_checknumber(L, 4);
	float rx = luaL_checknumber(L, 5);
	float ry = luaL_checknumber(L, 6);
	float line_thickness;
	struct nk_color color;
	nk_love_getGraphics(&line_thickness, &color);
	if (mode == NK_LOVE_FILL)
		nk_fill_circle(&context->nkctx->current->buffer, nk_rect(x - rx, y - ry, rx * 2, ry * 2), color);
	else if (mode == NK_LOVE_LINE)
		nk_stroke_circle(&context->nkctx->current->buffer, nk_rect(x - rx, y - ry, rx * 2, ry * 2), line_thickness, color);
	return 0;
}

static int nk_love_arc(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 7);
	struct nk_love_context *context = nk_love_checkcontext(1);
	enum nk_love_draw_mode mode = nk_love_checkdraw(2);
	float cx = luaL_checknumber(L, 3);
	float cy = luaL_checknumber(L, 4);
	float r = luaL_checknumber(L, 5);
	float a0 = luaL_checknumber(L, 6);
	float a1 = luaL_checknumber(L, 7);
	float line_thickness;
	struct nk_color color;
	nk_love_getGraphics(&line_thickness, &color);
	if (mode == NK_LOVE_FILL)
		nk_fill_arc(&context->nkctx->current->buffer, cx, cy, r, a0, a1, color);
	else if (mode == NK_LOVE_LINE)
		nk_stroke_arc(&context->nkctx->current->buffer, cx, cy, r, a0, a1, line_thickness, color);
	return 0;
}

static int nk_love_rect_multi_color(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 9);
	struct nk_love_context *context = nk_love_checkcontext(1);
	float x = luaL_checknumber(L, 2);
	float y = luaL_checknumber(L, 3);
	float w = luaL_checknumber(L, 4);
	float h = luaL_checknumber(L, 5);
	struct nk_color topLeft = nk_love_checkcolor(6);
	struct nk_color topRight = nk_love_checkcolor(7);
	struct nk_color bottomLeft = nk_love_checkcolor(8);
	struct nk_color bottomRight = nk_love_checkcolor(9);
	nk_fill_rect_multi_color(&context->nkctx->current->buffer, nk_rect(x, y, w, h), topLeft, topRight, bottomLeft, bottomRight);
	return 0;
}

static int nk_love_push_scissor(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 5);
	struct nk_love_context *context = nk_love_checkcontext(1);
	float x = luaL_checknumber(L, 2);
	float y = luaL_checknumber(L, 3);
	float w = luaL_checknumber(L, 4);
	float h = luaL_checknumber(L, 5);
	nk_push_scissor(&context->nkctx->current->buffer, nk_rect(x, y, w, h));
	return 0;
}

static int nk_love_input_has_mouse(int down)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc == 6);
	struct nk_love_context *context = nk_love_checkcontext(1);
	enum nk_buttons button = nk_love_checkbutton(2);
	float x = luaL_checknumber(L, 3);
	float y = luaL_checknumber(L, 4);
	float w = luaL_checknumber(L, 5);
	float h = luaL_checknumber(L, 6);
	int ret = nk_input_has_mouse_click_down_in_rect(&context->nkctx->input, button, nk_rect(x, y, w, h), down);
	lua_pushboolean(L, ret);
	return 1;
}

static int nk_love_input_has_mouse_pressed(lua_State *L)
{
	return nk_love_input_has_mouse(nk_true);
}

static int nk_love_input_has_mouse_released(lua_State *L)
{
	return nk_love_input_has_mouse(nk_false);
}

static int nk_love_input_is_mouse(int down)
{
	int argc = lua_gettop(L);
	nk_love_assert_argc(argc == 6);
	struct nk_love_context *context = nk_love_checkcontext(1);
	enum nk_buttons button = nk_love_checkbutton(2);
	float x = luaL_checknumber(L, 3);
	float y = luaL_checknumber(L, 4);
	float w = luaL_checknumber(L, 5);
	float h = luaL_checknumber(L, 6);
	int ret = nk_input_is_mouse_click_down_in_rect(&context->nkctx->input, button, nk_rect(x, y, w, h), down);
	lua_pushboolean(L, ret);
	return 1;
}

static int nk_love_input_is_mouse_pressed(lua_State *L)
{
	return nk_love_input_is_mouse(nk_true);
}

static int nk_love_input_is_mouse_released(lua_State *L)
{
	return nk_love_input_is_mouse(nk_false);
}

static int nk_love_input_was_hovered(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 5);
	struct nk_love_context *context = nk_love_checkcontext(1);
	float x = luaL_checknumber(L, 2);
	float y = luaL_checknumber(L, 3);
	float w = luaL_checknumber(L, 4);
	float h = luaL_checknumber(L, 5);
	int was_hovered = nk_input_is_mouse_prev_hovering_rect(&context->nkctx->input, nk_rect(x, y, w, h));
	lua_pushboolean(L, was_hovered);
	return 1;
}

static int nk_love_input_is_hovered(lua_State *L)
{
	nk_love_assert_argc(lua_gettop(L) == 5);
	struct nk_love_context *context = nk_love_checkcontext(1);
	float x = luaL_checknumber(L, 2);
	float y = luaL_checknumber(L, 3);
	float w = luaL_checknumber(L, 4);
	float h = luaL_checknumber(L, 5);
	int is_hovered = nk_input_is_mouse_hovering_rect(&context->nkctx->input, nk_rect(x, y, w, h));
	lua_pushboolean(L, is_hovered);
	return 1;
}

static int nk_love_image_callback(lua_State *L)
{
	return 0;
}

static int nk_love_font_callback(lua_State *L)
{
	return 0;
}

/*
 * ===============================================================
 *
 *                          REGISTER
 *
 * ===============================================================
 */

#define NK_LOVE_REGISTER(name, func) \
	lua_pushcfunction(L, func); \
	lua_setfield(L, -2, name)


LUALIB_API int luaopen_nuklear(lua_State *lua_State)
{
	L = lua_State;

	//create global love state to get graphics(color and border thickness)
	//it is empty here
	lua_newtable(L); //1
	lua_setglobal(L, "love"); //0

	//create global nuklear context, which has image and font table.
	lua_newtable(L); //1
	lua_setfield(L, LUA_REGISTRYINDEX, "nuklear"); //0

	luaL_newmetatable(L, "metatable"); //1
	NK_LOVE_REGISTER("keypressed", nk_love_keypressed);
	NK_LOVE_REGISTER("keyreleased", nk_love_keyreleased);
	NK_LOVE_REGISTER("mousepressed", nk_love_mousepressed);
	NK_LOVE_REGISTER("mousereleased", nk_love_mousereleased);
	NK_LOVE_REGISTER("mousemoved", nk_love_mousemoved);
	NK_LOVE_REGISTER("textinput", nk_love_textinput);
	NK_LOVE_REGISTER("wheelmoved", nk_love_wheelmoved);

	/* NK_LOVE_REGISTER("draw", nk_love_draw); */

	NK_LOVE_REGISTER("frameBegin", nk_love_frame_begin);
	NK_LOVE_REGISTER("frameEnd", nk_love_frame_end);
	NK_LOVE_REGISTER("frame", nk_love_frame);

	NK_LOVE_REGISTER("rotate", nk_love_rotate);
	NK_LOVE_REGISTER("scale", nk_love_scale);
	NK_LOVE_REGISTER("shear", nk_love_shear);
	NK_LOVE_REGISTER("translate", nk_love_translate);

	NK_LOVE_REGISTER("windowBegin", nk_love_window_begin);
	NK_LOVE_REGISTER("windowEnd", nk_love_window_end);
	NK_LOVE_REGISTER("window", nk_love_window);
	NK_LOVE_REGISTER("windowGetBounds", nk_love_window_get_bounds);
	NK_LOVE_REGISTER("windowGetPosition", nk_love_window_get_position);
	NK_LOVE_REGISTER("windowGetSize", nk_love_window_get_size);
	NK_LOVE_REGISTER("windowGetScroll", nk_love_window_get_scroll);
	NK_LOVE_REGISTER("windowGetContentRegion", nk_love_window_get_content_region);
	NK_LOVE_REGISTER("windowHasFocus", nk_love_window_has_focus);
	NK_LOVE_REGISTER("windowIsCollapsed", nk_love_window_is_collapsed);
	NK_LOVE_REGISTER("windowIsClosed", nk_love_window_is_closed);
	NK_LOVE_REGISTER("windowIsHidden", nk_love_window_is_hidden);
	NK_LOVE_REGISTER("windowIsActive", nk_love_window_is_active);
	NK_LOVE_REGISTER("windowIsHovered", nk_love_window_is_hovered);
	NK_LOVE_REGISTER("windowIsAnyHovered", nk_love_window_is_any_hovered);
	NK_LOVE_REGISTER("itemIsAnyActive", nk_love_item_is_any_active);
	NK_LOVE_REGISTER("windowSetBounds", nk_love_window_set_bounds);
	NK_LOVE_REGISTER("windowSetPosition", nk_love_window_set_position);
	NK_LOVE_REGISTER("windowSetSize", nk_love_window_set_size);
	NK_LOVE_REGISTER("windowSetFocus", nk_love_window_set_focus);
	NK_LOVE_REGISTER("windowSetScroll", nk_love_window_set_scroll);
	NK_LOVE_REGISTER("windowClose", nk_love_window_close);
	NK_LOVE_REGISTER("windowCollapse", nk_love_window_collapse);
	NK_LOVE_REGISTER("windowExpand", nk_love_window_expand);
	NK_LOVE_REGISTER("windowShow", nk_love_window_show);
	NK_LOVE_REGISTER("windowHide", nk_love_window_hide);

	NK_LOVE_REGISTER("layoutRow", nk_love_layout_row);
	NK_LOVE_REGISTER("layoutRowBegin", nk_love_layout_row_begin);
	NK_LOVE_REGISTER("layoutRowPush", nk_love_layout_row_push);
	NK_LOVE_REGISTER("layoutRowEnd", nk_love_layout_row_end);
	NK_LOVE_REGISTER("layoutTemplateBegin", nk_love_layout_template_begin);
	NK_LOVE_REGISTER("layoutTemplatePush", nk_love_layout_template_push);
	NK_LOVE_REGISTER("layoutTemplateEnd", nk_love_layout_template_end);
	NK_LOVE_REGISTER("layoutTemplate", nk_love_layout_template);
	NK_LOVE_REGISTER("layoutSpaceBegin", nk_love_layout_space_begin);
	NK_LOVE_REGISTER("layoutSpacePush", nk_love_layout_space_push);
	NK_LOVE_REGISTER("layoutSpaceEnd", nk_love_layout_space_end);
	NK_LOVE_REGISTER("layoutSpace", nk_love_layout_space);
	NK_LOVE_REGISTER("layoutSpaceBounds", nk_love_layout_space_bounds);
	NK_LOVE_REGISTER("layoutSpaceToScreen", nk_love_layout_space_to_screen);
	NK_LOVE_REGISTER("layoutSpaceToLocal", nk_love_layout_space_to_local);
	NK_LOVE_REGISTER("layoutSpaceRectToScreen", nk_love_layout_space_rect_to_screen);
	NK_LOVE_REGISTER("layoutSpaceRectToLocal", nk_love_layout_space_rect_to_local);
	NK_LOVE_REGISTER("layoutRatioFromPixel", nk_love_layout_ratio_from_pixel);

	NK_LOVE_REGISTER("groupBegin", nk_love_group_begin);
	NK_LOVE_REGISTER("groupEnd", nk_love_group_end);
	NK_LOVE_REGISTER("group", nk_love_group);
	NK_LOVE_REGISTER("groupGetScroll", nk_love_group_get_scroll);
	NK_LOVE_REGISTER("groupSetScroll", nk_love_group_set_scroll);

	NK_LOVE_REGISTER("treePush", nk_love_tree_push);
	NK_LOVE_REGISTER("treePop", nk_love_tree_pop);
	NK_LOVE_REGISTER("tree", nk_love_tree);

	NK_LOVE_REGISTER("treeStatePush", nk_love_tree_state_push);
	NK_LOVE_REGISTER("treeStatePop", nk_love_tree_state_pop);
	NK_LOVE_REGISTER("treeState", nk_love_tree_state);

	NK_LOVE_REGISTER("label", nk_love_label);
	NK_LOVE_REGISTER("image", nk_love_image);
	NK_LOVE_REGISTER("button", nk_love_button);
	NK_LOVE_REGISTER("buttonSetBehavior", nk_love_button_set_behavior);
	NK_LOVE_REGISTER("buttonPushBehavior", nk_love_button_push_behavior);
	NK_LOVE_REGISTER("buttonPopBehavior", nk_love_button_pop_behavior);
	NK_LOVE_REGISTER("checkbox", nk_love_checkbox);
	NK_LOVE_REGISTER("radio", nk_love_radio);
	NK_LOVE_REGISTER("selectable", nk_love_selectable);
	NK_LOVE_REGISTER("slider", nk_love_slider);
	NK_LOVE_REGISTER("progress", nk_love_progress);
	NK_LOVE_REGISTER("colorPicker", nk_love_color_picker);
	NK_LOVE_REGISTER("property", nk_love_property);
	NK_LOVE_REGISTER("edit", nk_love_edit);
	NK_LOVE_REGISTER("editFocus", nk_love_edit_focus);
	NK_LOVE_REGISTER("editUnfocus", nk_love_edit_unfocus);
	NK_LOVE_REGISTER("popupBegin", nk_love_popup_begin);
	NK_LOVE_REGISTER("popupClose", nk_love_popup_close);
	NK_LOVE_REGISTER("popupEnd", nk_love_popup_end);
	NK_LOVE_REGISTER("popup", nk_love_popup);
	NK_LOVE_REGISTER("popupGetScroll", nk_love_popup_get_scroll);
	NK_LOVE_REGISTER("popupSetScroll", nk_love_popup_set_scroll);
	NK_LOVE_REGISTER("combobox", nk_love_combobox);
	NK_LOVE_REGISTER("comboboxBegin", nk_love_combobox_begin);
	NK_LOVE_REGISTER("comboboxItem", nk_love_combobox_item);
	NK_LOVE_REGISTER("comboboxClose", nk_love_combobox_close);
	NK_LOVE_REGISTER("comboboxEnd", nk_love_combobox_end);
	NK_LOVE_REGISTER("contextualBegin", nk_love_contextual_begin);
	NK_LOVE_REGISTER("contextualItem", nk_love_contextual_item);
	NK_LOVE_REGISTER("contextualClose", nk_love_contextual_close);
	NK_LOVE_REGISTER("contextualEnd", nk_love_contextual_end);
	NK_LOVE_REGISTER("contextual", nk_love_contextual);
	NK_LOVE_REGISTER("tooltip", nk_love_tooltip);
	NK_LOVE_REGISTER("tooltipBegin", nk_love_tooltip_begin);
	NK_LOVE_REGISTER("tooltipEnd", nk_love_tooltip_end);
	NK_LOVE_REGISTER("menubarBegin", nk_love_menubar_begin);
	NK_LOVE_REGISTER("menubarEnd", nk_love_menubar_end);
	NK_LOVE_REGISTER("menubar", nk_love_menubar);
	NK_LOVE_REGISTER("menuBegin", nk_love_menu_begin);
	NK_LOVE_REGISTER("menuItem", nk_love_menu_item);
	NK_LOVE_REGISTER("menuClose", nk_love_menu_close);
	NK_LOVE_REGISTER("menuEnd", nk_love_menu_end);
	NK_LOVE_REGISTER("menu", nk_love_menu);

	NK_LOVE_REGISTER("styleDefault", nk_love_style_default);
	NK_LOVE_REGISTER("styleLoadColors", nk_love_style_load_colors);
	NK_LOVE_REGISTER("styleSetFont", nk_love_style_set_font);
	NK_LOVE_REGISTER("stylePush", nk_love_style_push);
	NK_LOVE_REGISTER("stylePop", nk_love_style_pop);
	NK_LOVE_REGISTER("style", nk_love_style);

	NK_LOVE_REGISTER("widgetBounds", nk_love_widget_bounds);
	NK_LOVE_REGISTER("widgetPosition", nk_love_widget_position);
	NK_LOVE_REGISTER("widgetSize", nk_love_widget_size);
	NK_LOVE_REGISTER("widgetWidth", nk_love_widget_width);
	NK_LOVE_REGISTER("widgetHeight", nk_love_widget_height);
	NK_LOVE_REGISTER("widgetIsHovered", nk_love_widget_is_hovered);
	NK_LOVE_REGISTER("widgetHasMousePressed", nk_love_widget_has_mouse_pressed);
	NK_LOVE_REGISTER("widgetHasMouseReleased", nk_love_widget_has_mouse_released);
	NK_LOVE_REGISTER("widgetIsMousePressed", nk_love_widget_is_mouse_pressed);
	NK_LOVE_REGISTER("widgetIsMouseReleased", nk_love_widget_is_mouse_released);
	NK_LOVE_REGISTER("spacing", nk_love_spacing);

	NK_LOVE_REGISTER("line", nk_love_line);
	NK_LOVE_REGISTER("curve", nk_love_curve);
	NK_LOVE_REGISTER("polygon", nk_love_polygon);
	NK_LOVE_REGISTER("circle", nk_love_circle);
	NK_LOVE_REGISTER("ellipse", nk_love_ellipse);
	NK_LOVE_REGISTER("arc", nk_love_arc);
	NK_LOVE_REGISTER("rectMultiColor", nk_love_rect_multi_color);
	NK_LOVE_REGISTER("scissor", nk_love_push_scissor);
	/* image */
	NK_LOVE_REGISTER("text", nk_love_text);

	NK_LOVE_REGISTER("inputHasMousePressed", nk_love_input_has_mouse_pressed);
	NK_LOVE_REGISTER("inputHasMouseReleased", nk_love_input_has_mouse_released);
	NK_LOVE_REGISTER("inputIsMousePressed", nk_love_input_is_mouse_pressed);
	NK_LOVE_REGISTER("inputIsMouseReleased", nk_love_input_is_mouse_released);
	NK_LOVE_REGISTER("inputWasHovered", nk_love_input_was_hovered);
	NK_LOVE_REGISTER("inputIsHovered", nk_love_input_is_hovered);

	NK_LOVE_REGISTER("rgba", nk_love_color_rgba);
	NK_LOVE_REGISTER("hsva", nk_love_color_hsva);
	NK_LOVE_REGISTER("rgbaString", nk_love_color_parse_rgba);
	NK_LOVE_REGISTER("lineWidth", nk_love_configureLineWidth);

	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
	static const struct luaL_Reg regs[] = {
		{"imageCallback", nk_love_image_callback},
		{"fontCallback", nk_love_font_callback},
	};
	luaL_newlib(L, regs);
	/* lua_newtable(L); */
	/* /\* NK_LOVE_REGISTER("newUI", nk_love_new_ui); *\/ */

	return 1;
}

#undef NK_LOVE_REGISTER
