#include "sereal.h"
#include <errno.h>
static void s_dump(sereal_t *s);

static inline void s_free_data_if_not_mine(sereal_t *s) {
        if (!(s->flags & FLAG_NOT_MINE)) {
            if (s->data)
                    free(s->data);
            s->data = NULL;
        }
}

static inline void s_reset_tracker(sereal_t *s) {
        if (s->tracked != Qnil)
                rb_gc_unregister_address(&s->tracked);
        s->tracked = Qnil;
}

static inline void s_destroy(sereal_t *s) {
        if (!s)
            return;

        s_reset_tracker(s);
        s_free_data_if_not_mine(s);
        free(s);
}

static inline void *s_realloc_or_raise(sereal_t *s, void *p, u32 n) {
        u8 *buf = realloc(p,n);
        if (!buf)
                s_raise(s,rb_eNoMemError,"memory allocation failure for %d bytes",n);
        return buf;
}
static inline void *s_alloc_or_raise(sereal_t *s,u32 n) {
        return s_realloc_or_raise(s,NULL,n);
}

static inline sereal_t * s_create(void) {
        sereal_t *s = s_alloc_or_raise(NULL,sizeof(*s));
	ZERO(s,sizeof(*s));
        s->tracked = Qnil;
        return s;
}

static inline void s_alloc(sereal_t *s, u32 len) {
        if (s->rsize > s->size + len)
                return;

        u32 size = s->size + len + 512; // every time allocate 512b more, so we wont alloc for a while
        u8 *buf = s_realloc_or_raise(s,s->data,size); 
        s->data = buf;
        s->rsize = size;
}

static inline void s_append(sereal_t *s, void *suffix, u32 s_len) {
        s_alloc(s,s_len);
        COPY(suffix,(s->data + s->size), s_len);
        s->size += s_len;
        s->pos += s_len;
}

static inline void s_append_u8(sereal_t *s,u8 b) {
        s_append(s,&b,sizeof(b));
}

static inline void s_append_u32(sereal_t *s,u32 b) {
        s_append(s,&b,sizeof(b));
}

static inline int s_read_stream(sereal_t *s, u32 end) {
         u32 pos = s->pos;
         while (s->size < end) {
                u32 req = end - s->size;
                u8 *buf = s_alloc_or_raise(s,s->size - req);
                int rc = read(s->fd,buf,req);
                if (rc <= 0) {
                        free(buf);
                        return -1;
                }
                s_append(s,buf,rc);
                free(buf);
        };
        s->pos = pos;
        return 1;
}

static inline void *s_get_p_at_pos(sereal_t *s, u32 pos,u32 req) {
        // returning s->data[pos], so we just make size count from 0
        if (pos + req >= s->size) {
                if (s->flags & FLAG_STREAM) {
                        if (s_read_stream(s,pos + req + 1) < 0) {
                                s_raise(s,rb_eRangeError,"stream request for %d bytes failed (err: %s)",
                                                         req,strerror(errno));
                        }
                } else {
                        u32 size = s->size;
                        s_raise(s,rb_eRangeError,"position is out of bounds (%d + %d >  %d)",pos,req,size);
                }
        }
        return &s->data[pos];
}

static inline void *s_get_p_at_pos_bang(sereal_t *s, u32 pos,u32 req) {
        void *p = s_get_p_at_pos(s,pos,req);
        s->pos += req;
        return p;
}

static inline void *s_end_p(sereal_t *s) {
       return s_get_p_at_pos(s,s->size - 1,0);
}
static inline void *s_get_p_req_inclusive(sereal_t *s, int req) {
       return s_get_p_at_pos(s,s->pos,req > 0 ? req - 1 : 0);
}
static inline void *s_get_p_req(sereal_t *s, int req) {
        return s_get_p_at_pos(s,s->pos,req);
}
static inline void *s_get_p(sereal_t *s) {
        return s_get_p_at_pos(s,s->pos,0);
}

static inline u8 s_get_u8(sereal_t *s) {
        return *((u8 *) s_get_p(s));
}

static inline u8 s_get_u8_bang(sereal_t *s) {
        u8 r = s_get_u8(s);
        s->pos++;
        return r;
}

static inline u32 s_get_u32_bang(sereal_t *s) {
        u32 *r = (u32 *) s_get_p_at_pos(s,s->pos,sizeof(u32) - 1); /* current position + 3 bytes */
        s->pos += sizeof(*r);
        return *r;
}

static inline float s_get_float_bang(sereal_t *s) {
        float *f = (float *) s_get_p_at_pos(s,s->pos,sizeof(*f) - 1);
        s->pos += sizeof(*f);
        return *f;
}

static inline double s_get_double_bang(sereal_t *s) {
        double *d = (double *) s_get_p_at_pos(s,s->pos,sizeof(*d) - 1);
        s->pos += sizeof(*d);
        return *d;
}

static inline long double s_get_long_double_bang(sereal_t *s) {
        long double *d = (long double *) s_get_p_at_pos(s,s->pos,sizeof(*d) - 1);
        s->pos += sizeof(*d);
        return *d;
}

static inline u32 s_shift_position_bang(sereal_t *s, u32 len) {
        s->pos += len;
        return len;
}

static void b_dump(u8 *p, u32 len, u32 pos) {
        int i;

        fprintf(stderr,"\n-----------\n");
        for (i = 0; i < len; i++) {
                if (i == pos) 
                        fprintf(stderr," [%c %d] ",p[i],p[i]);
                else
                        fprintf(stderr," (%c %d) ",p[i],p[i]);
        }
        fprintf(stderr,"\n-----------\n");
}

static void s_dump(sereal_t *s) {
        fprintf(stderr, "[ pos: %d, size: %d, rsize: %d ]\n",s->pos,s->size,s->rsize);
//        b_dump(s->data,s->size,s->pos);
}
static void s_shift_left(sereal_t *s) {
    u32 left = s->size - s->pos;
    if (left == 0) {
        free(s->data);
        s->data = NULL;
        s->size = 0;
        s->rsize = 0;
    } else {
        u8 *buf = s_alloc_or_raise(s,left);
        COPY(s->data + s->pos, buf, left);
        free(s->data);
        s->data = buf;
        s->size = left;
        s->rsize = left;
    }
}
