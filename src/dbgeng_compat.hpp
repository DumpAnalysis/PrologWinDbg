#pragma once
#include "win_util.hpp"
// MinGW's bundled DbgEng header stops before the wide-character interfaces.
// Cross builds use Microsoft's current, unmodified SDK header. These SAL
// annotations are missing in MinGW; suppress them only around this include.
#ifdef __MINGW32__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wpedantic"
#ifndef __in
#define __in
#define PD_UNDEF_IN
#endif
#ifndef __out
#define __out
#define PD_UNDEF_OUT
#endif
#ifndef _Analysis_noreturn_
#define _Analysis_noreturn_
#define PD_UNDEF_NORETURN
#endif
#endif
#include <dbgeng.h>
#ifdef PD_UNDEF_IN
#undef __in
#undef PD_UNDEF_IN
#endif
#ifdef PD_UNDEF_OUT
#undef __out
#undef PD_UNDEF_OUT
#endif
#ifdef PD_UNDEF_NORETURN
#undef _Analysis_noreturn_
#undef PD_UNDEF_NORETURN
#endif
#ifdef __MINGW32__
#pragma GCC diagnostic pop
#endif
