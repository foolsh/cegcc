// -*- C++ -*- forwarding header.

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005
// Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

//
// ISO C++ 14882: 21.4
//

/** @file cwchar
 *  This is a Standard C++ Library file.  You should @c #include this file
 *  in your programs, rather than any of the "*.h" implementation files.
 *
 *  This is the C++ version of the Standard C Library header @c wchar.h,
 *  and its contents are (mostly) the same as that header, but are all
 *  contained in the namespace @c std (except for names which are defined
 *  as macros in C).
 */

#ifndef _GLIBCXX_CWCHAR
#define _GLIBCXX_CWCHAR 1

#pragma GCC system_header

#include <bits/c++config.h>
#include <cstddef>
#include <ctime>

#if _GLIBCXX_HAVE_WCHAR_H
#include <wchar.h>
#endif

// Need to do a bit of trickery here with mbstate_t as char_traits
// assumes it is in wchar.h, regardless of wchar_t specializations.
#ifndef _GLIBCXX_HAVE_MBSTATE_T
extern "C"
{
  typedef struct
  {
    int __fill[6];
  } mbstate_t;
}
#endif

namespace std
{
  using ::mbstate_t;
}

// Get rid of those macros defined in <wchar.h> in lieu of real functions.
#undef btowc
#undef fgetwc
#undef fgetws
#undef fputwc
#undef fputws
#undef fwide
#undef fwprintf
#undef fwscanf
#undef getwc
#undef getwchar
#undef mbrlen
#undef mbrtowc
#undef mbsinit
#undef mbsrtowcs
#undef putwc
#undef putwchar
#undef swprintf
#undef swscanf
#undef ungetwc
#undef vfwprintf
#if _GLIBCXX_HAVE_VFWSCANF
# undef vfwscanf
#endif
#undef vswprintf
#if _GLIBCXX_HAVE_VSWSCANF
# undef vswscanf
#endif
#undef vwprintf
#if _GLIBCXX_HAVE_VWSCANF
# undef vwscanf
#endif
#undef wcrtomb
#undef wcscat
#undef wcschr
#undef wcscmp
#undef wcscoll
#undef wcscpy
#undef wcscspn
#undef wcsftime
#undef wcslen
#undef wcsncat
#undef wcsncmp
#undef wcsncpy
#undef wcspbrk
#undef wcsrchr
#undef wcsrtombs
#undef wcsspn
#undef wcsstr
#undef wcstod
#if _GLIBCXX_HAVE_WCSTOF
# undef wcstof
#endif
#undef wcstok
#undef wcstol
#undef wcstoul
#undef wcsxfrm
#undef wctob
#undef wmemchr
#undef wmemcmp
#undef wmemcpy
#undef wmemmove
#undef wmemset
#undef wprintf
#undef wscanf

#if _GLIBCXX_USE_WCHAR_T
namespace std
{
  using ::wint_t;

  using ::btowc;
  using ::fgetwc;
  using ::fgetws;
  using ::fputwc;
  using ::fputws;
  using ::fwide;
  using ::fwprintf;
  using ::fwscanf;
  using ::getwc;
  using ::getwchar;
  using ::mbrlen;
  using ::mbrtowc;
  using ::mbsinit;
  using ::mbsrtowcs;
  using ::putwc;
  using ::putwchar;
  using ::swprintf;
  using ::swscanf;
  using ::ungetwc;
  using ::vfwprintf;
#if _GLIBCXX_HAVE_VFWSCANF
  using ::vfwscanf;
#endif
  using ::vswprintf;
#if _GLIBCXX_HAVE_VSWSCANF
  using ::vswscanf;
#endif
  using ::vwprintf;
#if _GLIBCXX_HAVE_VWSCANF
  using ::vwscanf;
#endif
  using ::wcrtomb;
  using ::wcscat;
  using ::wcscmp;
#if _GLIBCXX_HAVE_WCSCOLL
  using ::wcscoll;
#endif
  using ::wcscpy;
  using ::wcscspn;
  using ::wcsftime;
  using ::wcslen;
  using ::wcsncat;
  using ::wcsncmp;
  using ::wcsncpy;
  using ::wcsrtombs;
  using ::wcsspn;
  using ::wcstod;
#if _GLIBCXX_HAVE_WCSTOF
  using ::wcstof;
#endif
  using ::wcstok;
  using ::wcstol;
  using ::wcstoul;
#if _GLIBCXX_HAVE_WCSXFRM
  using ::wcsxfrm;
#endif
  using ::wctob;
  using ::wmemcmp;
  using ::wmemcpy;
  using ::wmemmove;
  using ::wmemset;
  using ::wprintf;
  using ::wscanf;

  using ::wcschr;

  inline wchar_t*
  wcschr(wchar_t* __p, wchar_t __c)
  { return wcschr(const_cast<const wchar_t*>(__p), __c); }

  using ::wcspbrk;

  inline wchar_t*
  wcspbrk(wchar_t* __s1, const wchar_t* __s2)
  { return wcspbrk(const_cast<const wchar_t*>(__s1), __s2); }

  using ::wcsrchr;

  inline wchar_t*
  wcsrchr(wchar_t* __p, wchar_t __c)
  { return wcsrchr(const_cast<const wchar_t*>(__p), __c); }

  using ::wcsstr;

  inline wchar_t*
  wcsstr(wchar_t* __s1, const wchar_t* __s2)
  { return wcsstr(const_cast<const wchar_t*>(__s1), __s2); }

  using ::wmemchr;

  inline wchar_t*
  wmemchr(wchar_t* __p, wchar_t __c, size_t __n)
  { return wmemchr(const_cast<const wchar_t*>(__p), __c, __n); }
}

#if _GLIBCXX_USE_C99

#undef wcstold
#undef wcstoll
#undef wcstoull

namespace __gnu_cxx
{
#if _GLIBCXX_USE_C99_CHECK || _GLIBCXX_USE_C99_DYNAMIC
  extern "C" long double
    (wcstold)(const wchar_t * restrict, wchar_t ** restrict);
#endif
#if !_GLIBCXX_USE_C99_DYNAMIC
  using ::wcstold;
#endif
#if _GLIBCXX_USE_C99_LONG_LONG_CHECK || _GLIBCXX_USE_C99_LONG_LONG_DYNAMIC
  extern "C" long long int
    (wcstoll)(const wchar_t * restrict, wchar_t ** restrict, int);
  extern "C" unsigned long long int
    (wcstoull)(const wchar_t * restrict, wchar_t ** restrict, int);
#endif
#if !_GLIBCXX_USE_C99_LONG_LONG_DYNAMIC
  using ::wcstoll;
  using ::wcstoull;
#endif
}

namespace std
{
  using __gnu_cxx::wcstold;
  using __gnu_cxx::wcstoll;
  using __gnu_cxx::wcstoull;
}
#endif

#endif //_GLIBCXX_USE_WCHAR_T

#endif
