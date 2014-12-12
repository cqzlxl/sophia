
/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <libsr.h>
#include <libsv.h>
#include <libsd.h>

int sd_ssinit(sdss *s)
{
	sr_bufinit(&s->buf);
	return 0;
}

int sd_ssopen(sdss *s, sr *r, srbuf *buf)
{
	sdssheader *h = (sdssheader*)buf->s;
	uint32_t crc = sr_crcs(h, sr_bufused(buf), 0);
	if (srunlikely(crc != h->crc))
		return sr_error(r->e, "%s", "bad snapshot header crc");
	sr_buffree(&s->buf, r->a);
	s->buf = *buf;
	memset(buf, 0, sizeof(srbuf));
	return 0;
}

int sd_ssfree(sdss *s, sr *r)
{
	sr_buffree(&s->buf, r->a);
	return 0;
}

static int
sd_ssappend(sdss *s, sr *r, uint64_t lsn, char *name, int namelen)
{
	int create = 0;
	int sizerecord = sizeof(sdssrecord) + namelen;
	int size = sizerecord;
	if (sr_bufused(&s->buf) == 0) {
		size += sizeof(sdssheader);
		create = 1;
	}
	int rc = sr_bufensure(&s->buf, r->a, size);
	if (srunlikely(rc == -1))
		return sr_error(r->e, "%s", "memory allocation failed");
	sdssheader *h = (sdssheader*)s->buf.s;
	if (create) {
		h->count = 0;
		h->crc = 0;
		h->vlsn = 0;
		sr_bufadvance(&s->buf, sizeof(sdssheader));
	}
	sdssrecord *rp;
	rp = (sdssrecord*)s->buf.p;
	rp->lsn = lsn;
	rp->namelen = namelen;
	memcpy(rp->name, name, namelen);
	sr_bufadvance(&s->buf, sizerecord);
	if (rp->lsn < h->vlsn)
		h->vlsn = rp->lsn;
	h->count++;
	return 0;
}

static inline void
sd_sscrc(sdss *s)
{
	if (! sr_bufused(&s->buf))
		return;
	sdssheader *h = (sdssheader*)s->buf.s;
	h->crc = sr_crcs(h, sr_bufused(&s->buf), 0);
}

int sd_ssadd(sdss *s, sdss *n, sr *r, uint64_t lsn, char *name)
{
	sd_ssinit(n);
	int len = strlen(name);
	int rc;
	sdssrecord *rp;
	sriter i;
	sr_iterinit(&i, &sd_ssiter, r);
	sr_iteropen(&i, &s->buf, 0);
	for (; sr_iterhas(&i); sr_iternext(&i)) {
		rp = sr_iterof(&i);
		if (srunlikely(strncmp(rp->name, name, len) == 0)) {
			sd_ssfree(n, r);
			return -1;
		}
		rc = sd_ssappend(n, r, rp->lsn, rp->name, rp->namelen);
		if (srunlikely(rc == -1)) {
			sd_ssfree(n, r);
			return -1;
		}
	}
	rc = sd_ssappend(n, r, lsn, name, len + 1);
	if (srunlikely(rc == -1))
		return -1;
	sd_sscrc(n);
	return 0;
}

int sd_ssdelete(sdss *s, sdss *n, sr *r, char *name)
{
	sd_ssinit(n);
	int match = 0;
	int rc;
	sriter i;
	sr_iterinit(&i, &sd_ssiter, r);
	sr_iteropen(&i, &s->buf, 0);
	for (; sr_iterhas(&i); sr_iternext(&i)) {
		sdssrecord *rp = sr_iterof(&i);
		if (srunlikely(strncmp(rp->name, name, rp->namelen) == 0)) {
			match = 1;
		} else {
			rc = sd_ssappend(n, r, rp->lsn, rp->name, rp->namelen);
			if (srunlikely(rc == -1)) {
				sd_ssfree(n, r);
				return -1;
			}
		}
	}
	if (srunlikely(! match)) {
		sd_ssfree(n, r);
		return -1;
	}
	sd_sscrc(n);
	return 0;
}

uint64_t sd_ssvlsn(sdss *s)
{
	if (srunlikely(! sr_bufused(&s->buf)))
		return UINT64_MAX;
	sdssheader *h = (sdssheader*)s->buf.s;
	return h->vlsn;
}
