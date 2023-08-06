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

/*
 *	Copyright (c) 1996-1998 Apple Computer, Inc.
 *	All Rights Reserved.
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 *	The copyright notice above does not evidence any actual or
 *	intended publication of such source code.
 */

#define RESOLVE_DBG

#include <appletalk.h>
#include <atp.h>
#include <at_atp.h>
#include <atp_inc.h>
#include <lap.h>
#include <at_lap.h>
#include <ddp.h>
#include <at_ddp.h>
#include <at_asp.h>
#include <adsp_frames.h>
#include <atlog.h>

static void atp_pack_bdsp(struct atp_trans *, struct atpBDS *);
static int atp_unpack_bdsp(struct atp_state *, gbuf_t *, struct atp_rcb *, 
			   int, int);

extern struct atp_rcb_qhead atp_need_rel;
extern int atp_inited;
extern char atp_off_flag;
extern struct atp_state *atp_used_list;
extern atlock_t atpgen_lock;
extern atlock_t atpall_lock;
extern atlock_t atptmo_lock;

extern gref_t *atp_inputQ[];
extern int atp_pidM[];
extern char ot_atp_socketM[];
extern char ddp_off_flag;
extern int ot_protoCnt;
extern char ot_protoT[];

static struct atp_trans *trp_tmo_list;
struct atp_trans *trp_tmo_rcb;

/*
 *	write queue put routine .... filter out other than IOCTLs
 *	Version 1.8 of atp_write.c on 89/02/09 17:53:26
 */

void
atp_wput(gref, m)
register gref_t *gref;
register gbuf_t *m;
{
	register ioc_t    *iocbp;
	int i, xcnt, s;
	struct atp_state *atp;
	struct atp_trans *trp;
	struct atp_rcb   *rcbp;
	at_socket skt;

	atp = (struct atp_state *)gref->info;
	if (atp->dflag)
		atp = (struct atp_state *)atp->atp_msgq;

	switch(gbuf_type(m)) {
	case MSG_DATA:
		if (atp->atp_msgq) {
			gbuf_freem(m);
			dPrintf(D_M_ATP, D_L_WARNING,
				("atp_wput: atp_msgq discarded\n"));
		} else
			atp->atp_msgq = m;
		break;

	case MSG_IOCTL:
		/* Need to ensure that all copyin/copyout calls are made at 
		 * put routine time which should be in the user context. (true when
		 * we are the stream head). The service routine can be called on an
		 * unpredictable context and copyin/copyout calls will get wrong results
		 * or even panic the kernel. 
		 */
	    	iocbp = (ioc_t *)gbuf_rptr(m);

		switch (iocbp->ioc_cmd) {
		case AT_ATP_LINK:
			atp_dequeue_atp(atp);
			ot_protoT[DDP_ATP] = 1;
			ot_protoCnt++;

			iocbp->ioc_rval = 0;
			atp_iocack(atp, m);
			trp_tmo_list = 0;
			trp_tmo_rcb = atp_trans_alloc(0);
			atp_timout(atp_rcb_timer, trp_tmo_rcb, 10 * HZ);
			atp_trp_timer(&atp_inited, 0);
			asp_timer(&atp_inited, 0);
			return;

		case AT_ATP_UNLINK:
			ot_protoT[DDP_ATP] = 0;
			ot_protoCnt--;
			ddp_off_flag = 0; /* why clear this in an unlink? */

			trp_tmo_list = 0;
			atp_trp_timer(&atp_inited, 1);
			asp_timer(&atp_inited, 1);
			atp_inited = 0;
			iocbp->ioc_rval = 0;
			atp_iocack(atp, m);
			return;

		case AT_ATP_BIND_REQ:
			if (gbuf_cont(m) == NULL) {
				iocbp->ioc_rval = -1;
				atp_iocnak(atp, m, EINVAL);
				return;
			}
			skt = *(at_socket *)gbuf_rptr(gbuf_cont(m));
			if ((skt = (at_socket)atp_bind(gref, (unsigned int)skt, 0)) == 0)
				atp_iocnak(atp, m, EINVAL);
			else {
				*(at_socket *)gbuf_rptr(gbuf_cont(m)) = skt;
				iocbp->ioc_rval = 0;
				atp_iocack(atp, m);
				atp_dequeue_atp(atp);
			}
			return;

		case AT_ATP_GET_CHANID:
			if (gbuf_cont(m) == NULL) {
				iocbp->ioc_rval = -1;
				atp_iocnak(atp, m, EINVAL);
				return;
			}
			*(gref_t **)gbuf_rptr(gbuf_cont(m)) = gref;
			atp_iocack(atp, m);
			return;

		case AT_ATP_ISSUE_REQUEST_DEF:
/*		case AT_ATP_ISSUE_REQUEST_DEF_NOTE: not actually XO */
		  /* AT_ATP_ISSUE_REQUEST_DEF_NOTE is used just for tickle 
		     so it's not appropriate to check for it here
		  */
		  {
#define atpBDSsize (sizeof(struct atpBDS)*ATP_TRESP_MAX)
			gbuf_t *bds, *tmp;

			if ((tmp = gbuf_cont(m)) != 0) {
				if ((bds = gbuf_dupb(tmp)) == NULL) {
					atp_iocnak(atp, m, ENOBUFS);
					return;
				}
				gbuf_rinc(tmp,atpBDSsize);
				gbuf_wset(bds,atpBDSsize);
				iocbp->ioc_count -= atpBDSsize;
				gbuf_cont(tmp) = bds;
			}
			/* FALL THRU */
		}

		case AT_ATP_SEND_FULL_RESPONSE: {
			gbuf_t *m2;
			struct atp_rcb *rcbp;
			at_ddp_t *ddp;
			at_atp_t *athp;

			/*
			 * 	send a response to a transaction
			 *		first check it out
			 */
			if (iocbp->ioc_count < TOTAL_ATP_HDR_SIZE) {
			    atp_iocnak(atp, m, EINVAL);
			    break;
			}

			/*
			 *	remove the response from the message
			 */
			m2 = gbuf_cont(m);
			gbuf_cont(m) = NULL;
			iocbp->ioc_count = 0;
			ddp = AT_DDP_HDR(m2);
			athp = AT_ATP_HDR(m2);
			if (atp->atp_msgq) {
				gbuf_cont(m2) = atp->atp_msgq;
				atp->atp_msgq = 0;
			}

			ATDISABLE(s, atp->atp_lock);
			/*
			 *	search for the corresponding rcb
			 */
			for (rcbp = atp->atp_rcb.head; rcbp; rcbp = rcbp->rc_list.next) {
			    if (rcbp->rc_tid == UAS_VALUE(athp->tid) &&
				rcbp->rc_socket.node == ddp->dst_node &&
				rcbp->rc_socket.net == NET_VALUE(ddp->dst_net) &&
				rcbp->rc_socket.socket == ddp->dst_socket)
				break;
			}
			ATENABLE(s, atp->atp_lock);

			/*
			 *	If it has already been sent then return an error
			 */
			if ((rcbp && rcbp->rc_state != RCB_NOTIFIED) || 
			    (rcbp == NULL && athp->xo)) {
			    atp_iocnak(atp, m, ENOENT);
			    gbuf_freem(m2);
			    return;
			}
			if (rcbp == NULL) { /* a response for an ALO transaction */
			    if ((rcbp = atp_rcb_alloc(atp)) == NULL) {
				atp_iocnak(atp, m, ENOBUFS);
				gbuf_freem(m2);
				return;
			    }
			    rcbp->rc_ioctl = 0;
			    rcbp->rc_socket.socket = ddp->dst_socket;
			    rcbp->rc_socket.node = ddp->dst_node;
			    rcbp->rc_socket.net = NET_VALUE(ddp->dst_net);
			    rcbp->rc_tid = UAS_VALUE(athp->tid);
			    rcbp->rc_bitmap = 0xff;
			    rcbp->rc_xo = 0;
			    ATDISABLE(s, atp->atp_lock);
			    rcbp->rc_state = RCB_SENDING;
			    ATP_Q_APPEND(atp->atp_rcb, rcbp, rc_list);
			    ATENABLE(s, atp->atp_lock);
			}
			/* first bds entry gives number of bds 
			 * entries in total (hack)
			 */
			if (gbuf_len(m2) > TOTAL_ATP_HDR_SIZE)
				xcnt = UAS_VALUE(((struct atpBDS *)
					(AT_ATP_HDR(m2)->data))->bdsDataSz);
			else
				xcnt = 0;
			if ((i = atp_unpack_bdsp(atp, m2, rcbp, xcnt, FALSE))) {
			    if ( !rcbp->rc_xo)
				atp_rcb_free(rcbp);
			    atp_iocnak(atp, m, i);
			    return;
			}
			atp_send_replies(atp, rcbp);

			/*
			 *	send the ack back to the responder
			 */
			atp_iocack(atp, m);
			return;
		}

		case AT_ATP_GET_POLL: {
			if (gbuf_cont(m)) {
			    gbuf_freem(gbuf_cont(m));
			    gbuf_cont(m) = NULL;
			    iocbp->ioc_count = 0;
			}

			/*
			 *	search for a waiting request
			 */
			ATDISABLE(s, atp->atp_lock);
			if ((rcbp = atp->atp_attached.head)) {
			    /*
			     *	Got one, move it to the active response Q
			     */
			    gbuf_cont(m) = rcbp->rc_ioctl;
			    rcbp->rc_ioctl = NULL;
			    if (rcbp->rc_xo) {
				ATP_Q_REMOVE(atp->atp_attached, rcbp, rc_list);
				rcbp->rc_state = RCB_NOTIFIED;
				ATP_Q_APPEND(atp->atp_rcb, rcbp, rc_list);
			    } else {
				/* detach rcbp from attached queue,
				 * and free any outstanding resources
				 */
				atp_rcb_free(rcbp);
			    }
				ATENABLE(s, atp->atp_lock);
				atp_iocack(atp, m);
			} else {
				/*
				 *	None available - can out
				 */
				ATENABLE(s, atp->atp_lock);
				atp_iocnak(atp, m, EAGAIN);
			}
			break;
		}

		case AT_ATP_CANCEL_REQUEST: {
			/*
			 *	Cancel a pending request
			 */
			if (iocbp->ioc_count != sizeof(int)) {
			    atp_iocnak(atp, m, EINVAL);
			    break;
			}
			i = *(int *)gbuf_rptr(gbuf_cont(m));
			gbuf_freem(gbuf_cont(m));
			gbuf_cont(m) = NULL;
			ATDISABLE(s, atp->atp_lock);
			for (trp = atp->atp_trans_wait.head; trp; trp = trp->tr_list.next) {
			  if (trp->tr_tid == i)
				break;
			}
			if (trp == NULL) {
			    ATENABLE(s, atp->atp_lock);
			    atp_iocnak(atp, m, ENOENT);
			} else {
				ATENABLE(s, atp->atp_lock);
				atp_free(trp);
				atp_iocack(atp, m);
			}
			break;
		}

		case AT_ATP_PEEK: {
			unsigned char event;
			if (atalk_peek(gref, &event) == -1)
			    atp_iocnak(atp, m, ENOMSG);
			else {
				*gbuf_rptr(gbuf_cont(m)) = event;
				atp_iocack(atp, m);
			}
			break;
		}

		default:
			/*
			 *	Otherwise pass it on, if possible
			 */
			iocbp->ioc_private = (void *)gref;
			if (iocbp->ioc_cmd == DDP_IOC_GET_CFG) {
				iocbp->ioc_count = atp->atp_socket_no;
				iocbp->ioc_error = 3;
			}
			DDP_OUTPUT(m);
			break;
	  }
		break;

	default:
		gbuf_freem(m);
		break;
	}
} /* atp_wput */

gbuf_t  *atp_build_release(trp)
register struct atp_trans *trp;
{
	register gbuf_t   *m;
	register at_ddp_t *ddp;
	register at_atp_t   *athp;

	/*
	 *	Now try and allocate enough space to send the message
	 *		if none is available the caller will schedule
	 *		a timeout so we can retry for more space soon
	 */
	if ((m = (gbuf_t *)gbuf_alloc(AT_WR_OFFSET+ATP_HDR_SIZE, PRI_HI)) != NULL) {
		gbuf_rinc(m,AT_WR_OFFSET);
		gbuf_wset(m,TOTAL_ATP_HDR_SIZE);
		ddp = AT_DDP_HDR(m);
		ddp->type = DDP_ATP;
		UAS_ASSIGN(ddp->checksum, 0);
		ddp->dst_socket = trp->tr_socket.socket;
		ddp->dst_node = trp->tr_socket.node;
		NET_ASSIGN(ddp->dst_net, trp->tr_socket.net);
		ddp->src_node = trp->tr_local_node;
		NET_NET(ddp->src_net, trp->tr_local_net);

		/*
		 * clear the cmd/xo/eom/sts/unused fields
		 */
		athp = AT_ATP_HDR(m);
		ATP_CLEAR_CONTROL(athp);
		athp->cmd = ATP_CMD_TREL;
		UAS_ASSIGN(athp->tid, trp->tr_tid);
	}

	return (m);
}

void atp_send_replies(atp, rcbp)
     register struct atp_state *atp;
     register struct atp_rcb   *rcbp;
{       register gbuf_t *m;
	register int     i, len;
	int              s_gen, s, cnt;
	unsigned char *m0_rptr = NULL, *m0_wptr = NULL;
	register at_atp_t *athp;
	register struct atpBDS *bdsp;
	register gbuf_t *m2, *m1, *m0;
	gbuf_t *mprev, *mlist = 0;
	at_socket src_socket = (at_socket)atp->atp_socket_no;
	gbuf_t *rc_xmt[ATP_TRESP_MAX];
	struct   ddp_atp {
	         char    ddp_atp_hdr[TOTAL_ATP_HDR_SIZE];
	};

	ATDISABLE(s, atp->atp_lock);
	if (rcbp->rc_queue != atp) {
		ATENABLE(s, atp->atp_lock);
		return;
	}
	if (rcbp->rc_not_sent_bitmap == 0)
		goto nothing_to_send;

	dPrintf(D_M_ATP_LOW, D_L_OUTPUT, ("atp_send_replies\n"));
	/*
	 *	Do this for each message that hasn't been sent
	 */
	cnt = rcbp->rc_pktcnt;
	for (i = 0; i < cnt; i++) {
	  rc_xmt[i] = 0;
	  if (rcbp->rc_snd[i]) {
		if ((rc_xmt[i] = gbuf_alloc(AT_WR_OFFSET+TOTAL_ATP_HDR_SIZE,PRI_MED)) == NULL) {
			for (cnt = 0; cnt < i; cnt++)
				if (rc_xmt[cnt])
					gbuf_freeb(rc_xmt[cnt]);
			goto nothing_to_send;
		}
	  }
	}

	m = rcbp->rc_xmt;
	m0 = gbuf_cont(m);
	if (m0) {
		m0_rptr = gbuf_rptr(m0);
		m0_wptr = gbuf_wptr(m0);
	}
	if (gbuf_len(m) > TOTAL_ATP_HDR_SIZE)
	  	bdsp = (struct atpBDS *)(AT_ATP_HDR(m)->data);
	else
		bdsp = 0;

	for (i = 0; i < cnt; i++) {
	  if (rcbp->rc_snd[i] == 0) {
		if ((len = UAS_VALUE(bdsp->bdsBuffSz)))
			gbuf_rinc(m0,len);

	  } else {
	        m2 = rc_xmt[i];
		gbuf_rinc(m2,AT_WR_OFFSET);
		gbuf_wset(m2,TOTAL_ATP_HDR_SIZE);
		*(struct ddp_atp *)(gbuf_rptr(m2))= *(struct ddp_atp *)(gbuf_rptr(m));
		athp = AT_ATP_HDR(m2);
		ATP_CLEAR_CONTROL(athp);
		athp->cmd = ATP_CMD_TRESP;
		athp->bitmap = i;
		if (i == (cnt - 1))
		        athp->eom = 1; /* for the last fragment */
		if (bdsp)
		  	UAL_UAL(athp->user_bytes, bdsp->bdsUserData);

		if (bdsp)
		  if (len = UAS_VALUE(bdsp->bdsBuffSz)) { /* copy in data */
		    if (m0 && gbuf_len(m0)) {
		      if ((m1 = gbuf_dupb(m0)) == NULL) {
				for (i = 0; i < cnt; i++)
					if (rc_xmt[i])
						gbuf_freem(rc_xmt[i]);
				gbuf_rptr(m0) = m0_rptr;
				gbuf_wset(m0,(m0_wptr-m0_rptr));
				goto nothing_to_send;
			}
			gbuf_wset(m1,len);
			gbuf_rinc(m0,len);
			if ((len = gbuf_len(m0)) < 0) {
				gbuf_rdec(m0,len);
				gbuf_wdec(m1,len);
				if (NULL == (gbuf_cont(m1) = gbuf_dupb(gbuf_cont(m0)))) {
				  for (i = 0; i < cnt; i++)
					if (rc_xmt[i])
						gbuf_freem(rc_xmt[i]);
				  gbuf_rptr(m0) = m0_rptr;
				  gbuf_wset(m0,(m0_wptr-m0_rptr));
				  goto nothing_to_send;
				}
			} else
				gbuf_cont(m1) = 0;
			gbuf_cont(m2) = m1;
		  }
		}

	  AT_DDP_HDR(m2)->src_socket = src_socket;
	  dPrintf(D_M_ATP_LOW, D_L_OUTPUT,
		("atp_send_replies: %d, socket=%d, size=%d\n",
		i, atp->atp_socket_no, gbuf_msgsize(gbuf_cont(m2))));

	  if (mlist)
	  	gbuf_next(mprev) = m2;
	  else
	  	mlist = m2;
	  mprev = m2;

	  rcbp->rc_snd[i] = 0;
	  rcbp->rc_not_sent_bitmap &= ~atp_mask[i];
	  if (rcbp->rc_not_sent_bitmap == 0)
	  	break;
	  }
	  /*
	   * on to the next frag
	   */
	  bdsp++;
	}
	if (m0) {
		gbuf_rptr(m0) = m0_rptr;
		gbuf_wset(m0,(m0_wptr-m0_rptr));
	}

	if (mlist) {
		ATENABLE(s, atp->atp_lock);
		DDP_OUTPUT(mlist);
		ATDISABLE(s, atp->atp_lock);
	}

nothing_to_send:
	/*
	 *	If all replies from this reply block have been sent then 
	 *		remove it from the queue and mark it so
	 */
	if (rcbp->rc_queue != atp) {
		ATENABLE(s, atp->atp_lock);
		return;
	}
	rcbp->rc_rep_waiting = 0;

	/*
	 *	If we are doing execute once re-set the rcb timeout 
	 *	each time we send back any part of the response. Note
	 * 	that this timer is started when an initial request is
	 *	received. Each response reprimes the timer.  Duplicate
	 * 	requests do not reprime the timer.
	 *     
	 *	We have sent all of a response so free the 
	 * 	resources.
	 */
	if (rcbp->rc_xo && rcbp->rc_state != RCB_RELEASED) {
		ATDISABLE(s_gen, atpgen_lock);
		if (rcbp->rc_timestamp == 0) {
	        	rcbp->rc_timestamp = time.tv_sec;
			if (rcbp->rc_timestamp == 0)
		  		rcbp->rc_timestamp = 1;
			ATP_Q_APPEND(atp_need_rel, rcbp, rc_tlist);
		}
		rcbp->rc_state = RCB_RESPONSE_FULL;
		ATENABLE(s_gen, atpgen_lock);
	} else
		atp_rcb_free(rcbp);
	ATENABLE(s, atp->atp_lock);
	} /* atp_send_replies */


static void
atp_pack_bdsp(trp, bdsp)
     register struct atp_trans *trp;
     register struct atpBDS *bdsp;
{
	register gbuf_t *m = NULL;
	register int i, datsize = 0;
	struct atpBDS *bdsbase = bdsp;

	dPrintf(D_M_ATP, D_L_INFO, ("atp_pack_bdsp: socket=%d\n",
		trp->tr_queue->atp_socket_no));

	for (i = 0; i < ATP_TRESP_MAX; i++, bdsp++) {
	  	short bufsize = UAS_VALUE(bdsp->bdsBuffSz);
		long bufaddr = UAL_VALUE(bdsp->bdsBuffAddr);

	        if ((m = trp->tr_rcv[i]) == NULL)
		        break;

		/* discard ddp hdr on first packet */
		if (i == 0)
			gbuf_rinc(m,DDP_X_HDR_SIZE); 

		/* this field may contain control information even when 
		   no data is present */
		UAL_UAL(bdsp->bdsUserData, 
			(((at_atp_t *)(gbuf_rptr(m)))->user_bytes));
		gbuf_rinc(m, ATP_HDR_SIZE);

		if ((bufsize != 0) && (bufaddr != 0)) {
		        /* user expects data back */
			short tmp = 0;
			register char *buf = (char *)bufaddr;

			while (m) {
			  	short len = (short)(gbuf_len(m));
				if (len) {
				  	if (len > bufsize)
						len = bufsize;
					copyout((caddr_t)gbuf_rptr(m), 
						(caddr_t)&buf[tmp],
						len);
					bufsize -= len;
					tmp =+ len;
				}
				m = gbuf_cont(m);
			}

			UAS_ASSIGN(bdsp->bdsDataSz, tmp);
			datsize += (int)tmp;
		}
		gbuf_freem(trp->tr_rcv[i]);
		trp->tr_rcv[i] = NULL;
	}

	/* report the number of packets */
	UAS_ASSIGN(((struct atpBDS *)bdsbase)->bdsBuffSz, i);

	dPrintf(D_M_ATP, D_L_INFO, ("             : size=%d\n",
		datsize));
} /* atp_pack_bdsp */


static int
atp_unpack_bdsp(atp, m, rcbp, cnt, wait)
	struct atp_state *atp;
        gbuf_t          *m;	/* ddp, atp and bdsp gbuf_t */
	register struct atp_rcb *rcbp;
        register int    cnt, wait;
{
	register struct atpBDS *bdsp;
	register gbuf_t        *m2, *m1, *m0;
        register at_atp_t        *athp;
	register int  i, len, s_gen;
	at_socket src_socket;
	struct   ddp_atp {
	         char    ddp_atp_hdr[TOTAL_ATP_HDR_SIZE];
	};
	gbuf_t *mprev, *mlist = 0;
	gbuf_t *rc_xmt[ATP_TRESP_MAX];
	unsigned char *m0_rptr, *m0_wptr;

	/*
	 * get the user data structure pointer
	 */
	bdsp = (struct atpBDS *)(AT_ATP_HDR(m)->data);

	/*
	 * Guard against bogus count argument.
	 */
	if ((unsigned) cnt > ATP_TRESP_MAX) {
		dPrintf(D_M_ATP, D_L_ERROR,
			("atp_unpack_bdsp: bad bds count 0x%x\n", cnt));
		gbuf_freem(m);
		return(EINVAL);
	}
	if ((src_socket = (at_socket)atp->atp_socket_no) == 0xFF) {
	  /* comparison was to -1, however src_socket is a u_char */
		gbuf_freem(m);
		return EPIPE;
	}

	m0 = gbuf_cont(m);
	rcbp->rc_xmt = m;
	rcbp->rc_pktcnt = cnt;
	rcbp->rc_state = RCB_SENDING;
	rcbp->rc_not_sent_bitmap = 0;

	if (cnt <= 1) {
	        /*
		 * special case this to
		 * improve AFP write transactions to the server
		 */
		rcbp->rc_pktcnt = 1;
		if ((m2 = gbuf_alloc_wait(AT_WR_OFFSET+TOTAL_ATP_HDR_SIZE, 
					  wait)) == NULL)
		    return 0;
		gbuf_rinc(m2,AT_WR_OFFSET);
		gbuf_wset(m2,TOTAL_ATP_HDR_SIZE);
		*(struct ddp_atp *)(gbuf_rptr(m2))= *(struct ddp_atp *)(gbuf_rptr(m));
		athp = AT_ATP_HDR(m2);
		ATP_CLEAR_CONTROL(athp);
		athp->cmd = ATP_CMD_TRESP;
		athp->bitmap = 0;
		athp->eom = 1;     /* there's only 1 fragment */

		/* *** why only if cnt > 0? *** */
		if (cnt > 0)
			UAL_UAL(athp->user_bytes, bdsp->bdsUserData);
		if (m0)
		  	if ((gbuf_cont(m2) = gbuf_dupb_wait(m0, wait)) == NULL) {
			  	gbuf_freeb(m2);
				return 0;
			}
		/*
		 *	send the message and mark it as sent
		 */
		AT_DDP_HDR(m2)->src_socket = src_socket;
		dPrintf(D_M_ATP_LOW, D_L_INFO,
			("atp_unpack_bdsp %d, socket=%d, size=%d, cnt=%d\n",
			0,atp->atp_socket_no,gbuf_msgsize(gbuf_cont(m2)),cnt));
		mlist = m2;
		goto l_send;
	}

	for (i = 0; i < cnt; i++) {
	        /* all hdrs, packet data and dst addr storage */
		if ((rc_xmt[i] = 
		     gbuf_alloc_wait(AT_WR_OFFSET+TOTAL_ATP_HDR_SIZE,
				     wait)) == NULL) {
			for (cnt = 0; cnt < i; cnt++)
				if (rc_xmt[cnt])
					gbuf_freeb(rc_xmt[cnt]);
			return 0;
		}
	}

	if (m0) {
	  	m0_rptr = gbuf_rptr(m0);
		m0_wptr = gbuf_wptr(m0);
	}

	for (i = 0; i < cnt; i++) {
	        m2 = rc_xmt[i];
		gbuf_rinc(m2,AT_WR_OFFSET);
		gbuf_wset(m2,TOTAL_ATP_HDR_SIZE);
		*(struct ddp_atp *)(gbuf_rptr(m2))= *(struct ddp_atp *)(gbuf_rptr(m));
		athp = AT_ATP_HDR(m2);
		ATP_CLEAR_CONTROL(athp);
		athp->cmd = ATP_CMD_TRESP;
		athp->bitmap = i;
		if (i == (cnt - 1))
			athp->eom = 1; /* for the last fragment */
		UAL_UAL(athp->user_bytes, bdsp->bdsUserData);

		if ((len = UAS_VALUE(bdsp->bdsBuffSz))) { /* copy in data */
		  if (m0 && gbuf_len(m0)) {
		  	if ((m1 = gbuf_dupb_wait(m0, wait)) == NULL) {
				for (i = 0; i < cnt; i++)
					if (rc_xmt[i])
						gbuf_freem(rc_xmt[i]);
				gbuf_rptr(m0) = m0_rptr;
				gbuf_wset(m0,(m0_wptr-m0_rptr));
				return 0;
			}
			gbuf_wset(m1,len);
			gbuf_rinc(m0,len);
			if ((len = gbuf_len(m0)) < 0) {
				gbuf_rdec(m0,len);
				gbuf_wdec(m1,len);
				if ((gbuf_cont(m1) = gbuf_dupb_wait(gbuf_cont(m0), wait)) == NULL) {
				  for (i = 0; i < cnt; i++)
				    	if (rc_xmt[i])
						gbuf_freem(rc_xmt[i]);
				  gbuf_rptr(m0) = m0_rptr;
				  gbuf_wset(m0,(m0_wptr-m0_rptr));
				  return 0;
				}
			} else
				gbuf_cont(m1) = 0;
			gbuf_cont(m2) = m1;
		  }
		}

		AT_DDP_HDR(m2)->src_socket = src_socket;
		dPrintf(D_M_ATP_LOW,D_L_INFO,
			("atp_unpack_bdsp %d, socket=%d, size=%d, cnt=%d\n",
			i,atp->atp_socket_no,gbuf_msgsize(gbuf_cont(m2)),cnt));
		if (mlist)
			gbuf_next(mprev) = m2;
		else
			mlist = m2;
		mprev = m2;
		/*
		 * on to the next frag
		 */
		bdsp++;
	}
	if (m0) {
	  	gbuf_rptr(m0) = m0_rptr;
		gbuf_wset(m0,(m0_wptr-m0_rptr));
	}
	/*
	 * send the message
	 */
l_send:
	if (rcbp->rc_xo) {
		ATDISABLE(s_gen, atpgen_lock);
		if (rcbp->rc_timestamp == 0) {
			if ((rcbp->rc_timestamp = time.tv_sec) == 0)
				rcbp->rc_timestamp = 1;
			ATP_Q_APPEND(atp_need_rel, rcbp, rc_tlist);
		}
		ATENABLE(s_gen, atpgen_lock);
	}

	DDP_OUTPUT(mlist);
	return 0;
} /* atp_unpack_bdsp */

#define ATP_SOCKET_LAST  (DDP_SOCKET_LAST-6)
#define ATP_SOCKET_FIRST (DDP_SOCKET_1st_DYNAMIC-64)
static unsigned int sNext = 0;

int atp_bind(gref, sVal, flag)
	gref_t *gref;
	unsigned int sVal;
	unsigned char *flag;
{
	extern unsigned char asp_inpC[];
	extern asp_scb_t *asp_scbQ[];
	unsigned char inpC, sNextUsed = 0;
	unsigned int sMin, sMax, sSav;
	struct atp_state *atp;
	int s;

	atp = (struct atp_state *)gref->info;
	if (atp->dflag)
		atp = (struct atp_state *)atp->atp_msgq;

	sMax = ATP_SOCKET_LAST;
	sMin = ATP_SOCKET_FIRST;
	ATDISABLE(s, atpgen_lock);
	if (flag && (*flag == 3)) {
		sMin += 40;
		if (sMin < sNext) {
			sMin = sNext;
			sNextUsed = 1;
		}
	}

	if ( (sVal != 0)
		&& ((sVal > sMax) || (sVal < 2) || (sVal == 6)
		    || ((atp_inputQ[sVal] != NULL) && 
			(atp_inputQ[sVal] != (gref_t *)1))
		    || (ot_atp_socketM[sVal] != 0)) )
	{
		ATENABLE(s, atpgen_lock);
		return 0;
	}

	if (sVal == 0) {
		inpC = 255;
again:
		for (sVal=sMin; sVal <= sMax; sVal++) {
			if (ot_atp_socketM[sVal] == 0) {
				if (atp_inputQ[sVal] == NULL || 
				    atp_inputQ[sVal] == (gref_t *)1)
					break;
				else if (flag && (*flag == 3)) {
					if ((asp_scbQ[sVal]->dflag == *flag)
							&& (asp_inpC[sVal] < inpC) ) {
						inpC = asp_inpC[sVal];
						sSav = sVal;
					}
				}
			}
		}
		if (sVal > sMax) {
			if (flag && (*flag == 3)) {
				if (sNextUsed) {
					sNextUsed = 0;
					sMax = sNext - 1;
					sMin = ATP_SOCKET_FIRST+40;
					goto again;
				}
				sNext = 0;
				*flag = (unsigned char)sSav;
			}
			ATENABLE(s, atpgen_lock);
			return 0;
		}
	}
	atp->atp_socket_no = (short)sVal;
	atp_inputQ[sVal] = gref;
	if (flag == 0)
		atp_pidM[sVal] = atp->atp_pid;
	else if (*flag == 3) {
		sNext = sVal + 1;
		if (sNext > ATP_SOCKET_LAST)
			sNext = 0;
	}

	ATENABLE(s, atpgen_lock);
	return (int)sVal;
}

void atp_req_ind(atp, mioc)
	register struct atp_state *atp;
	register gbuf_t *mioc;
{
	register struct atp_rcb *rcbp;
	int s;

	if ((rcbp = atp->atp_attached.head) != 0) {
		gbuf_cont(mioc) = rcbp->rc_ioctl;
		rcbp->rc_ioctl = NULL;
		ATDISABLE(s, atp->atp_lock);
		if (rcbp->rc_xo) {
			ATP_Q_REMOVE(atp->atp_attached, rcbp, rc_list);
			rcbp->rc_state = RCB_NOTIFIED;
			ATP_Q_APPEND(atp->atp_rcb, rcbp, rc_list);
		} else
			atp_rcb_free(rcbp);
		ATENABLE(s, atp->atp_lock);
		if (gbuf_cont(mioc))
		  ((ioc_t *)gbuf_rptr(mioc))->ioc_count = gbuf_msgsize(gbuf_cont(mioc));
		else
		  ((ioc_t *)gbuf_rptr(mioc))->ioc_count = 0;
		asp_ack_reply(atp->atp_gref, mioc);
	} else
		gbuf_freeb(mioc);
}

void atp_rsp_ind(trp, mioc)
    register struct atp_trans *trp;
	register gbuf_t *mioc;
{
	register struct atp_state *atp = trp->tr_queue;
	register int err;
	gbuf_t *xm = 0;

	err = 0;
	{
	    switch (trp->tr_state) {
	    case TRANS_DONE:
			if (asp_pack_bdsp(trp, &xm) < 0)
			    err = EFAULT;
			gbuf_cont(mioc) = trp->tr_xmt;
			trp->tr_xmt = NULL;
			break;

	    case TRANS_FAILED:
			err = ETIMEDOUT;
			break;

	    default:
			err = ENOENT;
			break;
	    }
	    atp_free(trp);

	    if (err) {
			dPrintf(D_M_ATP, D_L_ERROR,
				("atp_rsp_ind: TRANSACTION error\n"));
			atp_iocnak(atp, mioc, err);
	    } else {
			gbuf_cont(gbuf_cont(mioc)) = xm;
	    	atp_iocack(atp, mioc);
	    }
	}
}

void atp_cancel_req(gref, tid)
	gref_t *gref;
	unsigned short tid;
{
	int s;
	struct atp_state *atp;
    struct atp_trans *trp;

	atp = (struct atp_state *)gref->info;
	if (atp->dflag)
		atp = (struct atp_state *)atp->atp_msgq;

	ATDISABLE(s, atp->atp_lock);
	for (trp = atp->atp_trans_wait.head; trp; trp = trp->tr_list.next) {
	    if (trp->tr_tid == tid)
			break;
	}
	ATENABLE(s, atp->atp_lock);
	if (trp != NULL)
		atp_free(trp);
}

/*
 * stop ATP now
 */
void
atp_stop(m, flag)
	gbuf_t *m;
	int flag;
{
	struct atp_trans *trp;
	int k, s, *x_wptr;
	struct atp_state *atp;
	gref_t *gref;

	gbuf_rptr(m)[1]++;

	if (flag)
		atp_off_flag = 1;

	asp_stop(m, flag);
	ATDISABLE(s, atpall_lock);

	x_wptr = (int *)gbuf_wptr(m);
	for (atp = atp_used_list; atp; atp = atp->atp_trans_waiting) {
	  if (flag == 2) {
		atp->atp_flags |= ATP_CLOSING;
	  } else {
		*(int *)gbuf_wptr(m) = atp->atp_pid;
		gbuf_winc(m,sizeof(int));
	  }
	}

	for (k=0; k < 256; k++) {
		if (((gref = atp_inputQ[k]) == 0) || gref == (gref_t *)1)
			continue;
		atp = (struct atp_state *)gref->info;	
		if (atp->dflag)
			continue;
	  if (flag == 2) {
		atp->atp_flags |= ATP_CLOSING;
		for (trp = atp->atp_trans_wait.head; trp; trp = trp->tr_list.next) {
		    if (trp->tr_rsp_wait) {
				trp->tr_state = TRANS_FAILED;
				thread_wakeup(&trp->tr_event);
			}
		}
	  } else {
		*(int *)gbuf_wptr(m) = atp->atp_pid;
		gbuf_winc(m,sizeof(int));
	  }
	}
	ATENABLE(s, atpall_lock);

	while (x_wptr != (int *)gbuf_wptr(m)) {
		dPrintf(D_M_ATP, D_L_TRACE, ("atp_stop: pid=%d\n", *x_wptr));
		x_wptr++;
	}
}

/*
 * remove atp from the use list
 */
void
atp_dequeue_atp(atp)
	struct atp_state *atp;
{
	int s;

	ATDISABLE(s, atpall_lock);
	if (atp == atp_used_list) {
		if ((atp_used_list = atp->atp_trans_waiting) != 0)
			atp->atp_trans_waiting->atp_rcb_waiting = 0;
	} else if (atp->atp_rcb_waiting) {
		if ((atp->atp_rcb_waiting->atp_trans_waiting
				= atp->atp_trans_waiting) != 0)
			atp->atp_trans_waiting->atp_rcb_waiting = atp->atp_rcb_waiting;
	}

	atp->atp_trans_waiting = 0;
	atp->atp_rcb_waiting = 0;
	ATENABLE(s, atpall_lock);
}

void
atp_timout(func, trp, ticks)
	void (*func)();
	struct atp_trans *trp;
	int ticks;
{
	int s;
	unsigned int sum;
	struct atp_trans *curr_trp, *prev_trp;

	ATDISABLE(s, atptmo_lock);
	if (trp->tr_tmo_func) {
		ATENABLE(s, atptmo_lock);
		return;
	}

	trp->tr_tmo_func = func;
	trp->tr_tmo_delta = 1+(ticks>>5);

	if (trp_tmo_list == 0) {
		trp->tr_tmo_next = trp->tr_tmo_prev = 0;
		trp_tmo_list = trp;
		ATENABLE(s, atptmo_lock);
		return;
	}

	prev_trp = 0;
	curr_trp = trp_tmo_list;
	sum = 0;

	while (1) {
		sum += curr_trp->tr_tmo_delta;
		if (sum > trp->tr_tmo_delta) {
			sum -= curr_trp->tr_tmo_delta;
			trp->tr_tmo_delta -= sum;
			curr_trp->tr_tmo_delta -= trp->tr_tmo_delta;
			break;
		}
		prev_trp = curr_trp;
		if ((curr_trp = curr_trp->tr_tmo_next) == 0) {
			trp->tr_tmo_delta -= sum;
			break;
		}
	}

	if (prev_trp) {
		trp->tr_tmo_prev = prev_trp;
		if ((trp->tr_tmo_next = prev_trp->tr_tmo_next) != 0)
			prev_trp->tr_tmo_next->tr_tmo_prev = trp;
		prev_trp->tr_tmo_next = trp;
	} else {
		trp->tr_tmo_prev = 0;
		trp->tr_tmo_next = trp_tmo_list;
		trp_tmo_list->tr_tmo_prev = trp;
		trp_tmo_list = trp;
	}
	ATENABLE(s, atptmo_lock);
}

void
atp_untimout(func, trp)
	void (*func)();
	struct atp_trans *trp;
{
	int s;

	ATDISABLE(s, atptmo_lock);
	if (trp->tr_tmo_func == 0) {
		ATENABLE(s, atptmo_lock);
		return;
	}

	if (trp_tmo_list == trp) {
		if ((trp_tmo_list = trp->tr_tmo_next) != 0) {
			trp_tmo_list->tr_tmo_prev = 0;
			trp->tr_tmo_next->tr_tmo_delta += trp->tr_tmo_delta;
		}
	} else {
		if ((trp->tr_tmo_prev->tr_tmo_next = trp->tr_tmo_next) != 0) {
			trp->tr_tmo_next->tr_tmo_prev = trp->tr_tmo_prev;
			trp->tr_tmo_next->tr_tmo_delta += trp->tr_tmo_delta;
		}
	}
	trp->tr_tmo_func = 0;
	ATENABLE(s, atptmo_lock);
}

static void *trp_clock_tmo = 0;

void
atp_trp_clock(arg)
	void *arg;
{
if (!atp_off_flag)
	atp_trp_timer(arg, 0);
}

void
atp_trp_timer(arg, flag)
	void *arg;
	int flag;
{
	int s;
	struct atp_trans *trp;
	void (*tr_tmo_func)();

	if (atp_off_flag)
		return;
	if (flag) {
		atp_off_flag = 1;
		atalk_untimeout(atp_trp_clock, (void *)arg, trp_clock_tmo);
		trp_clock_tmo = 0;
		return;
	}

	ATDISABLE(s, atptmo_lock);
	if (trp_tmo_list)
		trp_tmo_list->tr_tmo_delta--;
	while (((trp = trp_tmo_list) != 0) && (trp_tmo_list->tr_tmo_delta == 0)) {
		if ((trp_tmo_list = trp->tr_tmo_next) != 0)
			trp_tmo_list->tr_tmo_prev = 0;
		if ((tr_tmo_func = trp->tr_tmo_func) != 0) {
			trp->tr_tmo_func = 0;
			ATENABLE(s, atptmo_lock);
			(*tr_tmo_func)(trp);
			ATDISABLE(s, atptmo_lock);
		}
	}
	ATENABLE(s, atptmo_lock);

	trp_clock_tmo = (void *)atalk_timeout(atp_trp_clock, (void *)arg, (1<<5));
}

void
atp_send_req(gref, mioc)
	gref_t *gref;
	gbuf_t *mioc;
{
	register struct atp_state *atp;
	register struct atp_trans *trp;
	register ioc_t *iocbp;
	register at_atp_t *athp;
	register at_ddp_t *ddp;
	gbuf_t *m, *m2, *bds;
	struct atp_set_default *sdb;
	int s, old;
	unsigned int timer;

	atp = (struct atp_state *)((struct atp_state *)gref->info)->atp_msgq;
	iocbp = (ioc_t *)gbuf_rptr(mioc);

	if ((trp = atp_trans_alloc(atp)) == NULL) {
l_retry:
		((asp_scb_t *)gref->info)->stat_msg = mioc;
		iocbp->ioc_private = (void *)gref;
		atalk_timeout(atp_retry_req, mioc, 10);
		return;
	}

	{
		m2 = gbuf_cont(mioc);

		if ((bds = gbuf_dupb(m2)) == NULL) {
			atp_trans_free(trp);
			goto l_retry;
		}
		gbuf_rinc(m2,atpBDSsize);
		gbuf_wset(bds,atpBDSsize);
		iocbp->ioc_count -= atpBDSsize;
		gbuf_cont(m2) = NULL;
	}

	old = iocbp->ioc_cmd;
	iocbp->ioc_cmd = AT_ATP_ISSUE_REQUEST;
	sdb = (struct atp_set_default *)gbuf_rptr(m2);

	/*
	 * The at_snd_req library routine multiplies seconds by 100.
	 * We need to divide by 100 in order to obtain the timer.
	 */
	if ((timer = (sdb->def_rate * HZ)/100) == 0)
	    timer = HZ;
	iocbp->ioc_count -= sizeof(struct atp_set_default);
	gbuf_rinc(m2,sizeof(struct atp_set_default));

	trp->tr_retry = sdb->def_retries;
	trp->tr_timeout = timer;
	trp->tr_bdsp = bds;
	trp->tr_tid = atp_tid(atp);
	trp->tr_xmt = mioc;

	/*
	 *	Now fill in the header (and remember the bits
	 *		we need to know)
	 */
	athp = AT_ATP_HDR(m2);
	athp->cmd = ATP_CMD_TREQ;
	UAS_ASSIGN(athp->tid, trp->tr_tid);
	athp->eom = 0;
	athp->sts = 0;
	trp->tr_xo = athp->xo;
	trp->tr_bitmap = athp->bitmap;
	ddp = AT_DDP_HDR(m2);
	ddp->type = DDP_ATP;
	ddp->src_socket = (at_socket)atp->atp_socket_no;
	trp->tr_socket.socket = ddp->dst_socket;
	trp->tr_socket.node = ddp->dst_node;
	trp->tr_socket.net = NET_VALUE(ddp->dst_net);
	trp->tr_local_socket = atp->atp_socket_no;
	trp->tr_local_node = ddp->src_node;
	NET_NET(trp->tr_local_net, ddp->src_net);
	/*
	 *	Put us in the transaction waiting queue
	 */
	ATDISABLE(s, atp->atp_lock);
	ATP_Q_APPEND(atp->atp_trans_wait, trp, tr_list);
	ATENABLE(s, atp->atp_lock);

	/*
	 * Send the message and set the timer	
	 */
	if ( !trp->tr_retry && !trp->tr_bitmap && !trp->tr_xo) { 
		m = (gbuf_t *)gbuf_copym(m2);
		atp_x_done(trp); /* no reason to tie up resources */
	} else {
		m = (gbuf_t *)gbuf_dupm(m2);
		atp_timout(atp_req_timeout, trp, trp->tr_timeout);
	}
	if (m)
		DDP_OUTPUT(m);
}

void
atp_retry_req(m)
	gbuf_t *m;
{
	gref_t *gref;

	gref = (gref_t *)((ioc_t *)gbuf_rptr(m))->ioc_private;
	if (gref->info) {
		((asp_scb_t *)gref->info)->stat_msg = 0;
		atp_send_req(gref, m);
	}
}

void
atp_send_rsp(gref, m, wait)
	gref_t *gref;
	gbuf_t *m;
	int wait;
{
	register struct atp_state *atp;
	register struct atp_rcb *rcbp;
	register at_atp_t *athp;
	register at_ddp_t *ddp;
	int s, xcnt;

	atp = (struct atp_state *)gref->info;
	if (atp->dflag)
		atp = (struct atp_state *)atp->atp_msgq;
	ddp = AT_DDP_HDR(m);
	athp = AT_ATP_HDR(m);

	/*
	 *	search for the corresponding rcb
	 */
	ATDISABLE(s, atp->atp_lock);
	for (rcbp = atp->atp_rcb.head; rcbp; rcbp = rcbp->rc_list.next) {
	    if ( (rcbp->rc_tid == UAS_VALUE(athp->tid)) &&
			(rcbp->rc_socket.node == ddp->dst_node) &&
			(rcbp->rc_socket.net == NET_VALUE(ddp->dst_net)) &&
			(rcbp->rc_socket.socket == ddp->dst_socket) )
		break;
	}

	/*
	 *	If it has already been sent then drop the request
	 */
	if ((rcbp && (rcbp->rc_state != RCB_NOTIFIED)) ||
			(rcbp == NULL && athp->xo) ) {
		ATENABLE(s, atp->atp_lock);
		gbuf_freem(m);
		return;
	}
	ATENABLE(s, atp->atp_lock);

	if (rcbp == NULL) { /* a response is being sent for an ALO transaction */
	    if ((rcbp = atp_rcb_alloc(atp)) == NULL) {
			gbuf_freem(m);
			return;
	    }
	    rcbp->rc_ioctl = 0;
	    rcbp->rc_socket.socket = ddp->dst_socket;
	    rcbp->rc_socket.node = ddp->dst_node;
	    rcbp->rc_socket.net = NET_VALUE(ddp->dst_net);
	    rcbp->rc_tid = UAS_VALUE(athp->tid);
	    rcbp->rc_bitmap = 0xff;
	    rcbp->rc_xo = 0;
	    rcbp->rc_state = RCB_RESPONSE_FULL;
		ATDISABLE(s, atp->atp_lock);
	    ATP_Q_APPEND(atp->atp_rcb, rcbp, rc_list);
		ATENABLE(s, atp->atp_lock);
	}
	else if (ddp->src_node == 0) {
		NET_NET(ddp->src_net, rcbp->rc_local_net);
		ddp->src_node = rcbp->rc_local_node;
	}

	/* first bds entry gives number of bds entries in total (hack) */
	xcnt = (gbuf_len(m) > TOTAL_ATP_HDR_SIZE) ? 
	  UAS_VALUE(((struct atpBDS *)(AT_ATP_HDR(m)->data))->bdsDataSz) : 0;
	s = atp_unpack_bdsp(atp, m, rcbp, xcnt, wait);
	if (s == 0)
		atp_send_replies(atp, rcbp);
} /* atp_send_rsp */

int
asp_pack_bdsp(trp, xm)
register struct  atp_trans     *trp;
gbuf_t **xm;
{
	register struct atpBDS *bdsp;
	register gbuf_t        *m, *m2;
	register int           i;
	gbuf_t        *m_prev, *m_head = 0;

	dPrintf(D_M_ATP, D_L_INFO, ("asp_pack_bdsp: socket=%d\n",
		trp->tr_queue->atp_socket_no));

	if ((m2 = trp->tr_bdsp) == NULL)
	        return 0;
	trp->tr_bdsp = NULL;
	bdsp = (struct atpBDS *)gbuf_rptr(m2);

	for (i = 0; (i < ATP_TRESP_MAX && 
		     bdsp < (struct atpBDS *)(gbuf_wptr(m2))); i++) {
	        if ((m = trp->tr_rcv[i]) == NULL)
		        break;
		if (i == 0) {
		        /* discard ddp hdr on first packet */
		        gbuf_rinc(m,DDP_X_HDR_SIZE); 
		}

		UAL_UAL(bdsp->bdsUserData, (((at_atp_t *)(gbuf_rptr(m)))->user_bytes));
		gbuf_rinc(m, ATP_HDR_SIZE);

		if (UAL_VALUE(bdsp->bdsBuffAddr)) {
		        /* user expects data back */
			short tmp;
			gbuf_t *tmp_m;

			while (m && ((tmp = (short)(gbuf_len(m))) == 0)) {
				tmp_m = m;
				m = gbuf_cont(m);
				gbuf_freeb(tmp_m);
			}
			if (m_head == 0)
				m_head = m;
			else
				gbuf_cont(m_prev) = m;
			if (m) {
				while (gbuf_cont(m)) {
					m = gbuf_cont(m);
			        tmp += (short)(gbuf_len(m));
				}
				m_prev = m;
			}
			UAS_ASSIGN(bdsp->bdsDataSz, tmp);
		}
		trp->tr_rcv[i] = NULL;
		bdsp++;

	}
	/*
	 * report the number of packets
	 */
	UAS_ASSIGN(((struct atpBDS *)gbuf_rptr(m2))->bdsBuffSz, i);

	if (trp->tr_xmt) /* an ioctl block is still held? */
		gbuf_cont(trp->tr_xmt) = m2;
	else
		trp->tr_xmt = m2;

	if (m_head)
		*xm = m_head;
	else
		*xm = 0;

	dPrintf(D_M_ATP, D_L_INFO, ("             : size=%d\n",
		gbuf_msgsize(*xm)));

	return 0;
}

/*
 * The following routines are direct entries from system
 * calls to allow fast sending and recving of ATP data.
 */

int
_ATPsndreq(fd, buf, len, nowait, err, proc)
	int fd;
	unsigned char *buf;
	int len;
	int nowait;
	int *err;
	void *proc;
{
	gref_t *gref;
	int s, rc;
	unsigned short tid;
	unsigned int timer;
	register struct atp_state *atp;
	register struct atp_trans *trp;
	register ioc_t *iocbp;
	register at_atp_t *athp;
	register at_ddp_t *ddp;
	struct atp_set_default *sdb;
	gbuf_t *m2, *m, *mioc;
	char bds[atpBDSsize];

	if ((*err = atalk_getref(0, fd, &gref, proc)) != 0)
		return -1;

	if ((gref == 0) || ((atp = (struct atp_state *)gref->info) == 0)
			|| (atp->atp_flags & ATP_CLOSING)) {
		dPrintf(D_M_ATP, D_L_ERROR, ("ATPsndreq: stale handle=0x%x, pid=%d\n",
			(u_int) gref, gref->pid));

		*err = EINVAL;
		return -1;
	}

	while ((mioc = gbuf_alloc(sizeof(ioc_t), PRI_MED)) == 0) {
		ATDISABLE(s, atp->atp_delay_lock);
		rc = tsleep(&atp->atp_delay_event, PSOCK | PCATCH, "atpmioc", 10);
		ATENABLE(s, atp->atp_delay_lock);
		if (rc != 0) {
			*err = rc;
			return -1;
		}
		
	}
	gbuf_wset(mioc,sizeof(ioc_t));
	len -= atpBDSsize;
	while ((m2 = gbuf_alloc(len, PRI_MED)) == 0) {
		ATDISABLE(s, atp->atp_delay_lock);
		rc = tsleep(&atp->atp_delay_event, PSOCK | PCATCH, "atpm2", 10);
		ATENABLE(s, atp->atp_delay_lock);
		if (rc != 0) {
			gbuf_freeb(mioc);
			*err = rc;
			return -1;
		}
	}
	gbuf_wset(m2,len);
	gbuf_cont(mioc) = m2;
	if (((*err = copyin((caddr_t)buf, (caddr_t)bds, atpBDSsize)) != 0)
		|| ((*err = copyin((caddr_t)&buf[atpBDSsize],
			(caddr_t)gbuf_rptr(m2), len)) != 0)) {
		gbuf_freem(mioc);
		return -1;
	}
	gbuf_set_type(mioc, MSG_IOCTL);
	iocbp = (ioc_t *)gbuf_rptr(mioc);
	iocbp->ioc_count = len;
	iocbp->ioc_cmd = nowait ? AT_ATP_ISSUE_REQUEST_NOTE : AT_ATP_ISSUE_REQUEST;
	sdb = (struct atp_set_default *)gbuf_rptr(m2);

	/*
	 * The at_snd_req library routine multiplies seconds by 100.
	 * We need to divide by 100 in order to obtain the timer.
	 */
	if ((timer = (sdb->def_rate * HZ)/100) == 0)
	    timer = HZ;
	iocbp->ioc_count -= sizeof(struct atp_set_default);
	gbuf_rinc(m2,sizeof(struct atp_set_default));

	/*
	 * allocate and set up the transaction record
	 */
	while ((trp = atp_trans_alloc(atp)) == 0) {
		ATDISABLE(s, atp->atp_delay_lock);
		rc = tsleep(&atp->atp_delay_event, PSOCK | PCATCH, "atptrp", 10);
		ATENABLE(s, atp->atp_delay_lock);
		if (rc != 0) {
			gbuf_freem(mioc);
			*err = rc;
			return -1;
		}
	}
	trp->tr_retry = sdb->def_retries;
	trp->tr_timeout = timer;
	trp->tr_bdsp = NULL;
	trp->tr_tid = atp_tid(atp);
	tid = trp->tr_tid;

	/*
	 *	remember the IOCTL packet so we can ack it
	 *		later
	 */
	trp->tr_xmt = mioc;

	/*
	 *	Now fill in the header (and remember the bits
	 *		we need to know)
	 */
	athp = AT_ATP_HDR(m2);
	athp->cmd = ATP_CMD_TREQ;
	UAS_ASSIGN(athp->tid, trp->tr_tid);
	athp->eom = 0;
	athp->sts = 0;
	trp->tr_xo = athp->xo;
	trp->tr_bitmap = athp->bitmap;
	ddp = AT_DDP_HDR(m2);
	ddp->type = DDP_ATP;
	ddp->src_socket = (at_socket)atp->atp_socket_no;
	ddp->src_node = 0;
	trp->tr_socket.socket = ddp->dst_socket;
	trp->tr_socket.node = ddp->dst_node;
	trp->tr_socket.net = NET_VALUE(ddp->dst_net);
	trp->tr_local_socket = atp->atp_socket_no;

	/*
	 *	Put us in the transaction waiting queue
	 */
	ATDISABLE(s, atp->atp_lock);
	ATP_Q_APPEND(atp->atp_trans_wait, trp, tr_list);
	ATENABLE(s, atp->atp_lock);

	/*
	 * Send the message and set the timer	
	 */
	if ( !trp->tr_retry && !trp->tr_bitmap && !trp->tr_xo) { 
		m = (gbuf_t *)gbuf_copym(m2);
		atp_x_done(trp); /* no reason to tie up resources */
	} else {
		m = (gbuf_t *)gbuf_dupm(m2);
		atp_timout(atp_req_timeout, trp, trp->tr_timeout);
	}
	if (m)
		DDP_OUTPUT(m);

	if (nowait)
		return (int)tid;

	/*
	 * wait for the transaction to complete
	 */
	ATDISABLE(s, trp->tr_lock);
	while ((trp->tr_state != TRANS_DONE) && (trp->tr_state != TRANS_FAILED)) {
		trp->tr_rsp_wait = 1;
		rc = tsleep(&trp->tr_event, PSOCK | PCATCH, "atpsndreq", 0);
		if (rc != 0) {
			trp->tr_rsp_wait = 0;
			ATENABLE(s, trp->tr_lock);
			*err = rc;
			return -1;
		}
	}
	trp->tr_rsp_wait = 0;
	ATENABLE(s, trp->tr_lock);

	if (trp->tr_state == TRANS_FAILED) {
		/*
		 * transaction timed out, return error
		 */
		atp_free(trp);
		*err = ETIMEDOUT;
		return -1;
	}

	/*
	 * copy out the recv data
	 */
	atp_pack_bdsp(trp, bds);

	/*
	 * copyout the result info
	 */
	copyout((caddr_t)bds, (caddr_t)buf, atpBDSsize);

	atp_free(trp);

	return (int)tid;
} /* _ATPsndreq */

int
_ATPsndrsp(fd, respbuff, resplen, datalen, err, proc)
	int fd;
	unsigned char *respbuff;
	int resplen;
	int datalen;
	int *err;
	void *proc;
{
	gref_t *gref;
	int s, rc;
	long bufaddr;
	gbuf_t *m, *mdata;
	register short len;
	register int size;
	register struct atp_state *atp;
	register struct atpBDS *bdsp;
	register char *buf;

	if ((*err = atalk_getref(0, fd, &gref, proc)) != 0)
		return -1;

	if ((gref == 0) || ((atp = (struct atp_state *)gref->info) == 0)
			|| (atp->atp_flags & ATP_CLOSING)) {
		dPrintf(D_M_ATP, D_L_ERROR, ("ATPsndrsp: stale handle=0x%x, pid=%d\n",
			(u_int) gref, gref->pid));

		*err = EINVAL;
		return -1;
	}

	/*
	 * allocate buffer and copy in the response info
	 */
	while ((m = gbuf_alloc(resplen, PRI_MED)) == 0) {
		ATDISABLE(s, atp->atp_delay_lock);
		rc = tsleep(&atp->atp_delay_event, PSOCK | PCATCH, "atprspinfo", 10);
		ATENABLE(s, atp->atp_delay_lock);
		if (rc != 0) {
			*err = rc;
			return -1;
		}
	}
	if ((*err = copyin((caddr_t)respbuff, (caddr_t)gbuf_rptr(m), resplen)) != 0) {
		gbuf_freeb(m);
		return -1;
	}
	gbuf_wset(m,resplen);
	((at_ddp_t *)gbuf_rptr(m))->src_node = 0;
	bdsp = (struct atpBDS *)(gbuf_rptr(m) + TOTAL_ATP_HDR_SIZE);
	if ((resplen == TOTAL_ATP_HDR_SIZE) || ((len = UAS_VALUE(bdsp->bdsDataSz)) == 1))
		len = 0;
	else
		len = 16 * sizeof(gbuf_t);

	/*
	 * allocate buffer and copy in the response data
	 */
	while ((mdata = gbuf_alloc(datalen+len, PRI_MED)) == 0) {
		ATDISABLE(s, atp->atp_delay_lock);
		rc = tsleep(&atp->atp_delay_event, PSOCK | PCATCH, "atprspdata", 10);
		ATENABLE(s, atp->atp_delay_lock);
		if (rc != 0) {
			gbuf_freem(m);
			*err = rc;
			return -1;
		}
	}
	gbuf_cont(m) = mdata;
	for (size=0; bdsp < (struct atpBDS *)gbuf_wptr(m); bdsp++) {
		if ((bufaddr = UAL_VALUE(bdsp->bdsBuffAddr)) != 0) {
			len = UAS_VALUE(bdsp->bdsBuffSz);
			buf = (char *)bufaddr;
			if ((*err = copyin((caddr_t)buf,
					(caddr_t)&gbuf_rptr(mdata)[size], len)) != 0) {
				gbuf_freem(m);
				return -1;
			}
			size += len;
		}
	}
	gbuf_wset(mdata,size);

	atp_send_rsp(gref, m, TRUE);
	return 0;
}

int
_ATPgetreq(fd, buf, buflen, err, proc)
	int fd;
	unsigned char *buf;
	int buflen;
	int *err;
	void *proc;
{
	gref_t *gref;
	register struct atp_state *atp;
	register struct atp_rcb *rcbp;
	register gbuf_t *m, *m_head;
	int s, size, len;

	if ((*err = atalk_getref(0, fd, &gref, proc)) != 0)
		return -1;

	if ((gref == 0) || ((atp = (struct atp_state *)gref->info) == 0)
			|| (atp->atp_flags & ATP_CLOSING)) {
		dPrintf(D_M_ATP, D_L_ERROR, ("ATPgetreq: stale handle=0x%x, pid=%d\n",
			(u_int) gref, gref->pid));
		*err = EINVAL;
		return -1;
	}

	ATDISABLE(s, atp->atp_lock);
	if ((rcbp = atp->atp_attached.head) != NULL) {
	    /*
	     * Got one, move it to the active response Q
	     */
		m_head = rcbp->rc_ioctl;
		rcbp->rc_ioctl = NULL;

		if (rcbp->rc_xo) {
			ATP_Q_REMOVE(atp->atp_attached, rcbp, rc_list);
			rcbp->rc_state = RCB_NOTIFIED;
			ATP_Q_APPEND(atp->atp_rcb, rcbp, rc_list);
		} else {
			/* detach rcbp from attached queue,
			 * and free any outstanding resources
			 */
			atp_rcb_free(rcbp);
		}
		ATENABLE(s, atp->atp_lock);

		/*
		 * copyout the request data, including the protocol header
		 */
		for (size=0, m=m_head; m; m = gbuf_cont(m)) {
			if ((len = gbuf_len(m)) > buflen)
				len = buflen;
			copyout((caddr_t)gbuf_rptr(m), (caddr_t)&buf[size], len);
			size += len;
			if ((buflen -= len) == 0)
				break;
		}
		gbuf_freem(m_head);

		return size;
	}
	ATENABLE(s, atp->atp_lock);

	return -1;
}

int
_ATPgetrsp(fd, bdsp, err, proc)
	int fd;
	struct atpBDS *bdsp;
	int *err;
	void *proc;
{
	gref_t *gref;
	register struct atp_state *atp;
	register struct atp_trans *trp;
	int s, tid;
	char bds[atpBDSsize];

	if ((*err = atalk_getref(0, fd, &gref, proc)) != 0)
		return -1;

	if ((gref == 0) || ((atp = (struct atp_state *)gref->info) == 0)
			|| (atp->atp_flags & ATP_CLOSING)) {
		dPrintf(D_M_ATP, D_L_ERROR, ("ATPgetrsp: stale handle=0x%x, pid=%d\n",
			(u_int) gref, gref->pid));
		*err = EINVAL;
		return -1;
	}

	ATDISABLE(s, atp->atp_lock);
	for (trp = atp->atp_trans_wait.head; trp; trp = trp->tr_list.next) {
		dPrintf(D_M_ATP, D_L_INFO,
			("ATPgetrsp: atp:0x%x, trp:0x%x, state:%d\n",
			(u_int) atp, (u_int) trp, trp->tr_state));

		switch (trp->tr_state) {
		case TRANS_DONE:
	    	ATENABLE(s, atp->atp_lock);
			if ((*err = copyin((caddr_t)bdsp,
					(caddr_t)bds, sizeof(bds))) != 0)
				return -1;
			atp_pack_bdsp(trp, bds);
			tid = (int)trp->tr_tid;
			atp_free(trp);
			copyout((caddr_t)bds, (caddr_t)bdsp, sizeof(bds));
			return tid;

		case TRANS_FAILED:
			/*
			 * transaction timed out, return error
			 */
	    	ATENABLE(s, atp->atp_lock);
			atp_free(trp);
			*err = ETIMEDOUT;
			return -1;

		default:
			continue;
	    }
	}
	ATENABLE(s, atp->atp_lock);

	*err = EINVAL;
	return -1;
}

void
atp_drop_req(gref, m)
	gref_t *gref;
	gbuf_t *m;
{
	int s;
	struct atp_state *atp;
	struct atp_rcb *rcbp;
	at_atp_t *athp;
	at_ddp_t *ddp;

	atp = (struct atp_state *)gref->info;
	if (atp->dflag)
		atp = (struct atp_state *)atp->atp_msgq;
	ddp = AT_DDP_HDR(m);
	athp = AT_ATP_HDR(m);

	/*
	 *	search for the corresponding rcb
	 */
	ATDISABLE(s, atp->atp_lock);
	for (rcbp = atp->atp_rcb.head; rcbp; rcbp = rcbp->rc_list.next) {
	    if ( (rcbp->rc_tid == UAS_VALUE(athp->tid)) &&
			(rcbp->rc_socket.node == ddp->src_node) &&
			(rcbp->rc_socket.net == NET_VALUE(ddp->src_net)) &&
			(rcbp->rc_socket.socket == ddp->src_socket) )
		break;
	}

	/*
	 *	drop the request
	 */
	if (rcbp)
		atp_rcb_free(rcbp);
	ATENABLE(s, atp->atp_lock);

	gbuf_freem(m);
}
