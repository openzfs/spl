/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef	_SPL_ISA_DEFS_H
#define	_SPL_ISA_DEFS_H

/*****************************************************************************\
 *  If the compiler is already defining _LP64 or _ILP32 (but not both), we
 *  should trust the compiler.
\*****************************************************************************/

#if !(defined(_ILP32) && !defined(_LP64)) \
 && !(!defined(_ILP32) && defined(_LP64))

#if defined(_ILP32) && defined(_LP64)
#error "Both _ILP32 and _LP64 are defined"
#endif

#if defined(__arch64__)
#define _LP64
#elif defined(__arch32__)
#define _ILP32
#endif

#if !defined(_ILP32) && !defined(_LP64)
#error "Neither _ILP32 or _LP64 are defined"
#endif
#endif

/*****************************************************************************\
 *  If the compiler is already defining *BIG_ENDIAN or *LITTLE_ENDIAN
 *  (but not both), we should trust the compiler.
\*****************************************************************************/

#if !((defined(__LITTLE_ENDIAN) && !defined(__BIG_ENDIAN)) \
 && !(!defined(__LITTLE_ENDIAN) && defined(__BIG_ENDIAN))) \
 || !((defined(_LITTLE_ENDIAN) && !defined(_BIG_ENDIAN)) \
 && !(!defined(_LITTLE_ENDIAN) && defined(_BIG_ENDIAN)))

#if defined(_LITTLE_ENDIAN) && defined(_BIG_ENDIAN)
#error "Both _LITTLE_ENDIAN and _BIG_ENDIAN are defined"
#endif

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN
#endif
#ifndef _BIG_ENDIAN
#define _BIG_ENDIAN
#endif
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN
#endif
#ifndef _LITTLE_ENDIAN
#define _LITTLE_ENDIAN
#endif
#endif

#if !defined(_LITTLE_ENDIAN) && !defined(_BIG_ENDIAN)
#error "Neither _LITTLE_ENDIAN nor _BIG_ENDIAN are defined"
#endif
#endif

/* Define _SUNOS_VTOC_16 only on archs that need it... */

#if (defined(__sparc) || defined(__sparc__)) \
 || (defined(__mips__))
#define _SUNOS_VTOC_16
#endif

#endif	/* _SPL_ISA_DEFS_H */
