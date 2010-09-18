#include <ruby.h>
#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include "raindrops_atomic.h"

#ifndef SIZET2NUM
#  define SIZET2NUM(x) ULONG2NUM(x)
#endif

/*
 * most modern CPUs have a cache-line size of 64 or 128.
 * We choose a bigger one by default since our structure is not
 * heavily used
 */
static size_t raindrop_size = 128;

/* each raindrop is a counter */
struct raindrop {
	unsigned long counter;
} __attribute__((packed));

/* allow mmap-ed regions can store more than one raindrop */
struct raindrops {
	long size;
	struct raindrop *drops;
};

/* called by GC */
static void evaporate(void *ptr)
{
	struct raindrops *r = ptr;

	if (r->drops) {
		int rv = munmap(r->drops, raindrop_size * r->size);
		if (rv != 0)
			rb_bug("munmap failed in gc: %s", strerror(errno));
	}

	xfree(ptr);
}

/* automatically called at creation (before initialize) */
static VALUE alloc(VALUE klass)
{
	struct raindrops *r;

	return Data_Make_Struct(klass, struct raindrops, NULL, evaporate, r);
}

static struct raindrops *get(VALUE self)
{
	struct raindrops *r;

	Data_Get_Struct(self, struct raindrops, r);

	return r;
}

/* initializes a Raindrops object to hold +size+ elements */
static VALUE init(VALUE self, VALUE size)
{
	struct raindrops *r = get(self);
	int tries = 1;

	if (r->drops)
		rb_raise(rb_eRuntimeError, "already initialized");

	r->size = NUM2LONG(size);
	if (r->size < 1)
		rb_raise(rb_eArgError, "size must be >= 1");

retry:
	r->drops = mmap(NULL, raindrop_size * r->size,
	                PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);
	if (r->drops == MAP_FAILED) {
		r->drops = NULL;
		if ((errno == EAGAIN || errno == ENOMEM) && tries-- > 0) {
			rb_gc();
			goto retry;
		}
		rb_sys_fail("mmap");
	}

	return self;
}

/* :nodoc */
static VALUE init_copy(VALUE dest, VALUE source)
{
	struct raindrops *dst = get(dest);
	struct raindrops *src = get(source);

	init(dest, LONG2NUM(src->size));
	memcpy(dst->drops, src->drops, raindrop_size * src->size);

	return dest;
}

static unsigned long *addr_of(VALUE self, VALUE index)
{
	struct raindrops *r = get(self);
	unsigned long off = FIX2ULONG(index) * raindrop_size;

	if (off >= raindrop_size * r->size)
		rb_raise(rb_eArgError, "offset overrun");

	return (unsigned long *)((unsigned long)r->drops + off);
}

static unsigned long incr_decr_arg(int argc, const VALUE *argv)
{
	if (argc > 2 || argc < 1)
		rb_raise(rb_eArgError,
		         "wrong number of arguments (%d for 1+)", argc);

	return argc == 2 ? NUM2ULONG(argv[1]) : 1;
}

/* increments the value referred to by the +index+ constant by 1 */
static VALUE incr(int argc, VALUE *argv, VALUE self)
{
	unsigned long nr = incr_decr_arg(argc, argv);

	return ULONG2NUM(__sync_add_and_fetch(addr_of(self, argv[0]), nr));
}

/* decrements the value referred to by the +index+ constant by 1 */
static VALUE decr(int argc, VALUE *argv, VALUE self)
{
	unsigned long nr = incr_decr_arg(argc, argv);

	return ULONG2NUM(__sync_sub_and_fetch(addr_of(self, argv[0]), nr));
}

/* converts the raindrops structure to an Array */
static VALUE to_ary(VALUE self)
{
	struct raindrops *r = get(self);
	VALUE rv = rb_ary_new2(r->size);
	long i;
	unsigned long base = (unsigned long)r->drops;

	for (i = 0; i < r->size; i++) {
		rb_ary_push(rv, ULONG2NUM(*((unsigned long *)base)));
		base += raindrop_size;
	}

	return rv;
}

static VALUE size(VALUE self)
{
	return LONG2NUM(get(self)->size);
}

static VALUE aset(VALUE self, VALUE index, VALUE value)
{
	unsigned long *addr = addr_of(self, index);

	*addr = NUM2ULONG(value);

	return value;
}

static VALUE aref(VALUE self, VALUE index)
{
	return  ULONG2NUM(*addr_of(self, index));
}

#ifdef __linux__
void Init_raindrops_linux_inet_diag(void);
#endif

#ifndef _SC_NPROCESSORS_ONLN
#  ifdef _SC_NPROC_ONLN
#    define _SC_NPROCESSORS_ONLN _SC_NPROC_ONLN
#  elif defined _SC_CRAY_NCPU
#    define _SC_NPROCESSORS_ONLN _SC_CRAY_NCPU
#  endif
#endif

void Init_raindrops_ext(void)
{
	VALUE cRaindrops = rb_define_class("Raindrops", rb_cObject);
	long tmp = 2;

#ifdef _SC_NPROCESSORS_ONLN
	tmp = sysconf(_SC_NPROCESSORS_ONLN);
#endif
	/* no point in padding on single CPU machines */
	if (tmp == 1)
		raindrop_size = sizeof(unsigned long);
#ifdef _SC_LEVEL1_DCACHE_LINESIZE
	if (tmp != 1) {
		tmp = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
		if (tmp > 0)
			raindrop_size = (size_t)tmp;
	}
#endif
	rb_define_const(cRaindrops, "SIZE", SIZET2NUM(raindrop_size));

	rb_define_alloc_func(cRaindrops, alloc);

	rb_define_method(cRaindrops, "initialize", init, 1);
	rb_define_method(cRaindrops, "incr", incr, -1);
	rb_define_method(cRaindrops, "decr", decr, -1);
	rb_define_method(cRaindrops, "to_ary", to_ary, 0);
	rb_define_method(cRaindrops, "[]", aref, 1);
	rb_define_method(cRaindrops, "[]=", aset, 2);
	rb_define_method(cRaindrops, "size", size, 0);
	rb_define_method(cRaindrops, "initialize_copy", init_copy, 1);

#ifdef __linux__
	Init_raindrops_linux_inet_diag();
#endif
}
