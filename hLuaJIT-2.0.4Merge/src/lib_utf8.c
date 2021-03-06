/*
** $Id: lutf8lib.c,v 1.13 2014/11/02 19:19:04 roberto Exp $
** Standard library for UTF-8 manipulation
** See Copyright Notice in lua.h
*/
#define _CRT_SECURE_NO_WARNINGS


#define lutf8lib_c
#define LUA_LIB



#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"

#include <windows.h>

#define MAXUNICODE	0x10FFFF

#define iscont(p)	((*(p) & 0xC0) == 0x80)


/* from strlib */
/* translate a relative string position: negative means back from end */
static lua_Integer u_posrelat(lua_Integer pos, size_t len) {
	if (pos >= 0) return pos;
	else if (0u - (size_t)pos > len) return 0;
	else return (lua_Integer)len + pos + 1;
}


/*
** Decode one UTF-8 sequence, returning NULL if byte sequence is invalid.
*/
static const char *utf8_decode(const char *o, int *val) {
	static unsigned int limits[] = { 0xFF, 0x7F, 0x7FF, 0xFFFF };
	const unsigned char *s = (const unsigned char *)o;
	unsigned int c = s[0];
	unsigned int res = 0;  /* final result */
	if (c < 0x80)  /* ascii? */
		res = c;
	else {
		int count = 0;  /* to count number of continuation bytes */
		while (c & 0x40) {  /* still have continuation bytes? */
			int cc = s[++count];  /* read next byte */
			if ((cc & 0xC0) != 0x80)  /* not a continuation byte? */
				return NULL;  /* invalid byte sequence */
			res = (res << 6) | (cc & 0x3F);  /* add lower 6 bits from cont. byte */
			c <<= 1;  /* to test next bit */
		}
		res |= ((c & 0x7F) << (count * 5));  /* add first byte */
		if (count > 3 || res > MAXUNICODE || res <= limits[count])
			return NULL;  /* invalid byte sequence */
		s += count;  /* skip continuation bytes read */
	}
	if (val) *val = res;
	return (const char *)s + 1;  /* +1 to include first byte */
}


/*
** utf8len(s [, i [, j]]) --> number of characters that start in the
** range [i,j], or nil + current position if 's' is not well formed in
** that interval
*/
static int utflen(lua_State *L) {
	int n = 0;
	size_t len;
	const char *s = luaL_checklstring(L, 1, &len);
	lua_Integer posi = u_posrelat(luaL_optinteger(L, 2, 1), len);
	lua_Integer posj = u_posrelat(luaL_optinteger(L, 3, -1), len);
	luaL_argcheck(L, 1 <= posi && --posi <= (lua_Integer)len, 2,
		"initial position out of string");
	luaL_argcheck(L, --posj < (lua_Integer)len, 3,
		"final position out of string");
	while (posi <= posj) {
		const char *s1 = utf8_decode(s + posi, NULL);
		if (s1 == NULL) {  /* conversion error? */
			lua_pushnil(L);  /* return nil ... */
			lua_pushinteger(L, posi + 1);  /* ... and current position */
			return 2;
		}
		posi = s1 - s;
		n++;
	}
	lua_pushinteger(L, n);
	return 1;
}


/*
** codepoint(s, [i, [j]])  -> returns codepoints for all characters
** that start in the range [i,j]
*/
static int codepoint(lua_State *L) {
	size_t len;
	const char *s = luaL_checklstring(L, 1, &len);
	lua_Integer posi = u_posrelat(luaL_optinteger(L, 2, 1), len);
	lua_Integer pose = u_posrelat(luaL_optinteger(L, 3, posi), len);
	int n;
	const char *se;
	luaL_argcheck(L, posi >= 1, 2, "out of range");
	luaL_argcheck(L, pose <= (lua_Integer)len, 3, "out of range");
	if (posi > pose) return 0;  /* empty interval; return no values */
	n = (int)(pose - posi + 1);
	if (posi + n <= pose)  /* (lua_Integer -> int) overflow? */
		return luaL_error(L, "string slice too long");
	luaL_checkstack(L, n, "string slice too long");
	n = 0;
	se = s + pose;
	for (s += posi - 1; s < se;) {
		int code;
		s = utf8_decode(s, &code);
		if (s == NULL)
			return luaL_error(L, "invalid UTF-8 code");
		lua_pushinteger(L, code);
		n++;
	}
	return n;
}


static void pushutfchar(lua_State *L, int arg) {
	lua_Integer code = luaL_checkinteger(L, arg);
	luaL_argcheck(L, 0 <= code && code <= MAXUNICODE, arg, "value out of range");
	lua_pushfstring(L, "%U", (long)code);
}


/*
** utfchar(n1, n2, ...)  -> char(n1)..char(n2)...
*/
static int utfchar(lua_State *L) {
	int n = lua_gettop(L);  /* number of arguments */
	if (n == 1)  /* optimize common case of single char */
		pushutfchar(L, 1);
	else {
		int i;
		luaL_Buffer b;
		luaL_buffinit(L, &b);
		for (i = 1; i <= n; i++) {
			pushutfchar(L, i);
			luaL_addvalue(&b);
		}
		luaL_pushresult(&b);
	}
	return 1;
}


/*
** offset(s, n, [i])  -> index where n-th character counting from
**   position 'i' starts; 0 means character at 'i'.
*/
static int byteoffset(lua_State *L) {
	size_t len;
	const char *s = luaL_checklstring(L, 1, &len);
	lua_Integer n = luaL_checkinteger(L, 2);
	lua_Integer posi = (n >= 0) ? 1 : len + 1;
	posi = u_posrelat(luaL_optinteger(L, 3, posi), len);
	luaL_argcheck(L, 1 <= posi && --posi <= (lua_Integer)len, 3,
		"position out of range");
	if (n == 0) {
		/* find beginning of current byte sequence */
		while (posi > 0 && iscont(s + posi)) posi--;
	}
	else {
		if (iscont(s + posi))
			luaL_error(L, "initial position is a continuation byte");
		if (n < 0) {
			while (n < 0 && posi > 0) {  /* move back */
				do {  /* find beginning of previous character */
					posi--;
				} while (posi > 0 && iscont(s + posi));
				n++;
			}
		}
		else {
			n--;  /* do not move for 1st character */
			while (n > 0 && posi < (lua_Integer)len) {
				do {  /* find beginning of next character */
					posi++;
				} while (iscont(s + posi));  /* (cannot pass final '\0') */
				n--;
			}
		}
	}
	if (n == 0)  /* did it find given character? */
		lua_pushinteger(L, posi + 1);
	else  /* no such character */
		lua_pushnil(L);
	return 1;
}


static int iter_aux(lua_State *L) {
	size_t len;
	const char *s = luaL_checklstring(L, 1, &len);
	lua_Integer n = lua_tointeger(L, 2) - 1;
	if (n < 0)  /* first iteration? */
		n = 0;  /* start from here */
	else if (n < (lua_Integer)len) {
		n++;  /* skip current byte */
		while (iscont(s + n)) n++;  /* and its continuations */
	}
	if (n >= (lua_Integer)len)
		return 0;  /* no more codepoints */
	else {
		int code;
		const char *next = utf8_decode(s + n, &code);
		if (next == NULL || iscont(next))
			return luaL_error(L, "invalid UTF-8 code");
		lua_pushinteger(L, n + 1);
		lua_pushinteger(L, code);
		return 2;
	}
}


static int iter_codes(lua_State *L) {
	luaL_checkstring(L, 1);
	lua_pushcfunction(L, iter_aux);
	lua_pushvalue(L, 1);
	lua_pushinteger(L, 0);
	return 3;
}

// utf8??cp932 20150511 ??????????????
BOOL ConvUtf8toSJis(const BYTE* pSource, BYTE* pDist, int* pSize)
{
	BYTE* buffUtf16;
	int nSizeSJis;
	//UTF-8????UTF-16??????
	int nSize;
	BYTE* buffSJis;

	*pSize = 0;
	nSize = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)pSource, -1, NULL, 0);

	buffUtf16 = malloc(nSize * 2 + 2);
	MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)pSource, -1, (LPWSTR)buffUtf16, nSize);

	//UTF-16????Shift-JIS??????
	nSizeSJis = WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)buffUtf16, -1, NULL, 0, NULL, NULL);
	if (!pDist) {
		*pSize = nSizeSJis;
		free(buffUtf16);
		return TRUE;
	}

	buffSJis = malloc(nSizeSJis * 2);
	ZeroMemory(buffSJis, nSizeSJis * 2);
	WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)buffUtf16, -1, (LPSTR)buffSJis, nSizeSJis, NULL, NULL);

	*pSize = lstrlen((char*)buffSJis);
	memcpy(pDist, buffSJis, *pSize);

	free(buffUtf16);
	free(buffSJis);

	return TRUE;
}

static int str_utf8_to_cp932(lua_State *L) {
	size_t l;
	luaL_Buffer b;
	BYTE* pDist;
	int nSize;


	const char *s = luaL_checklstring(L, 1, &l);
	const char *ename = luaL_checklstring(L, 2, &l);

	// ???Q??????"cp932"????utf8??cp932
	if (strcmp((const char *)_strlwr((char *)ename), "cp932") == 0) {
		luaL_buffinit(L, &b);

		// ????????cp932??utf8????????
		nSize = 0;
		// ?P???????????T?C?Y??????????
		ConvUtf8toSJis(s, NULL, &nSize);
		// ?T?C?Y?????????????m??
		pDist = malloc(nSize + 1);
		// 0???S???N???A?????c
		ZeroMemory(pDist, nSize + 1);
		// ???x??pDist???????????????????i?[
		ConvUtf8toSJis(s, pDist, &nSize);

		// ???????????????????v?b?V??
		luaL_addstring(&b, pDist);
		luaL_pushresult(&b);

		// ????
		free(pDist);
	}
	else {
		luaL_buffinit(L, &b);
		// ???????????????????v?b?V??
		luaL_addstring(&b, s);
		luaL_pushresult(&b);
	}
	return 1;
}

/* pattern to match a single UTF-8 character */
#define UTF8PATT	"[\0-\x7F\xC2-\xF4][\x80-\xBF]*"


static const luaL_Reg utf8lib[] = {
	{ "offset", byteoffset },
	{ "codepoint", codepoint },
	{ "char", utfchar },
	{ "len", utflen },
	{ "codes", iter_codes },
	{ "encode", str_utf8_to_cp932 },
	/* placeholders */
	{ "charpattern", NULL },
	{ NULL, NULL }
};


LUALIB_API int luaopen_utf8(lua_State *L) {
	luaL_register(L, LUA_UTF8LIBNAME, utf8lib);
	lua_pushliteral(L, UTF8PATT);
	lua_setfield(L, -2, "charpattern");
	return 1;
}


