/*
 * Copyright (c) 1999 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Portions Copyright (c) 1999 Apple Computer, Inc.  All Rights
 * Reserved.  This file contains Original Code and/or Modifications of
 * Original Code as defined in and that are subject to the Apple Public
 * Source License Version 1.1 (the "License").  You may not use this file
 * except in compliance with the License.  Please obtain a copy of the
 * License at http://www.apple.com/publicsource and read it before using
 * this file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON- INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

/* Copyright (c) 1992 NeXT Computer, Inc.  All rights reserved.
 *
 *      File:   libc/i386/ansi/strchr.c
 *      Author: Bruce Martin, NeXT Computer, Inc.
 *
 *      This file contains machine dependent code for the strchr function
 *      on NeXT i386-based products.  Currently tuned for the i486.
 *
 * HISTORY
 * 14-Aug-92  Bruce Martin (Bruce_Martin@NeXT.COM)
 *	Created.
 */


char *strchr(char *str, int ch)
{
    do {
	if (str[0] == ch)
	    return(str);
    } while (*str++);
    return ((char *) 0);
}
