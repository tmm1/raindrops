#ifdef RSTRUCT
#  ifndef RSTRUCT_PTR
#    define RSTRUCT_PTR(s) (RSTRUCT(s)->ptr)
#   endif
#  ifndef RSTRUCT_LEN
#    define RSTRUCT_LEN(s) (RSTRUCT(s)->len)
#  endif
#endif


#ifndef HAVE_RB_STRUCT_ALLOC_NOINIT
static ID id_new;
static VALUE rb_struct_alloc_noinit(VALUE class)
{
	return rb_funcall(class, id_new, 0, 0);
}
#endif /* !defined(HAVE_RB_STRUCT_ALLOC_NOINIT) */
