
/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#include <libsr.h>
#include <libsv.h>

typedef struct svmergeiter svmergeiter;

struct svmergeiter {
	srorder order;
	svmerge *merge;
	svmergesrc *src, *end;
	svmergesrc *v;
} srpacked;

static void
sv_mergeiter_init(sriter *i)
{
	assert(sizeof(svmergeiter) <= sizeof(i->priv));
	svmergeiter *im = (svmergeiter*)i->priv;
	memset(im, 0, sizeof(*im));
}

static void sv_mergeiter_next(sriter*);

static int
sv_mergeiter_open(sriter *i, va_list args)
{
	svmergeiter *im = (svmergeiter*)i->priv;
	im->merge = va_arg(args, svmerge*);
	im->order = va_arg(args, srorder);
	im->src   = (svmergesrc*)(im->merge->buf.s);
	im->end   = (svmergesrc*)(im->merge->buf.p);
	im->v     = NULL;
	sv_mergeiter_next(i);
	return 0;
}

static void
sv_mergeiter_close(sriter *i srunused)
{ }

static int
sv_mergeiter_has(sriter *i)
{
	svmergeiter *im = (svmergeiter*)i->priv;
	return im->v != NULL;
}

static void*
sv_mergeiter_of(sriter *i)
{
	svmergeiter *im = (svmergeiter*)i->priv;
	if (srunlikely(im->v == NULL))
		return NULL;
	return sr_iterof(&im->v->i);
}

static inline svmergesrc*
sv_mergeiter_nextsrc(svmergeiter *im, svmergesrc *src) {
	return (svmergesrc*)((char*)src + sizeof(svmergesrc) +
	                     im->merge->reserve);
}

static inline void
sv_mergeiter_dupset(svmergeiter *im)
{
	svmergesrc *v = im->src;
	while (v != im->end) {
		if (v->dup)
			svsetdup(sr_iterof(&v->i));
		v->dup = 0;
		v = sv_mergeiter_nextsrc(im, v);
	}
}

static inline void
sv_mergeiter_dupreset(svmergeiter *im, svmergesrc *pos)
{
	svmergesrc *v = im->src;
	while (v != pos) {
		v->dup = 0;
		v = sv_mergeiter_nextsrc(im, v);
	}
}

static void
sv_mergeiter_gt(sriter *it)
{
	svmergeiter *im = (svmergeiter*)it->priv;
	if (im->v) {
		sr_iternext(&im->v->i);
	}
	im->v = NULL;
	int dupn = 0;
	svmergesrc *min, *src;
	sv *minv;
	minv = NULL;
	min  = NULL;
	src  = im->src;
	while (src < im->end) {
		sv *v = sr_iterof(&src->i);
		if (v == NULL) {
			src = sv_mergeiter_nextsrc(im, src);
			continue;
		}
		if (min == NULL) {
			minv = v;
			min = src;
		} else {
			int rc = svcompare(minv, v, it->r->cmp);
			switch (rc) {
			case 0:
				assert(svlsn(v) < svlsn(minv));
				src->dup = 1;
				dupn++;
				break;
			case 1:
				minv = v;
				min = src;
				sv_mergeiter_dupreset(im, src);
				dupn = 0;
				break;
			}
		}
		src = sv_mergeiter_nextsrc(im, src);
	}
	if (srunlikely(min == NULL))
		return;
	im->v = min;
	if (dupn) {
		sv_mergeiter_dupset(im);
	}
}

static void
sv_mergeiter_lt(sriter *it)
{
	svmergeiter *im = (svmergeiter*)it->priv;
	if (im->v) {
		sr_iternext(&im->v->i);
	}
	im->v = NULL;
	int dupn = 0;
	svmergesrc *max, *src;
	sv *maxv;
	maxv = NULL;
	max  = NULL;
	src  = im->src;
	while (src < im->end) {
		sv *v = sr_iterof(&src->i);
		if (v == NULL) {
			src = sv_mergeiter_nextsrc(im, src);
			continue;
		}
		if (max == NULL) {
			maxv = v;
			max = src;
		} else {
			int rc = svcompare(maxv, v, it->r->cmp);
			switch (rc) {
			case  0:
				assert(svlsn(v) < svlsn(maxv));
				src->dup = 1;
				dupn++;
				break;
			case -1:
				maxv = v;
				max = src;
				sv_mergeiter_dupreset(im, src);
				dupn = 0;
				break;
			}
		}
		src = sv_mergeiter_nextsrc(im, src);
	}
	if (srunlikely(max == NULL))
		return;
	im->v = max;
	if (dupn) {
		sv_mergeiter_dupset(im);
	}
}

static void
sv_mergeiter_next(sriter *it)
{
	switch (((svmergeiter*)it->priv)->order) {
	case SR_RANDOM:
	case SR_GT:
	case SR_GTE:
		sv_mergeiter_gt(it);
		break;
	case SR_LT:
	case SR_LTE:
		sv_mergeiter_lt(it);
		break;
	default: assert(0);
	}
}

sriterif sv_mergeiter =
{
	.init    = sv_mergeiter_init,
	.open    = sv_mergeiter_open,
	.close   = sv_mergeiter_close,
	.has     = sv_mergeiter_has,
	.of      = sv_mergeiter_of,
	.next    = sv_mergeiter_next
};