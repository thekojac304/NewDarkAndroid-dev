/* Shared touch radial selector. SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_radial.h"
#include <math.h>
#include <string.h>

#define TAU 6.283185307f

void touch_radial_close(TouchRadialSelector *r) {
    r->open=false;
    r->owner=-1;
    r->count=0;
    r->highlighted=-1;
}
bool touch_radial_open(TouchRadialSelector *r, int owner, TouchRadialCategory category,
                       float width, float height, const TouchRadialEntry *entries, int count) {
    if (r->open || owner<0 || category<0 || category>=TOUCH_RADIAL_CATEGORY_COUNT ||
        width<=0 || height<=0 || count<0 || count>TOUCH_RADIAL_MAX_ENTRIES ||
        (count && !entries)) return false;
    r->open=true; r->owner=owner; r->category=category;
    r->cx=width*.5f; r->cy=height*.51f;
    /* NewDarkAndroid: more than 8 segments get a larger wheel, so each stays wide enough for a finger */
    r->outer_radius=fminf(width,height)*(count>8 ? .36f : .28f);
    r->inner_radius=r->outer_radius*.28f;
    r->count=count; r->highlighted=-1;
    r->span=TAU; r->start=count ? -TAU/2/(float)count : 0;   /* NewDarkAndroid: segment zero centred at the top */
    if (count) memcpy(r->entries,entries,(size_t)count*sizeof(*entries));
    return true;
}

/* ---------------------------------------------------------------- NewDarkAndroid Phase 3G: anchored wheels */
static bool whole(const TouchRadialSelector *r) { return r->span<=0 || r->span>=TAU-1e-4f; }
bool touch_radial_is_fan(const TouchRadialSelector *r) { return !whole(r); }
/* The widest run of directions (clockwise from up, sampled every 2 degrees) in which the rim at radius R stays at least
   m inside the screen. The screen is convex and the centre is on it, so a direction whose rim point is on the screen has
   its whole segment on it. Returns the arc (TAU: every direction) and its first direction in *start. */
static float visible_arc(float cx, float cy, float R, float w, float h, float m, float *start) {
    enum { N=180 };
    bool v[N], all=true;
    for (int i=0;i<N;++i) {
        float a=TAU*(float)i/N, x=cx+sinf(a)*R, y=cy-cosf(a)*R;
        v[i]=x>=m && x<=w-m && y>=m && y<=h-m;
        all=all && v[i];
    }
    *start=0;
    if (all) return TAU;
    int best=0,first=0;
    for (int i=0;i<N;++i) {
        if (!v[i] || v[(i+N-1)%N]) continue;                  /* only where a run begins */
        int n=0; while (n<N && v[(i+n)%N]) ++n;
        if (n>best) { best=n; first=i; }
    }
    if (best<2) return 0;
    *start=TAU*(float)first/N;
    return TAU*(float)(best-1)/N;                             /* both ends are sampled directions on the screen */
}
bool touch_radial_open_at(TouchRadialSelector *r, int owner, TouchRadialCategory category, float width, float height,
                          float anchor_x, float anchor_y, float finger_x, float finger_y,
                          const TouchRadialEntry *entries, int count) {
    if (!touch_radial_open(r,owner,category,width,height,entries,count)) return false;
    if (!isfinite(anchor_x) || !isfinite(anchor_y) || !isfinite(finger_x) || !isfinite(finger_y) || !count) return true;
    float side=fminf(width,height), m=side*.015f, base=r->outer_radius, cap=fmaxf(base,side*.45f);
    /* Centred exactly on the button, so the wheel grows out of the control being held. Its cancel circle holds the
       finger with room to spare, so lifting it at once never selects; at a button touching the screen edge, the part of
       that circle beyond the edge is simply not drawn (nothing can be touched there). */
    float inner=fmaxf(base*.28f,hypotf(finger_x-anchor_x,finger_y-anchor_y)+side*.03f);
    /* the segment width (arc at the rim) a fan aims for: a little over half of the centred wheel's, but never more than
       the quarter turn an entry may have (a fan of one or two entries does not grow) */
    float target=base*fminf(.55f*TAU/(float)count,TAU/4), best=-1, bR=0, bstart=0, bspan=0;
    for (int k=0;k<=5;++k) {
        float R=fminf(base*(1+.1f*(float)k),cap), start;
        float span=visible_arc(anchor_x,anchor_y,R,width,height,m,&start);
        if (span>=TAU) {                                      /* a whole wheel fits: segment zero at the top, as before */
            if (k==0) { best=1; bR=R; bstart=-TAU/2/(float)count; bspan=TAU; }
            break;                                            /* (a larger one never fits where this one did not) */
        }
        float used=fminf(span,(float)count*TAU/4);           /* at most a quarter turn per segment ... */
        float score=R*used/(float)count;
        if (score>best) { best=score; bR=R; bstart=start+(span-used)/2; bspan=used; }   /* ... centred in the arc */
        if (score>=target || R>=cap) break;
    }
    if (best<=0) return true;                                 /* nothing on the screen: the centred wheel (not reached) */
    r->cx=anchor_x; r->cy=anchor_y; r->outer_radius=bR; r->start=bstart; r->span=bspan; r->inner_radius=fminf(inner,bR*.6f);
    return true;
}
float touch_radial_segment_angle(const TouchRadialSelector *r, int segment) {
    int n=r->count>0 ? r->count : 1;
    float span=whole(r) ? TAU : r->span, start=r->span<=0 ? -TAU/2/(float)n : r->start;
    return start+span*((float)segment+.5f)/(float)n-TAU/4;   /* clockwise from up -> screen angle */
}
void touch_radial_segment_point(const TouchRadialSelector *r, int segment, float distance, float *x, float *y) {
    float a=touch_radial_segment_angle(r,segment);
    *x=r->cx+cosf(a)*distance; *y=r->cy+sinf(a)*distance;
}

int touch_radial_highlight(TouchRadialSelector *r, int owner, float x, float y) {
    if (!r->open || owner!=r->owner || !isfinite(x) || !isfinite(y)) return -1;
    float dx=x-r->cx,dy=y-r->cy,dist=hypotf(dx,dy);
    r->highlighted=-1;
    if (dist<r->inner_radius || dist>r->outer_radius*1.25f || !r->count) return -1;
    /* Segment zero begins at the top, proceeding clockwise. */
    float angle=atan2f(dx,-dy);
    if (angle<0) angle+=6.283185307f;
    int segment;
    if (whole(r)) {
        /* NewDarkAndroid: relative to where segment zero is centred (the top for touch_radial_open: unchanged) */
        float centre=r->span<=0 ? 0 : r->start+TAU/2/(float)r->count;
        angle-=centre; if (angle<0) angle+=TAU; if (angle>=TAU) angle-=TAU;
        segment=(int)floorf(angle*r->count/6.283185307f+.5f)%r->count;
    } else {
        /* NewDarkAndroid Phase 3G: a fan. Up to half a segment (at most 20 degrees) past either end still counts as
           that end's segment; further round, behind the fan, is nothing, as the centre is. */
        float rel=angle-r->start; while (rel<0) rel+=TAU; while (rel>=TAU) rel-=TAU;
        float each=r->span/(float)r->count, slack=fminf(each/2,.349f);
        if (rel<=r->span) segment=(int)fminf(floorf(rel/each),(float)(r->count-1));
        else if (rel-r->span<=slack) segment=r->count-1;
        else if (TAU-rel<=slack) segment=0;
        else return -1;
    }
    if (r->entries[segment].available) r->highlighted=segment;
    return r->highlighted;
}
int touch_radial_release(TouchRadialSelector *r, int owner, float x, float y) {
    if (!r->open || owner!=r->owner) return -1;
    int segment=touch_radial_highlight(r,owner,x,y);
    int id=segment>=0 ? r->entries[segment].id : -1;
    touch_radial_close(r);
    return id;
}
void touch_radial_refresh(TouchRadialSelector *r, const TouchRadialEntry *entries, int count) {
    if (!r->open || count<0 || count>TOUCH_RADIAL_MAX_ENTRIES || (count && !entries)) return;
    for (int i=0;i<r->count;++i) {
        TouchRadialEntry *old=&r->entries[i];
        old->available=false; old->current=false; old->count=0;
        for (int j=0;j<count;++j) if (entries[j].id==old->id) {
            old->available=entries[j].available;
            old->current=entries[j].current;
            old->count=entries[j].count;
            old->no_count=entries[j].no_count;
            break;
        }
    }
    if (r->highlighted>=0 && !r->entries[r->highlighted].available) r->highlighted=-1;
}
