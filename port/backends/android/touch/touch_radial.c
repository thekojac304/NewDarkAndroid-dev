/* Shared touch radial selector. SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_radial.h"
#include <math.h>
#include <string.h>

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
    r->outer_radius=fminf(width*.28f,height*.28f);
    r->inner_radius=r->outer_radius*.28f;
    r->count=count; r->highlighted=-1;
    if (count) memcpy(r->entries,entries,(size_t)count*sizeof(*entries));
    return true;
}
int touch_radial_highlight(TouchRadialSelector *r, int owner, float x, float y) {
    if (!r->open || owner!=r->owner || !isfinite(x) || !isfinite(y)) return -1;
    float dx=x-r->cx,dy=y-r->cy,dist=hypotf(dx,dy);
    r->highlighted=-1;
    if (dist<r->inner_radius || dist>r->outer_radius*1.25f || !r->count) return -1;
    /* Segment zero begins at the top, proceeding clockwise. */
    float angle=atan2f(dx,-dy);
    if (angle<0) angle+=6.283185307f;
    int segment=(int)floorf(angle*r->count/6.283185307f+.5f)%r->count;
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
            break;
        }
    }
    if (r->highlighted>=0 && !r->entries[r->highlighted].available) r->highlighted=-1;
}
