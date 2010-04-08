require 'mkmf'

# FIXME: test for GCC __sync_XXX builtins here, somehow...
have_func('mmap', 'sys/mman.h') or abort 'mmap() not found'
have_func('munmap', 'sys/mman.h') or abort 'munmap() not found'

have_func("rb_struct_alloc_noinit")
have_func('rb_thread_blocking_region')

dir_config('raindrops')
create_makefile('raindrops_ext')
