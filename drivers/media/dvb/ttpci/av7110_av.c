/*
 * av7110_av.c: audio and video MPEG decoder stuff
 *
 * Copyright (C) 1999-2002 Ralph  Metzler
 *                       & Marcus Metzler for convergence integrated media GmbH
 *
 * originally based on code by:
 * Copyright (C) 1998,1999 Christian Theiss <mistert@rz.fh-augsburg.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 *
 *
 * the project's page is at http://www.linuxtv.org/dvb/
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/byteorder/swabb.h>
#include <linux/smp_lock.h>
#include <linux/fs.h>

#include "av7110.h"
#include "av7110_hw.h"
#include "av7110_av.h"
#include "av7110_ipack.h"

/* MPEG-2 (ISO 13818 / H.222.0) stream types */
#define PROG_STREAM_MAP  0xBC
#define PRIVATE_STREAM1  0xBD
#define PADDING_STREAM	 0xBE
#define PRIVATE_STREAM2  0xBF
#define AUDIO_STREAM_S	 0xC0
#define AUDIO_STREAM_E	 0xDF
#define VIDEO_STREAM_S	 0xE0
#define VIDEO_STREAM_E	 0xEF
#define ECM_STREAM	 0xF0
#define EMM_STREAM	 0xF1
#define DSM_CC_STREAM	 0xF2
#define ISO13522_STREAM  0xF3
#define PROG_STREAM_DIR  0xFF

#define PTS_DTS_FLAGS	 0xC0

//pts_dts flags
#define PTS_ONLY	 0x80
#define PTS_DTS		 0xC0
#define TS_SIZE		 188
#define TRANS_ERROR	 0x80
#define PAY_START	 0x40
#define TRANS_PRIO	 0x20
#define PID_MASK_HI	 0x1F
//flags
#define TRANS_SCRMBL1	 0x80
#define TRANS_SCRMBL2	 0x40
#define ADAPT_FIELD	 0x20
#define PAYLOAD		 0x10
#define COUNT_MASK	 0x0F

// adaptation flags
#define DISCON_IND	 0x80
#define RAND_ACC_IND	 0x40
#define ES_PRI_IND	 0x20
#define PCR_FLAG	 0x10
#define OPCR_FLAG	 0x08
#define SPLICE_FLAG	 0x04
#define TRANS_PRIV	 0x02
#define ADAP_EXT_FLAG	 0x01

// adaptation extension flags
#define LTW_FLAG	 0x80
#define PIECE_RATE	 0x40
#define SEAM_SPLICE	 0x20


static void p_to_t(u8 const *buf, long int length, u16 pid,
		   u8 *counter, struct dvb_demux_feed *feed);


int av7110_record_cb(struct dvb_filter_pes2ts *p2t, u8 *buf, size_t len)
{
	struct dvb_demux_feed *dvbdmxfeed = (struct dvb_demux_feed *) p2t->priv;

	if (!(dvbdmxfeed->ts_type & TS_PACKET))
		return 0;
	if (buf[3] == 0xe0)	 // video PES do not have a length in TS
		buf[4] = buf[5] = 0;
	if (dvbdmxfeed->ts_type & TS_PAYLOAD_ONLY)
		return dvbdmxfeed->cb.ts(buf, len, NULL, 0,
					 &dvbdmxfeed->feed.ts, DMX_OK);
	else
		return dvb_filter_pes2ts(p2t, buf, len, 1);
}

static int dvb_filter_pes2ts_cb(void *priv, unsigned char *data)
{
	struct dvb_demux_feed *dvbdmxfeed = (struct dvb_demux_feed *) priv;

	dvbdmxfeed->cb.ts(data, 188, NULL, 0,
			  &dvbdmxfeed->feed.ts, DMX_OK);
	return 0;
}

int av7110_av_start_record(struct av7110 *av7110, int av,
			   struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;

	dprintk(2, "av7110:%p, , dvb_demux_feed:%p\n", av7110, dvbdmxfeed);

	if (av7110->playing || (av7110->rec_mode & av))
		return -EBUSY;
	av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Stop, 0);
	dvbdmx->recording = 1;
	av7110->rec_mode |= av;

	switch (av7110->rec_mode) {
	case RP_AUDIO:
		dvb_filter_pes2ts_init(&av7110->p2t[0],
				       dvbdmx->pesfilter[0]->pid,
				       dvb_filter_pes2ts_cb,
				       (void *) dvbdmx->pesfilter[0]);
		av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Record, 2, AudioPES, 0);
		break;

	case RP_VIDEO:
		dvb_filter_pes2ts_init(&av7110->p2t[1],
				       dvbdmx->pesfilter[1]->pid,
				       dvb_filter_pes2ts_cb,
				       (void *) dvbdmx->pesfilter[1]);
		av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Record, 2, VideoPES, 0);
		break;

	case RP_AV:
		dvb_filter_pes2ts_init(&av7110->p2t[0],
				       dvbdmx->pesfilter[0]->pid,
				       dvb_filter_pes2ts_cb,
				       (void *) dvbdmx->pesfilter[0]);
		dvb_filter_pes2ts_init(&av7110->p2t[1],
				       dvbdmx->pesfilter[1]->pid,
				       dvb_filter_pes2ts_cb,
				       (void *) dvbdmx->pesfilter[1]);
		av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Record, 2, AV_PES, 0);
		break;
	}
	return 0;
}

int av7110_av_start_play(struct av7110 *av7110, int av)
{
	dprintk(2, "av7110:%p, \n", av7110);

	if (av7110->rec_mode)
		return -EBUSY;
	if (av7110->playing & av)
		return -EBUSY;

	av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Stop, 0);

	if (av7110->playing == RP_NONE) {
		av7110_ipack_reset(&av7110->ipack[0]);
		av7110_ipack_reset(&av7110->ipack[1]);
	}

	av7110->playing |= av;
	switch (av7110->playing) {
	case RP_AUDIO:
		av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Play, 2, AudioPES, 0);
		break;
	case RP_VIDEO:
		av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Play, 2, VideoPES, 0);
		av7110->sinfo = 0;
		break;
	case RP_AV:
		av7110->sinfo = 0;
		av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Play, 2, AV_PES, 0);
		break;
	}
	return av7110->playing;
}

void av7110_av_stop(struct av7110 *av7110, int av)
{
	dprintk(2, "av7110:%p, \n", av7110);

	if (!(av7110->playing & av) && !(av7110->rec_mode & av))
		return;

	av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Stop, 0);
	if (av7110->playing) {
		av7110->playing &= ~av;
		switch (av7110->playing) {
		case RP_AUDIO:
			av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Play, 2, AudioPES, 0);
			break;
		case RP_VIDEO:
			av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Play, 2, VideoPES, 0);
			break;
		case RP_NONE:
			av7110_set_vidmode(av7110, av7110->vidmode);
			break;
		}
	} else {
		av7110->rec_mode &= ~av;
		switch (av7110->rec_mode) {
		case RP_AUDIO:
			av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Record, 2, AudioPES, 0);
			break;
		case RP_VIDEO:
			av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Record, 2, VideoPES, 0);
			break;
		case RP_NONE:
			break;
		}
	}
}


int av7110_pes_play(void *dest, struct dvb_ringbuffer *buf, int dlen)
{
	int len;
	u32 sync;
	u16 blen;

	if (!dlen) {
		wake_up(&buf->queue);
		return -1;
	}
	while (1) {
		if ((len = dvb_ringbuffer_avail(buf)) < 6)
			return -1;
		sync =  DVB_RINGBUFFER_PEEK(buf, 0) << 24;
		sync |= DVB_RINGBUFFER_PEEK(buf, 1) << 16;
		sync |= DVB_RINGBUFFER_PEEK(buf, 2) << 8;
		sync |= DVB_RINGBUFFER_PEEK(buf, 3);

		if (((sync &~ 0x0f) == 0x000001e0) ||
		    ((sync &~ 0x1f) == 0x000001c0) ||
		    (sync == 0x000001bd))
			break;
		printk("resync\n");
		DVB_RINGBUFFER_SKIP(buf, 1);
	}
	blen =  DVB_RINGBUFFER_PEEK(buf, 4) << 8;
	blen |= DVB_RINGBUFFER_PEEK(buf, 5);
	blen += 6;
	if (len < blen || blen > dlen) {
		//printk("buffer empty - avail %d blen %u dlen %d\n", len, blen, dlen);
		wake_up(&buf->queue);
		return -1;
	}

	dvb_ringbuffer_read(buf, dest, (size_t) blen, 0);

	dprintk(2, "pread=0x%08lx, pwrite=0x%08lx\n",
	       (unsigned long) buf->pread, (unsigned long) buf->pwrite);
	wake_up(&buf->queue);
	return blen;
}


int av7110_set_volume(struct av7110 *av7110, int volleft, int volright)
{
	int err, vol, val, balance = 0;

	dprintk(2, "av7110:%p, \n", av7110);

	av7110->mixer.volume_left = volleft;
	av7110->mixer.volume_right = volright;

	switch (av7110->adac_type) {
	case DVB_ADAC_TI:
		volleft = (volleft * 256) / 1036;
		volright = (volright * 256) / 1036;
		if (volleft > 0x3f)
			volleft = 0x3f;
		if (volright > 0x3f)
			volright = 0x3f;
		if ((err = SendDAC(av7110, 3, 0x80 + volleft)))
			return err;
		return SendDAC(av7110, 4, volright);

	case DVB_ADAC_CRYSTAL:
		volleft = 127 - volleft / 2;
		volright = 127 - volright / 2;
		i2c_writereg(av7110, 0x20, 0x03, volleft);
		i2c_writereg(av7110, 0x20, 0x04, volright);
		return 0;

	case DVB_ADAC_MSP:
		vol  = (volleft > volright) ? volleft : volright;
		val	= (vol * 0x73 / 255) << 8;
		if (vol > 0)
		       balance = ((volright - volleft) * 127) / vol;
		msp_writereg(av7110, MSP_WR_DSP, 0x0001, balance << 8);
		msp_writereg(av7110, MSP_WR_DSP, 0x0000, val); /* loudspeaker */
		msp_writereg(av7110, MSP_WR_DSP, 0x0006, val); /* headphonesr */
		return 0;
	}
	return 0;
}

void av7110_set_vidmode(struct av7110 *av7110, int mode)
{
	dprintk(2, "av7110:%p, \n", av7110);

	av7110_fw_cmd(av7110, COMTYPE_ENCODER, LoadVidCode, 1, mode);

	if (!av7110->playing) {
		ChangePIDs(av7110, av7110->pids[DMX_PES_VIDEO],
			   av7110->pids[DMX_PES_AUDIO],
			   av7110->pids[DMX_PES_TELETEXT],
			   0, av7110->pids[DMX_PES_PCR]);
		av7110_fw_cmd(av7110, COMTYPE_PIDFILTER, Scan, 0);
	}
}


static int sw2mode[16] = {
	VIDEO_MODE_PAL, VIDEO_MODE_NTSC, VIDEO_MODE_NTSC, VIDEO_MODE_PAL,
	VIDEO_MODE_NTSC, VIDEO_MODE_NTSC, VIDEO_MODE_PAL, VIDEO_MODE_NTSC,
	VIDEO_MODE_PAL, VIDEO_MODE_PAL, VIDEO_MODE_PAL, VIDEO_MODE_PAL,
	VIDEO_MODE_PAL, VIDEO_MODE_PAL, VIDEO_MODE_PAL, VIDEO_MODE_PAL,
};

static void get_video_format(struct av7110 *av7110, u8 *buf, int count)
{
	int i;
	int hsize, vsize;
	int sw;
	u8 *p;

	dprintk(2, "av7110:%p, \n", av7110);

	if (av7110->sinfo)
		return;
	for (i = 7; i < count - 10; i++) {
		p = buf + i;
		if (p[0] || p[1] || p[2] != 0x01 || p[3] != 0xb3)
			continue;
		p += 4;
		hsize = ((p[1] &0xF0) >> 4) | (p[0] << 4);
		vsize = ((p[1] &0x0F) << 8) | (p[2]);
		sw = (p[3] & 0x0F);
		av7110_set_vidmode(av7110, sw2mode[sw]);
		dprintk(2, "playback %dx%d fr=%d\n", hsize, vsize, sw);
		av7110->sinfo = 1;
		break;
	}
}


/****************************************************************************
 * I/O buffer management and control
 ****************************************************************************/

static inline long aux_ring_buffer_write(struct dvb_ringbuffer *rbuf,
					 const char *buf, unsigned long count)
{
	unsigned long todo = count;
	int free;

	while (todo > 0) {
		if (dvb_ringbuffer_free(rbuf) < 2048) {
			if (wait_event_interruptible(rbuf->queue,
						     (dvb_ringbuffer_free(rbuf) >= 2048)))
				return count - todo;
		}
		free = dvb_ringbuffer_free(rbuf);
		if (free > todo)
			free = todo;
		dvb_ringbuffer_write(rbuf, buf, free);
		todo -= free;
		buf += free;
	}

	return count - todo;
}

static void play_video_cb(u8 *buf, int count, void *priv)
{
	struct av7110 *av7110 = (struct av7110 *) priv;
	dprintk(2, "av7110:%p, \n", av7110);

	if ((buf[3] & 0xe0) == 0xe0) {
		get_video_format(av7110, buf, count);
		aux_ring_buffer_write(&av7110->avout, buf, count);
	} else
		aux_ring_buffer_write(&av7110->aout, buf, count);
}

static void play_audio_cb(u8 *buf, int count, void *priv)
{
	struct av7110 *av7110 = (struct av7110 *) priv;
	dprintk(2, "av7110:%p, \n", av7110);

	aux_ring_buffer_write(&av7110->aout, buf, count);
}

#define FREE_COND (dvb_ringbuffer_free(&av7110->avout) >= 20 * 1024 && \
		   dvb_ringbuffer_free(&av7110->aout) >= 20 * 1024)

static ssize_t dvb_play(struct av7110 *av7110, const u8 __user *buf,
			unsigned long count, int nonblock, int type)
{
	unsigned long todo = count, n;
	dprintk(2, "av7110:%p, \n", av7110);

	if (!av7110->kbuf[type])
		return -ENOBUFS;

	if (nonblock && !FREE_COND)
		return -EWOULDBLOCK;

	while (todo > 0) {
		if (!FREE_COND) {
			if (nonblock)
				return count - todo;
			if (wait_event_interruptible(av7110->avout.queue,
						     FREE_COND))
				return count - todo;
		}
		n = todo;
		if (n > IPACKS * 2)
			n = IPACKS * 2;
		if (copy_from_user(av7110->kbuf[type], buf, n))
			return -EFAULT;
		av7110_ipack_instant_repack(av7110->kbuf[type], n,
					    &av7110->ipack[type]);
		todo -= n;
		buf += n;
	}
	return count - todo;
}

static ssize_t dvb_play_kernel(struct av7110 *av7110, const u8 *buf,
			unsigned long count, int nonblock, int type)
{
	unsigned long todo = count, n;
	dprintk(2, "av7110:%p, \n", av7110);

	if (!av7110->kbuf[type])
		return -ENOBUFS;

	if (nonblock && !FREE_COND)
		return -EWOULDBLOCK;

	while (todo > 0) {
		if (!FREE_COND) {
			if (nonblock)
				return count - todo;
			if (wait_event_interruptible(av7110->avout.queue,
						     FREE_COND))
				return count - todo;
		}
		n = todo;
		if (n > IPACKS * 2)
			n = IPACKS * 2;
		av7110_ipack_instant_repack(buf, n, &av7110->ipack[type]);
		todo -= n;
		buf += n;
	}
	return count - todo;
}

static ssize_t dvb_aplay(struct av7110 *av7110, const u8 __user *buf,
			 unsigned long count, int nonblock, int type)
{
	unsigned long todo = count, n;
	dprintk(2, "av7110:%p, \n", av7110);

	if (!av7110->kbuf[type])
		return -ENOBUFS;
	if (nonblock && dvb_ringbuffer_free(&av7110->aout) < 20 * 1024)
		return -EWOULDBLOCK;

	while (todo > 0) {
		if (dvb_ringbuffer_free(&av7110->aout) < 20 * 1024) {
			if (nonblock)
				return count - todo;
			if (wait_event_interruptible(av7110->aout.queue,
					(dvb_ringbuffer_free(&av7110->aout) >= 20 * 1024)))
				return count-todo;
		}
		n = todo;
		if (n > IPACKS * 2)
			n = IPACKS * 2;
		if (copy_from_user(av7110->kbuf[type], buf, n))
			return -EFAULT;
		av7110_ipack_instant_repack(av7110->kbuf[type], n,
					    &av7110->ipack[type]);
		todo -= n;
		buf += n;
	}
	return count - todo;
}

void av7110_p2t_init(struct av7110_p2t *p, struct dvb_demux_feed *feed)
{
	memset(p->pes, 0, TS_SIZE);
	p->counter = 0;
	p->pos = 0;
	p->frags = 0;
	if (feed)
		p->feed = feed;
}

static void clear_p2t(struct av7110_p2t *p)
{
	memset(p->pes, 0, TS_SIZE);
//	p->counter = 0;
	p->pos = 0;
	p->frags = 0;
}


static int find_pes_header(u8 const *buf, long int length, int *frags)
{
	int c = 0;
	int found = 0;

	*frags = 0;

	while (c < length - 3 && !found) {
		if (buf[c] == 0x00 && buf[c + 1] == 0x00 &&
		    buf[c + 2] == 0x01) {
			switch ( buf[c + 3] ) {
			case PROG_STREAM_MAP:
			case PRIVATE_STREAM2:
			case PROG_STREAM_DIR:
			case ECM_STREAM     :
			case EMM_STREAM     :
			case PADDING_STREAM :
			case DSM_CC_STREAM  :
			case ISO13522_STREAM:
			case PRIVATE_STREAM1:
			case AUDIO_STREAM_S ... AUDIO_STREAM_E:
			case VIDEO_STREAM_S ... VIDEO_STREAM_E:
				found = 1;
				break;

			default:
				c++;
				break;
			}
		} else
			c++;
	}
	if (c == length - 3 && !found) {
		if (buf[length - 1] == 0x00)
			*frags = 1;
		if (buf[length - 2] == 0x00 &&
		    buf[length - 1] == 0x00)
			*frags = 2;
		if (buf[length - 3] == 0x00 &&
		    buf[length - 2] == 0x00 &&
		    buf[length - 1] == 0x01)
			*frags = 3;
		return -1;
	}

	return c;
}

void av7110_p2t_write(u8 const *buf, long int length, u16 pid, struct av7110_p2t *p)
{
	int c, c2, l, add;
	int check, rest;

	c = 0;
	c2 = 0;
	if (p->frags){
		check = 0;
		switch(p->frags) {
		case 1:
			if (buf[c] == 0x00 && buf[c + 1] == 0x01) {
				check = 1;
				c += 2;
			}
			break;
		case 2:
			if (buf[c] == 0x01) {
				check = 1;
				c++;
			}
			break;
		case 3:
			check = 1;
		}
		if (check) {
			switch (buf[c]) {
			case PROG_STREAM_MAP:
			case PRIVATE_STREAM2:
			case PROG_STREAM_DIR:
			case ECM_STREAM     :
			case EMM_STREAM     :
			case PADDING_STREAM :
			case DSM_CC_STREAM  :
			case ISO13522_STREAM:
			case PRIVATE_STREAM1:
			case AUDIO_STREAM_S ... AUDIO_STREAM_E:
			case VIDEO_STREAM_S ... VIDEO_STREAM_E:
				p->pes[0] = 0x00;
				p->pes[1] = 0x00;
				p->pes[2] = 0x01;
				p->pes[3] = buf[c];
				p->pos = 4;
				memcpy(p->pes + p->pos, buf + c, (TS_SIZE - 4) - p->pos);
				c += (TS_SIZE - 4) - p->pos;
				p_to_t(p->pes, (TS_SIZE - 4), pid, &p->counter, p->feed);
				clear_p2t(p);
				break;

			default:
				c = 0;
				break;
			}
		}
		p->frags = 0;
	}

	if (p->pos) {
		c2 = find_pes_header(buf + c, length - c, &p->frags);
		if (c2 >= 0 && c2 < (TS_SIZE - 4) - p->pos)
			l = c2+c;
		else
			l = (TS_SIZE - 4) - p->pos;
		memcpy(p->pes + p->pos, buf, l);
		c += l;
		p->pos += l;
		p_to_t(p->pes, p->pos, pid, &p->counter, p->feed);
		clear_p2t(p);
	}

	add = 0;
	while (c < length) {
		c2 = find_pes_header(buf + c + add, length - c - add, &p->frags);
		if (c2 >= 0) {
			c2 += c + add;
			if (c2 > c){
				p_to_t(buf + c, c2 - c, pid, &p->counter, p->feed);
				c = c2;
				clear_p2t(p);
				add = 0;
			} else
				add = 1;
		} else {
			l = length - c;
			rest = l % (TS_SIZE - 4);
			l -= rest;
			p_to_t(buf + c, l, pid, &p->counter, p->feed);
			memcpy(p->pes, buf + c + l, rest);
			p->pos = rest;
			c = length;
		}
	}
}


static int write_ts_header2(u16 pid, u8 *counter, int pes_start, u8 *buf, u8 length)
{
	int i;
	int c = 0;
	int fill;
	u8 tshead[4] = { 0x47, 0x00, 0x00, 0x10 };

	fill = (TS_SIZE - 4) - length;
	if (pes_start)
		tshead[1] = 0x40;
	if (fill)
		tshead[3] = 0x30;
	tshead[1] |= (u8)((pid & 0x1F00) >> 8);
	tshead[2] |= (u8)(pid & 0x00FF);
	tshead[3] |= ((*counter)++ & 0x0F);
	memcpy(buf, tshead, 4);
	c += 4;

	if (fill) {
		buf[4] = fill - 1;
		c++;
		if (fill > 1) {
			buf[5] = 0x00;
			c++;
		}
		for (i = 6; i < fill + 4; i++) {
			buf[i] = 0xFF;
			c++;
		}
	}

	return c;
}


static void p_to_t(u8 const *buf, long int length, u16 pid, u8 *counter,
		   struct dvb_demux_feed *feed)
{
	int l, pes_start;
	u8 obuf[TS_SIZE];
	long c = 0;

	pes_start = 0;
	if (length > 3 &&
	     buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x01)
		switch (buf[3]) {
			case PROG_STREAM_MAP:
			case PRIVATE_STREAM2:
			case PROG_STREAM_DIR:
			case ECM_STREAM     :
			case EMM_STREAM     :
			case PADDING_STREAM :
			case DSM_CC_STREAM  :
			case ISO13522_STREAM:
			case PRIVATE_STREAM1:
			case AUDIO_STREAM_S ... AUDIO_STREAM_E:
			case VIDEO_STREAM_S ... VIDEO_STREAM_E:
				pes_start = 1;
				break;

			default:
				break;
		}

	while (c < length) {
		memset(obuf, 0, TS_SIZE);
		if (length - c >= (TS_SIZE - 4)){
			l = write_ts_header2(pid, counter, pes_start,
					     obuf, (TS_SIZE - 4));
			memcpy(obuf + l, buf + c, TS_SIZE - l);
			c += TS_SIZE - l;
		} else {
			l = write_ts_header2(pid, counter, pes_start,
					     obuf, length - c);
			memcpy(obuf + l, buf + c, TS_SIZE - l);
			c = length;
		}
		feed->cb.ts(obuf, 188, NULL, 0, &feed->feed.ts, DMX_OK);
		pes_start = 0;
	}
}


int av7110_write_to_decoder(struct dvb_demux_feed *feed, const u8 *buf, size_t len)
{
	struct dvb_demux *demux = feed->demux;
	struct av7110 *av7110 = (struct av7110 *) demux->priv;
	struct ipack *ipack = &av7110->ipack[feed->pes_type];

	dprintk(2, "av7110:%p, \n", av7110);

	switch (feed->pes_type) {
	case 0:
		if (av7110->audiostate.stream_source == AUDIO_SOURCE_MEMORY)
			return -EINVAL;
		break;
	case 1:
		if (av7110->videostate.stream_source == VIDEO_SOURCE_MEMORY)
			return -EINVAL;
		break;
	default:
		return -1;
	}

	if (!(buf[3] & 0x10)) /* no payload? */
		return -1;
	if (buf[1] & 0x40)
		av7110_ipack_flush(ipack);

	if (buf[3] & 0x20) {  /* adaptation field? */
		len -= buf[4] + 1;
		buf += buf[4] + 1;
		if (!len)
			return 0;
	}

	av7110_ipack_instant_repack(buf + 4, len - 4, &av7110->ipack[feed->pes_type]);
	return 0;
}



/******************************************************************************
 * Video MPEG decoder events
 ******************************************************************************/
void dvb_video_add_event(struct av7110 *av7110, struct video_event *event)
{
	struct dvb_video_events *events = &av7110->video_events;
	int wp;

	spin_lock_bh(&events->lock);

	wp = (events->eventw + 1) % MAX_VIDEO_EVENT;
	if (wp == events->eventr) {
		events->overflow = 1;
		events->eventr = (events->eventr + 1) % MAX_VIDEO_EVENT;
	}

	//FIXME: timestamp?
	memcpy(&events->events[events->eventw], event, sizeof(struct video_event));
	events->eventw = wp;

	spin_unlock_bh(&events->lock);

	wake_up_interruptible(&events->wait_queue);
}


static int dvb_video_get_event (struct av7110 *av7110, struct video_event *event, int flags)
{
	struct dvb_video_events *events = &av7110->video_events;

	if (events->overflow) {
		events->overflow = 0;
		return -EOVERFLOW;
	}
	if (events->eventw == events->eventr) {
		int ret;

		if (flags & O_NONBLOCK)
			return -EWOULDBLOCK;

		ret = wait_event_interruptible(events->wait_queue,
					       events->eventw != events->eventr);
		if (ret < 0)
			return ret;
	}

	spin_lock_bh(&events->lock);

	memcpy(event, &events->events[events->eventr],
	       sizeof(struct video_event));
	events->eventr = (events->eventr + 1) % MAX_VIDEO_EVENT;

	spin_unlock_bh(&events->lock);

	return 0;
}


/******************************************************************************
 * DVB device file operations
 ******************************************************************************/

static unsigned int dvb_video_poll(struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev = (struct dvb_device *) file->private_data;
	struct av7110 *av7110 = (struct av7110 *) dvbdev->priv;
	unsigned int mask = 0;

	dprintk(2, "av7110:%p, \n", av7110);

	if ((file->f_flags & O_ACCMODE) != O_RDONLY)
		poll_wait(file, &av7110->avout.queue, wait);

	poll_wait(file, &av7110->video_events.wait_queue, wait);

	if (av7110->video_events.eventw != av7110->video_events.eventr)
		mask = POLLPRI;

	if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
		if (av7110->playing) {
			if (FREE_COND)
				mask |= (POLLOUT | POLLWRNORM);
			} else /* if not playing: may play if asked for */
				mask |= (POLLOUT | POLLWRNORM);
	}

	return mask;
}

static ssize_t dvb_video_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = (struct dvb_device *) file->private_data;
	struct av7110 *av7110 = (struct av7110 *) dvbdev->priv;

	dprintk(2, "av7110:%p, \n", av7110);

	if ((file->f_flags & O_ACCMODE) == O_RDONLY)
		return -EPERM;

	if (av7110->videostate.stream_source != VIDEO_SOURCE_MEMORY)
		return -EPERM;

	return dvb_play(av7110, buf, count, file->f_flags & O_NONBLOCK, 1);
}

static unsigned int dvb_audio_poll(struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev = (struct dvb_device *) file->private_data;
	struct av7110 *av7110 = (struct av7110 *) dvbdev->priv;
	unsigned int mask = 0;

	dprintk(2, "av7110:%p, \n", av7110);

	poll_wait(file, &av7110->aout.queue, wait);

	if (av7110->playing) {
		if (dvb_ringbuffer_free(&av7110->aout) >= 20 * 1024)
			mask |= (POLLOUT | POLLWRNORM);
	} else /* if not playing: may play if asked for */
		mask = (POLLOUT | POLLWRNORM);

	return mask;
}

static ssize_t dvb_audio_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = (struct dvb_device *) file->private_data;
	struct av7110 *av7110 = (struct av7110 *) dvbdev->priv;

	dprintk(2, "av7110:%p, \n", av7110);

	if (av7110->audiostate.stream_source != AUDIO_SOURCE_MEMORY) {
		printk(KERN_ERR "not audio source memory\n");
		return -EPERM;
	}
	return dvb_aplay(av7110, buf, count, file->f_flags & O_NONBLOCK, 0);
}

static u8 iframe_header[] = { 0x00, 0x00, 0x01, 0xe0, 0x00, 0x00, 0x80, 0x00, 0x00 };

#define MIN_IFRAME 400000

static int play_iframe(struct av7110 *av7110, u8 __user *buf, unsigned int len, int nonblock)
{
	int i, n;

	dprintk(2, "av7110:%p, \n", av7110);

	if (!(av7110->playing & RP_VIDEO)) {
		if (av7110_av_start_play(av7110, RP_VIDEO) < 0)
			return -EBUSY;
	}

	/* setting n always > 1, fixes problems when playing stillframes
	   consisting of I- and P-Frames */
	n = MIN_IFRAME / len + 1;

	/* FIXME: nonblock? */
	dvb_play_kernel(av7110, iframe_header, sizeof(iframe_header), 0, 1);

	for (i = 0; i < n; i++)
		dvb_play(av7110, buf, len, 0, 1);

	av7110_ipack_flush(&av7110->ipack[1]);
	return 0;
}


static int dvb_video_ioctl(struct inode *inode, struct file *file,
			   unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = (struct dvb_device *) file->private_data;
	struct av7110 *av7110 = (struct av7110 *) dvbdev->priv;
	unsigned long arg = (unsigned long) parg;
	int ret = 0;

	dprintk(2, "av7110:%p, \n", av7110);

	if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
		if ( cmd != VIDEO_GET_STATUS && cmd != VIDEO_GET_EVENT &&
		     cmd != VIDEO_GET_SIZE ) {
			return -EPERM;
		}
	}

	switch (cmd) {
	case VIDEO_STOP:
		av7110->videostate.play_state = VIDEO_STOPPED;
		if (av7110->videostate.stream_source == VIDEO_SOURCE_MEMORY)
			av7110_av_stop(av7110, RP_VIDEO);
		else
			vidcom(av7110, VIDEO_CMD_STOP,
			       av7110->videostate.video_blank ? 0 : 1);
		av7110->trickmode = TRICK_NONE;
		break;

	case VIDEO_PLAY:
		av7110->trickmode = TRICK_NONE;
		if (av7110->videostate.play_state == VIDEO_FREEZED) {
			av7110->videostate.play_state = VIDEO_PLAYING;
			vidcom(av7110, VIDEO_CMD_PLAY, 0);
		}

		if (av7110->videostate.stream_source == VIDEO_SOURCE_MEMORY) {
			if (av7110->playing == RP_AV) {
				av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Stop, 0);
				av7110->playing &= ~RP_VIDEO;
			}
			av7110_av_start_play(av7110, RP_VIDEO);
			vidcom(av7110, VIDEO_CMD_PLAY, 0);
		} else {
			//av7110_av_stop(av7110, RP_VIDEO);
			vidcom(av7110, VIDEO_CMD_PLAY, 0);
		}
		av7110->videostate.play_state = VIDEO_PLAYING;
		break;

	case VIDEO_FREEZE:
		av7110->videostate.play_state = VIDEO_FREEZED;
		if (av7110->playing & RP_VIDEO)
			av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Pause, 0);
		else
			vidcom(av7110, VIDEO_CMD_FREEZE, 1);
		av7110->trickmode = TRICK_FREEZE;
		break;

	case VIDEO_CONTINUE:
		if (av7110->playing & RP_VIDEO)
			av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Continue, 0);
		vidcom(av7110, VIDEO_CMD_PLAY, 0);
		av7110->videostate.play_state = VIDEO_PLAYING;
		av7110->trickmode = TRICK_NONE;
		break;

	case VIDEO_SELECT_SOURCE:
		av7110->videostate.stream_source = (video_stream_source_t) arg;
		break;

	case VIDEO_SET_BLANK:
		av7110->videostate.video_blank = (int) arg;
		break;

	case VIDEO_GET_STATUS:
		memcpy(parg, &av7110->videostate, sizeof(struct video_status));
		break;

	case VIDEO_GET_EVENT:
		ret=dvb_video_get_event(av7110, parg, file->f_flags);
		break;

	case VIDEO_GET_SIZE:
		memcpy(parg, &av7110->video_size, sizeof(video_size_t));
		break;

	case VIDEO_SET_DISPLAY_FORMAT:
	{
		video_displayformat_t format = (video_displayformat_t) arg;
		u16 val = 0;

		switch (format) {
		case VIDEO_PAN_SCAN:
			val = VID_PAN_SCAN_PREF;
			break;

		case VIDEO_LETTER_BOX:
			val = VID_VC_AND_PS_PREF;
			break;

		case VIDEO_CENTER_CUT_OUT:
			val = VID_CENTRE_CUT_PREF;
			break;

		default:
			ret = -EINVAL;
		}
		if (ret < 0)
			break;
		av7110->videostate.video_format = format;
		ret = av7110_fw_cmd(av7110, COMTYPE_ENCODER, SetPanScanType,
				    1, (u16) val);
		break;
	}

	case VIDEO_SET_FORMAT:
		if (arg > 1) {
			ret = -EINVAL;
			break;
		}
		av7110->display_ar = arg;
		ret = av7110_fw_cmd(av7110, COMTYPE_ENCODER, SetMonitorType,
				    1, (u16) arg);
		break;

	case VIDEO_STILLPICTURE:
	{
		struct video_still_picture *pic =
			(struct video_still_picture *) parg;
		av7110->videostate.stream_source = VIDEO_SOURCE_MEMORY;
		dvb_ringbuffer_flush_spinlock_wakeup(&av7110->avout);
		ret = play_iframe(av7110, pic->iFrame, pic->size,
				  file->f_flags & O_NONBLOCK);
		break;
	}

	case VIDEO_FAST_FORWARD:
		//note: arg is ignored by firmware
		if (av7110->playing & RP_VIDEO)
			av7110_fw_cmd(av7110, COMTYPE_REC_PLAY,
				      __Scan_I, 2, AV_PES, 0);
		else
			vidcom(av7110, VIDEO_CMD_FFWD, arg);
		av7110->trickmode = TRICK_FAST;
		av7110->videostate.play_state = VIDEO_PLAYING;
		break;

	case VIDEO_SLOWMOTION:
		if (av7110->playing&RP_VIDEO) {
			av7110_fw_cmd(av7110, COMTYPE_REC_PLAY, __Slow, 2, 0, 0);
			vidcom(av7110, VIDEO_CMD_SLOW, arg);
		} else {
			vidcom(av7110, VIDEO_CMD_PLAY, 0);
			vidcom(av7110, VIDEO_CMD_STOP, 0);
			vidcom(av7110, VIDEO_CMD_SLOW, arg);
		}
		av7110->trickmode = TRICK_SLOW;
		av7110->videostate.play_state = VIDEO_PLAYING;
		break;

	case VIDEO_GET_CAPABILITIES:
		*(int *)parg = VIDEO_CAP_MPEG1 | VIDEO_CAP_MPEG2 |
			VIDEO_CAP_SYS | VIDEO_CAP_PROG;
		break;

	case VIDEO_CLEAR_BUFFER:
		dvb_ringbuffer_flush_spinlock_wakeup(&av7110->avout);
		av7110_ipack_reset(&av7110->ipack[1]);

		if (av7110->playing == RP_AV) {
			av7110_fw_cmd(av7110, COMTYPE_REC_PLAY,
				      __Play, 2, AV_PES, 0);
			if (av7110->trickmode == TRICK_FAST)
				av7110_fw_cmd(av7110, COMTYPE_REC_PLAY,
					      __Scan_I, 2, AV_PES, 0);
			if (av7110->trickmode == TRICK_SLOW) {
				av7110_fw_cmd(av7110, COMTYPE_REC_PLAY,
					      __Slow, 2, 0, 0);
				vidcom(av7110, VIDEO_CMD_SLOW, arg);
			}
			if (av7110->trickmode == TRICK_FREEZE)
				vidcom(av7110, VIDEO_CMD_STOP, 1);
		}
		break;

	case VIDEO_SET_STREAMTYPE:

		break;

	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}

static int dvb_audio_ioctl(struct inode *inode, struct file *file,
			   unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = (struct dvb_device *) file->private_data;
	struct av7110 *av7110 = (struct av7110 *) dvbdev->priv;
	unsigned long arg = (unsigned long) parg;
	int ret = 0;

	dprintk(2, "av7110:%p, \n", av7110);

	if (((file->f_flags & O_ACCMODE) == O_RDONLY) &&
	    (cmd != AUDIO_GET_STATUS))
		return -EPERM;

	switch (cmd) {
	case AUDIO_STOP:
		if (av7110->audiostate.stream_source == AUDIO_SOURCE_MEMORY)
			av7110_av_stop(av7110, RP_AUDIO);
		else
			audcom(av7110, AUDIO_CMD_MUTE);
		av7110->audiostate.play_state = AUDIO_STOPPED;
		break;

	case AUDIO_PLAY:
		if (av7110->audiostate.stream_source == AUDIO_SOURCE_MEMORY)
			av7110_av_start_play(av7110, RP_AUDIO);
		audcom(av7110, AUDIO_CMD_UNMUTE);
		av7110->audiostate.play_state = AUDIO_PLAYING;
		break;

	case AUDIO_PAUSE:
		audcom(av7110, AUDIO_CMD_MUTE);
		av7110->audiostate.play_state = AUDIO_PAUSED;
		break;

	case AUDIO_CONTINUE:
		if (av7110->audiostate.play_state == AUDIO_PAUSED) {
			av7110->audiostate.play_state = AUDIO_PLAYING;
			audcom(av7110, AUDIO_CMD_MUTE | AUDIO_CMD_PCM16);
		}
		break;

	case AUDIO_SELECT_SOURCE:
		av7110->audiostate.stream_source = (audio_stream_source_t) arg;
		break;

	case AUDIO_SET_MUTE:
	{
		audcom(av7110, arg ? AUDIO_CMD_MUTE : AUDIO_CMD_UNMUTE);
		av7110->audiostate.mute_state = (int) arg;
		break;
	}

	case AUDIO_SET_AV_SYNC:
		av7110->audiostate.AV_sync_state = (int) arg;
		audcom(av7110, arg ? AUDIO_CMD_SYNC_ON : AUDIO_CMD_SYNC_OFF);
		break;

	case AUDIO_SET_BYPASS_MODE:
		ret = -EINVAL;
		break;

	case AUDIO_CHANNEL_SELECT:
		av7110->audiostate.channel_select = (audio_channel_select_t) arg;

		switch(av7110->audiostate.channel_select) {
		case AUDIO_STEREO:
			audcom(av7110, AUDIO_CMD_STEREO);
			break;

		case AUDIO_MONO_LEFT:
			audcom(av7110, AUDIO_CMD_MONO_L);
			break;

		case AUDIO_MONO_RIGHT:
			audcom(av7110, AUDIO_CMD_MONO_R);
			break;

		default:
			ret = -EINVAL;
			break;
		}
		break;

	case AUDIO_GET_STATUS:
		memcpy(parg, &av7110->audiostate, sizeof(struct audio_status));
		break;

	case AUDIO_GET_CAPABILITIES:
		*(int *)parg = AUDIO_CAP_LPCM | AUDIO_CAP_MP1 | AUDIO_CAP_MP2;
		break;

	case AUDIO_CLEAR_BUFFER:
		dvb_ringbuffer_flush_spinlock_wakeup(&av7110->aout);
		av7110_ipack_reset(&av7110->ipack[0]);
		if (av7110->playing == RP_AV)
			av7110_fw_cmd(av7110, COMTYPE_REC_PLAY,
			       __Play, 2, AV_PES, 0);
		break;
	case AUDIO_SET_ID:

		break;
	case AUDIO_SET_MIXER:
	{
		struct audio_mixer *amix = (struct audio_mixer *)parg;

		av7110_set_volume(av7110, amix->volume_left, amix->volume_right);
		break;
	}
	case AUDIO_SET_STREAMTYPE:
		break;
	default:
		ret = -ENOIOCTLCMD;
	}
	return ret;
}


static int dvb_video_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = (struct dvb_device *) file->private_data;
	struct av7110 *av7110 = (struct av7110 *) dvbdev->priv;
	int err;

	dprintk(2, "av7110:%p, \n", av7110);

	if ((err = dvb_generic_open(inode, file)) < 0)
		return err;

	if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
		dvb_ringbuffer_flush_spinlock_wakeup(&av7110->aout);
		dvb_ringbuffer_flush_spinlock_wakeup(&av7110->avout);
		av7110->video_blank = 1;
		av7110->audiostate.AV_sync_state = 1;
		av7110->videostate.stream_source = VIDEO_SOURCE_DEMUX;

		/*  empty event queue */
		av7110->video_events.eventr = av7110->video_events.eventw = 0;
	}

	return 0;
}

static int dvb_video_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = (struct dvb_device *) file->private_data;
	struct av7110 *av7110 = (struct av7110 *) dvbdev->priv;

	dprintk(2, "av7110:%p, \n", av7110);

	if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
		av7110_av_stop(av7110, RP_VIDEO);
	}

	return dvb_generic_release(inode, file);
}

static int dvb_audio_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = (struct dvb_device *) file->private_data;
	struct av7110 *av7110 = (struct av7110 *) dvbdev->priv;
	int err=dvb_generic_open(inode, file);

	dprintk(2, "av7110:%p, \n", av7110);

	if (err < 0)
		return err;
	dvb_ringbuffer_flush_spinlock_wakeup(&av7110->aout);
	av7110->audiostate.stream_source = AUDIO_SOURCE_DEMUX;
	return 0;
}

static int dvb_audio_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = (struct dvb_device *) file->private_data;
	struct av7110 *av7110 = (struct av7110 *) dvbdev->priv;

	dprintk(2, "av7110:%p, \n", av7110);

	av7110_av_stop(av7110, RP_AUDIO);
	return dvb_generic_release(inode, file);
}



/******************************************************************************
 * driver registration
 ******************************************************************************/

static struct file_operations dvb_video_fops = {
	.owner		= THIS_MODULE,
	.write		= dvb_video_write,
	.ioctl		= dvb_generic_ioctl,
	.open		= dvb_video_open,
	.release	= dvb_video_release,
	.poll		= dvb_video_poll,
};

static struct dvb_device dvbdev_video = {
	.priv		= NULL,
	.users		= 6,
	.readers	= 5,	/* arbitrary */
	.writers	= 1,
	.fops		= &dvb_video_fops,
	.kernel_ioctl	= dvb_video_ioctl,
};

static struct file_operations dvb_audio_fops = {
	.owner		= THIS_MODULE,
	.write		= dvb_audio_write,
	.ioctl		= dvb_generic_ioctl,
	.open		= dvb_audio_open,
	.release	= dvb_audio_release,
	.poll		= dvb_audio_poll,
};

static struct dvb_device dvbdev_audio = {
	.priv		= NULL,
	.users		= 1,
	.writers	= 1,
	.fops		= &dvb_audio_fops,
	.kernel_ioctl	= dvb_audio_ioctl,
};


int av7110_av_register(struct av7110 *av7110)
{
	av7110->audiostate.AV_sync_state = 0;
	av7110->audiostate.mute_state = 0;
	av7110->audiostate.play_state = AUDIO_STOPPED;
	av7110->audiostate.stream_source = AUDIO_SOURCE_DEMUX;
	av7110->audiostate.channel_select = AUDIO_STEREO;
	av7110->audiostate.bypass_mode = 0;

	av7110->videostate.video_blank = 0;
	av7110->videostate.play_state = VIDEO_STOPPED;
	av7110->videostate.stream_source = VIDEO_SOURCE_DEMUX;
	av7110->videostate.video_format = VIDEO_FORMAT_4_3;
	av7110->videostate.display_format = VIDEO_CENTER_CUT_OUT;
	av7110->display_ar = VIDEO_FORMAT_4_3;

	init_waitqueue_head(&av7110->video_events.wait_queue);
	spin_lock_init(&av7110->video_events.lock);
	av7110->video_events.eventw = av7110->video_events.eventr = 0;
	av7110->video_events.overflow = 0;
	memset(&av7110->video_size, 0, sizeof (video_size_t));

	dvb_register_device(av7110->dvb_adapter, &av7110->video_dev,
			    &dvbdev_video, av7110, DVB_DEVICE_VIDEO);

	dvb_register_device(av7110->dvb_adapter, &av7110->audio_dev,
			    &dvbdev_audio, av7110, DVB_DEVICE_AUDIO);

	return 0;
}

void av7110_av_unregister(struct av7110 *av7110)
{
	dvb_unregister_device(av7110->audio_dev);
	dvb_unregister_device(av7110->video_dev);
}

int av7110_av_init(struct av7110 *av7110)
{
	av7110->vidmode = VIDEO_MODE_PAL;

	av7110_ipack_init(&av7110->ipack[0], IPACKS, play_audio_cb);
	av7110->ipack[0].data = (void *) av7110;
	av7110_ipack_init(&av7110->ipack[1], IPACKS, play_video_cb);
	av7110->ipack[1].data = (void *) av7110;

	dvb_ringbuffer_init(&av7110->avout, av7110->iobuf, AVOUTLEN);
	dvb_ringbuffer_init(&av7110->aout, av7110->iobuf + AVOUTLEN, AOUTLEN);

	av7110->kbuf[0] = (u8 *)(av7110->iobuf + AVOUTLEN + AOUTLEN + BMPLEN);
	av7110->kbuf[1] = av7110->kbuf[0] + 2 * IPACKS;

	return 0;
}

int av7110_av_exit(struct av7110 *av7110)
{
	av7110_ipack_free(&av7110->ipack[0]);
	av7110_ipack_free(&av7110->ipack[1]);
	return 0;
}