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

/*	hfs_dbg.h
 *
 *	(c) 1997 Apple Computer, Inc.  All Rights Reserved
 *
 *	hfs_dbg.h -- debugging macros for HFS file system.
 *
 *	HISTORY
 *	10-Nov-1998 Pat Dirks		Cleaned up definition of DBG_ASSERT to handle embedded '%' correctly.
 *	28-Apr-1998	Scott Roberts	Reorganized and added DEBUG_STAGE
 *	17-Nov-1997	Pat Dirks		Pat Dirks at Apple Computer
 *								Derived from old hfs version.
 */

#ifdef KERNEL
#include	<libkern/libkern.h>
#else /* KERNEL */
#include	<bsd/stdio.h>
#endif /* KERNEL */

extern void call_kdp(void);
extern int lbolt;

/* Define the debugging stage...
		4 -> Do all, aggresive, call_kdp
		3 -> debug asserts and debug err, panic instead of call_kdp
		2 -> debug error, no kdb
		1 -> very little, panic only
*/
#if DIAGNOSTIC
  /* OK, we're doing a debug build; DIAGNOSTIC=1 */
#else
  /* This is a debug build (DIAGNOSTIC is undefined or DIAGNOSTIC=0); make sure DIAGNOSTIC=0 */
  #ifdef DIAGNOSTIC
    #warning DIAGNOSTIC defined but not set!
  #endif
  /* XXX PPD Is this just totally superfluous, or even dangerous? #if DIAGNOSTIC already fails, and now #ifdef DIAGNOSTIC will fire... */
  #define DIAGNOSTIC 0
#endif

#if DIAGNOSTIC
	#define DEBUG_STAGE 3
#else
	#define DEBUG_STAGE 1
#endif /* KERNEL */

#ifdef KERNEL
  #define PRINTIT kprintf
#else /* KERNEL */
  #define PRINTIT printf
#endif /* KERNEL */

#if (DEBUG_STAGE > 2)
#define DEBUG_BREAK call_kdp();
#else
#define DEBUG_BREAK
#endif

#if (DEBUG_STAGE == 4)
    #define DEBUG_BREAK_MSG(PRINTF_ARGS) { PRINTIT PRINTF_ARGS; DEBUG_BREAK };
#elif (DEBUG_STAGE == 3)
    #define DEBUG_BREAK_MSG(PRINTF_ARGS) { panic PRINTF_ARGS;};
#else
    #define DEBUG_BREAK_MSG(PRINTF_ARGS) { PRINTIT PRINTF_ARGS; };
#endif


//#define PRINT_DELAY (void) tsleep((caddr_t)&lbolt, PPAUSE, "hfs kprintf", 0)
#define PRINT_DELAY

/*
 * Debugging macros.
 */
#if	DIAGNOSTIC
extern int hfs_dbg_all;
extern int hfs_dbg_vfs;
extern int hfs_dbg_vop;
extern int hfs_dbg_load;
extern int hfs_dbg_io;
extern int hfs_dbg_utils;
extern int hfs_dbg_rw;
extern int hfs_dbg_DIT;
extern int hfs_dbg_tree;
extern int hfs_dbg_err;
extern int hfs_dbg_test;

#ifdef KERNEL
    #if (DEBUG_STAGE == 4)
        #define DBG_ASSERT(a) { if (!(a)) { PRINTIT("Oops - File "__FILE__", line %d: assertion '%s' failed.\n", __LINE__, #a); DEBUG_BREAK } }
	#else
#define DBG_ASSERT(a) { if (!(a)) { panic("File "__FILE__", line %d: assertion '%s' failed.\n", __LINE__, #a); } }
    #endif /* DEBUG_STAGE */
#else
    #define DBG_ASSERT(a) assert(a)
#endif /* KERNEL */

//#define DBG_VFS if (hfs_dbg_all || hfs_dbg_vfs) PRINTIT
#define DBG_VFS(x)	{		\
	if(hfs_dbg_all || hfs_dbg_vfs) {	\
        PRINTIT("%X: ", CURRENT_PROC->p_pid); \
        PRINTIT x;		\
	    PRINT_DELAY;  \
	};			\
}
#define DBG_VFS_CONT(x)	{		\
    if(hfs_dbg_all || hfs_dbg_vfs) {	\
        PRINTIT x;		\
        PRINT_DELAY;  \
    };			\
}
#define DBG_VOP(x)	{		\
    if(hfs_dbg_all || hfs_dbg_vop) {	\
        PRINTIT("%X: ", CURRENT_PROC->p_pid); \
        PRINTIT x;			\
        PRINT_DELAY;  \
    };			\
}
#define DBG_VOP_CONT(x)	{		\
    if(hfs_dbg_all || hfs_dbg_vop) {	\
        PRINTIT x;			\
        PRINT_DELAY;  \
    };			\
}
#define DBG_LOAD(x)	{		\
	if(hfs_dbg_all || hfs_dbg_load) {	\
        PRINTIT("%X: ", CURRENT_PROC->p_pid); \
	    PRINTIT x;			\
	    PRINT_DELAY;  \
	};			\
}
#define DBG_IO(x)	{		\
	if(hfs_dbg_all || hfs_dbg_io) {	\
        PRINTIT("%X: ", CURRENT_PROC->p_pid); \
	    PRINTIT x;			\
	    PRINT_DELAY;  \
	};			\
}
#define DBG_UTILS(x)	{		\
	if(hfs_dbg_all || hfs_dbg_utils) {	\
        PRINTIT("%X: ", CURRENT_PROC->p_pid); \
	    PRINTIT x;			\
        PRINT_DELAY;  \
	};			\
}
#define DBG_RW(x)	{		\
	if(hfs_dbg_all || hfs_dbg_rw) {	\
        PRINTIT("%X: ", CURRENT_PROC->p_pid); \
	    PRINTIT x;			\
	    PRINT_DELAY;  \
	};			\
}
#define DBG_DIT(x)	{		\
	if(hfs_dbg_all || hfs_dbg_DIT) {	\
        PRINTIT("%X: ", CURRENT_PROC->p_pid); \
	    PRINTIT x;			\
	    PRINT_DELAY;  \
	};			\
}
#define DBG_TREE(x)	{		\
	if(hfs_dbg_all || hfs_dbg_tree) {	\
        PRINTIT("%X: ", CURRENT_PROC->p_pid); \
	    PRINTIT x;			\
        PRINT_DELAY;  \
	};			\
}
#define DBG_ERR(x)	{		\
	if(hfs_dbg_all || hfs_dbg_err) {	\
        PRINTIT("%X: ", CURRENT_PROC->p_pid); \
	    PRINTIT("HFS ERROR: "); \
	    PRINTIT x;			\
	    PRINT_DELAY;  \
	};			\
}
#define DBG_TEST(x)	{		\
	if(hfs_dbg_all || hfs_dbg_test) {	\
        PRINTIT("%X: ", CURRENT_PROC->p_pid); \
	    PRINTIT x;			\
	    PRINT_DELAY;  \
	};			\
}
#else	// DIAGNOSTIC
#define DBG_ASSERT(a)
#define DBG_VFS(x)
#define DBG_VFS_CONT(x)
#define DBG_VOP(x)
#define DBG_VOP_CONT(x)
#define DBG_LOAD(x)
#define DBG_IO(x)
#define DBG_UTILS(x)
#define DBG_RW(x)
#define DBG_DIT(x)
#define DBG_TREE(x)
#define DBG_ERR(x)
#define DBG_TEST(x)
#endif	// DIAGNOSTIC


/* Used to help print commone values in the vnode ops */
#if DIAGNOSTIC
    extern void debug_vn_status (char* introStr, struct vnode *vn);
    extern void debug_vn_print (char* introStr, struct vnode *vn);

    #define DBG_VN_STATUS (introStr, vn) 	debug_vn_status (introStr, vn)
    #define DBG_VN_PRINT (introStr, vn) 	debug_vn_print (introStr, vn)
    #define DBG_FUNC_NAME(FSTR) 			static char *funcname = FSTR
	#define DBG_PRINT_FUNC_NAME() 			PRINTIT("%X: %s\n", CURRENT_PROC->p_pid, funcname)
	#define DBG_VOP_PRINT_FUNCNAME()		DBG_VOP(("%s: ", funcname));


    /* This checks to make sure the passed in node is valid and HFS */
    #define	DBG_HFS_NODE_CHECK(VP)	{							\
        if ((VP) == NULL || VTOH((VP))->h_valid != HFS_VNODE_MAGIC) {		\
            DBG_VOP_CONT(("%s: INVALID VNODE: ", funcname));	\
                DBG_VOP_PRINT_VNODE_INFO(VP);				\
                    DBG_VOP_CONT(("\n")); 							\
                        return (EINVAL);							\
                            }											\
        }

    #define DBG_VOP_PRINT_VNODE_INFO(VP)	{ if (VP && VTOH((VP))->h_valid == HFS_VNODE_MAGIC)		{ \
        DBG_VOP_CONT(("\tn: %s, p: %ld, id: %ld, f: %d, u: %d, v: 0x%x ",H_NAME(VTOH(VP)), 	\
                H_DIRID(VTOH(VP)), H_FILEID(VTOH(VP)), H_FORKTYPE(VTOH(VP)), (VP)->v_usecount, (u_int)(VP))); \
                    } else { \
                        DBG_VOP_CONT(("\tBAD MACNODE"));}}

	#define DBG_VOP_PRINT_CPN_INFO(CN) DBG_VOP_CONT(("name: %s",(CN)->cn_nameptr));

#else /* DIAGNOSTIC */

    #define DBG_VN_PRINT(introStr,vn)
    #define DBG_VN_STATUS(introStr,vn)
    #define DBG_FUNC_NAME(FSTR)
    #define DBG_PRINT_FUNC_NAME()
    #define DBG_HFS_NODE_CHECK(VP)
    #define DBG_VOP_PRINT_FUNCNAME()
    #define DBG_VOP_PRINT_VNODE_INFO(VP)
    #define DBG_VOP_PRINT_CPN_INFO(CN)

#endif /* DIAGNOSTIC */


#if DIAGNOSTIC
#define	DBG_VOP_TEST_LOCKS	1
#else /* DIAGNOSTIC */
#undef DBG_VOP_TEST_LOCKS
#endif /* DIAGNOSTIC */



#if DBG_VOP_TEST_LOCKS

typedef struct	VopDbgStoreRec {
    short	id;
    struct vnode	*vp;
    short	inState;
    short	outState;
    short	errState;
    int		inValue;
    int		outValue;
    } VopDbgStoreRec;

    
void DbgVopTest (int max, int error, VopDbgStoreRec *VopDbgStore, char *funcname);
void DbgLookupTest(char *funcname, 	struct componentname  *cnp, struct vnode *dvp, struct vnode *vp);

#define 	VOPDBG_IGNORE			0
#define 	VOPDBG_LOCKED			1
#define 	VOPDBG_UNLOCKED			-1
#define 	VOPDBG_LOCKNOTNIL		2
#define 	VOPDBG_SAME				3

#define 	VOPDBG_ZERO	0
#define 	VOPDBG_POS	1

/*		This sets up the test for the lock state of vnodes. The entry paramaters are:
 *			I = index of paramater
 *			VP = pointer to a vnode
 *			ENTRYSTATE = the inState of the lock
 *			EXITSTATE = the outState of the lock
 *			ERRORSTATE = the error state of the lock
 *		It initializes the structure, does some preliminary validity checks, but does nothing
 *		if the instate is set to be ignored.
 */


#define  DBG_VOP_LOCKS_DECL(I)	VopDbgStoreRec	VopDbgStore[I];short numOfLockSlots=I
#define  DBG_VOP_LOCKS_INIT(I,VP,ENTRYSTATE,EXITSTATE,ERRORSTATE,CHECKFLAG)		\
		if (I >= numOfLockSlots) { \
                DEBUG_BREAK_MSG(("%s: DBG_VOP_LOCKS_INIT: Entry #%d greater than allocated slots!\n", funcname, I)); \
            }; \
           VopDbgStore[I].id 			= I; \
			VopDbgStore[I].vp 			= VP; \
			VopDbgStore[I].inState 		= ENTRYSTATE; \
			VopDbgStore[I].outState 	= EXITSTATE; \
			VopDbgStore[I].errState 	= ERRORSTATE; \
			VopDbgStore[I].inValue 		= 0; \
			VopDbgStore[I].outValue 	= 0; \
			if ((VopDbgStore[I].inState != VOPDBG_IGNORE)) {							\
				if ((VP) == NULL) 														\
                PRINTIT ("%X: %s: DBG_VOP_LOCK on start: Null vnode ptr\n", CURRENT_PROC->p_pid, funcname); 	\
				else 																	\
					VopDbgStore[I].inValue = lockstatus (&(VTOH(VP))->h_lock);			\
				}																		\
			if ((VP) != NULL)															\
				{																		\
				if (CHECKFLAG==VOPDBG_POS && (VP)->v_usecount <= 0) 					\
                PRINTIT("%X: %s: BAD USECOUNT OF %d !!!!\n", CURRENT_PROC->p_pid, funcname, (VP)->v_usecount);	\
				else if ((VP)->v_usecount < 0) 													\
                PRINTIT("%X: %s: BAD USECOUNT OF %d !!!!\n", CURRENT_PROC->p_pid, funcname, (VP)->v_usecount);	\
				}

    #define DBG_VOP_UPDATE_VP(I, VP) \
        	VopDbgStore[I].vp 			= VP;

    #define  DBG_VOP_LOCKS_TEST(status) DbgVopTest (numOfLockSlots, status, VopDbgStore, funcname);
    #define  DBG_VOP_LOOKUP_TEST(funcname, cnp, dvp, vp) DbgLookupTest (funcname, cnp, dvp, vp);

#else   /* DBG_VOP_TEST_LOCKS */

    #define  DBG_VOP_LOCKS_DECL(A)
    #define  DBG_VOP_LOCKS_INIT(A,B,C,D,E,F)
    #define  DBG_VOP_LOCKS_TEST(a)
    #define  DBG_VOP_LOOKUP_TEST(funcname, cnp, dvp, vp)
    #define  DBG_VOP_UPDATE_VP(I, VP)
#endif	/* DBG_VOP_TEST_LOCKS */
