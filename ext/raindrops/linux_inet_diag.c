#include <ruby.h>

/* Ruby 1.8.6+ macros (for compatibility with Ruby 1.9) */
#ifndef RSTRING_PTR
#  define RSTRING_PTR(s) (RSTRING(s)->ptr)
#endif
#ifndef RSTRING_LEN
#  define RSTRING_LEN(s) (RSTRING(s)->len)
#endif
#ifndef RSTRUCT_PTR
#  define RSTRUCT_PTR(s) (RSTRUCT(s)->ptr)
#endif
#ifndef RSTRUCT_LEN
#  define RSTRUCT_LEN(s) (RSTRUCT(s)->len)
#endif

#ifndef HAVE_RB_STRUCT_ALLOC_NOINIT
static ID id_new;
static VALUE rb_struct_alloc_noinit(VALUE class)
{
	return rb_funcall(class, id_new, 0, 0);
}
#endif /* !defined(HAVE_RB_STRUCT_ALLOC_NOINIT) */

/* partial emulation of the 1.9 rb_thread_blocking_region under 1.8 */
#ifndef HAVE_RB_THREAD_BLOCKING_REGION
#  include <rubysig.h>
#  define RUBY_UBF_IO ((rb_unblock_function_t *)-1)
typedef void rb_unblock_function_t(void *);
typedef VALUE rb_blocking_function_t(void *);
static VALUE
rb_thread_blocking_region(
	rb_blocking_function_t *func, void *data1,
	rb_unblock_function_t *ubf, void *data2)
{
	VALUE rv;

	TRAP_BEG;
	rv = func(data1);
	TRAP_END;

	return rv;
}
#endif /* ! HAVE_RB_THREAD_BLOCKING_REGION */

#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <asm/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/inet_diag.h>

static size_t page_size;
static unsigned g_seq;
static VALUE cListenStats;

struct my_addr {
	in_addr_t addr;
	uint16_t port;
};

struct listen_stats {
	long active;
	long queued;
};

#define OPLEN (sizeof(struct inet_diag_bc_op) + \
               sizeof(struct inet_diag_hostcond) + \
               sizeof(in_addr_t))

struct nogvl_args {
	struct iovec iov[3]; /* last iov holds inet_diag bytecode */
	struct my_addr query_addr;
	struct listen_stats stats;
};

/* creates a Ruby ListenStats Struct based on our internal listen_stats */
static VALUE rb_listen_stats(struct listen_stats *stats)
{
	VALUE rv = rb_struct_alloc_noinit(cListenStats);
	VALUE *ptr = RSTRUCT_PTR(rv);

	ptr[0] = LONG2NUM(stats->active);
	ptr[1] = LONG2NUM(stats->queued);

	return rv;
}

/*
 * converts a base 10 string representing a port number into
 * an unsigned 16 bit integer.  Raises ArgumentError on failure
 */
static uint16_t my_inet_port(const char *port)
{
	char *err;
	unsigned long tmp = strtoul(port, &err, 10);

	if (*err != 0 || tmp > 0xffff)
		rb_raise(rb_eArgError, "port not parsable: `%s'\n", port);

	return (uint16_t)tmp;
}

/* inner loop of inet_diag, called for every socket returned by netlink */
static inline void r_acc(struct nogvl_args *args, struct inet_diag_msg *r)
{
	/*
	 * inode == 0 means the connection is still in the listen queue
	 * and has not yet been accept()-ed by the server.  The
	 * inet_diag bytecode cannot filter this for us.
	 */
	if (r->idiag_inode == 0)
		return;
	if (r->idiag_state == TCP_ESTABLISHED)
		args->stats.active++;
	else /* if (r->idiag_state == TCP_LISTEN) */
		args->stats.queued = r->idiag_rqueue;
	/*
	 * we wont get anything else because of the idiag_states filter
	 */
}

static const char err_socket[] = "socket";
static const char err_sendmsg[] = "sendmsg";
static const char err_recvmsg[] = "recvmsg";
static const char err_nlmsg[] = "nlmsg";

/* does the inet_diag stuff with netlink(), this is called w/o GVL */
static VALUE diag(void *ptr)
{
	struct nogvl_args *args = ptr;
	struct sockaddr_nl nladdr;
	struct rtattr rta;
	struct {
		struct nlmsghdr nlh;
		struct inet_diag_req r;
	} req;
	struct msghdr msg;
	const char *err = NULL;
	unsigned seq = ++g_seq; /* not atomic, rely on GVL for now */
	int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_INET_DIAG);

	if (fd < 0)
		return (VALUE)err_socket;

	memset(&args->stats, 0, sizeof(struct listen_stats));

	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;

	memset(&req, 0, sizeof(req));
	req.nlh.nlmsg_len = sizeof(req) + RTA_LENGTH(args->iov[2].iov_len);
	req.nlh.nlmsg_type = TCPDIAG_GETSOCK;
	req.nlh.nlmsg_flags = NLM_F_ROOT | NLM_F_MATCH | NLM_F_REQUEST;
	req.nlh.nlmsg_pid = getpid();
	req.nlh.nlmsg_seq = seq;
	req.r.idiag_family = AF_INET;
	req.r.idiag_states = (1<<TCP_ESTABLISHED) | (1<<TCP_LISTEN);
	rta.rta_type = INET_DIAG_REQ_BYTECODE;
	rta.rta_len = RTA_LENGTH(args->iov[2].iov_len);

	args->iov[0].iov_base = &req;
	args->iov[0].iov_len = sizeof(req);
	args->iov[1].iov_base = &rta;
	args->iov[1].iov_len = sizeof(rta);

	memset(&msg, 0, sizeof(msg));
	msg.msg_name = (void *)&nladdr;
	msg.msg_namelen = sizeof(nladdr);
	msg.msg_iov = args->iov;
	msg.msg_iovlen = 3;

	if (sendmsg(fd, &msg, 0) < 0) {
		err = err_sendmsg;
		goto out;
	}

	/* reuse buffer that was allocated for bytecode */
	args->iov[0].iov_len = page_size;
	args->iov[0].iov_base = args->iov[2].iov_base;

	while (1) {
		ssize_t readed;
		struct nlmsghdr *h = (struct nlmsghdr *)args->iov[0].iov_base;

		memset(&msg, 0, sizeof(msg));
		msg.msg_name = (void *)&nladdr;
		msg.msg_namelen = sizeof(nladdr);
		msg.msg_iov = args->iov;
		msg.msg_iovlen = 1;

		readed = recvmsg(fd, &msg, 0);
		if (readed < 0) {
			if (errno == EINTR)
				continue;
			err = err_recvmsg;
			goto out;
		}
		if (readed == 0)
			goto out;

		for ( ; NLMSG_OK(h, readed); h = NLMSG_NEXT(h, readed)) {
			if (h->nlmsg_seq != seq)
				continue;
			if (h->nlmsg_type == NLMSG_DONE)
				goto out;
			if (h->nlmsg_type == NLMSG_ERROR) {
				err = err_nlmsg;
				goto out;
			}
			r_acc(args, NLMSG_DATA(h));
		}
	}
out:
	{
		int save_errno = errno;
		close(fd);
		errno = save_errno;
	}
	return (VALUE)err;
}

/* populates inet my_addr struct by parsing +addr+ */
static void parse_addr(struct my_addr *inet, VALUE addr)
{
	char *host_port, *colon;

	if (TYPE(addr) != T_STRING)
		rb_raise(rb_eArgError, "addrs must be an Array of Strings");

	host_port = RSTRING_PTR(addr);
	colon = memchr(host_port, ':', RSTRING_LEN(addr));
	if (!colon)
		rb_raise(rb_eArgError, "port not found in: `%s'", host_port);

	*colon = 0;
	inet->addr = inet_addr(host_port);
	*colon = ':';
	inet->port = htons(my_inet_port(colon + 1));
}

/* generates inet_diag bytecode to match a single addr */
static void gen_bytecode(struct iovec *iov, struct my_addr *inet)
{
	struct inet_diag_bc_op *op;
	struct inet_diag_hostcond *cond;

	/* iov_len was already set and base allocated in a parent function */
	assert(iov->iov_len == OPLEN && iov->iov_base && "iov invalid");
	op = iov->iov_base;
	op->code = INET_DIAG_BC_S_COND;
	op->yes = OPLEN;
	op->no = sizeof(struct inet_diag_bc_op) + OPLEN;

	cond = (struct inet_diag_hostcond *)(op + 1);
	cond->family = AF_INET;
	cond->port = ntohs(inet->port);
	cond->prefix_len = inet->addr == 0 ? 0 : sizeof(in_addr_t) * CHAR_BIT;
	*cond->addr = inet->addr;
}

static VALUE tcp_stats(struct nogvl_args *args, VALUE addr)
{
	const char *err;
	VALUE verr;

	parse_addr(&args->query_addr, addr);
	gen_bytecode(&args->iov[2], &args->query_addr);

	verr = rb_thread_blocking_region(diag, args, RUBY_UBF_IO, 0);
	err = (const char *)verr;
	if (err) {
		if (err == err_nlmsg)
			rb_raise(rb_eRuntimeError, "NLMSG_ERROR");
		else
			rb_sys_fail(err);
	}

	return rb_listen_stats(&args->stats);
}

/*
 * call-seq:
 *      addrs = %w(0.0.0.0:80 127.0.0.1:8080)
 *      Raindrops::Linux.tcp_listener_stats(addrs) => hash
 *
 * Takes an array of strings representing listen addresses to filter for.
 * Returns a hash with given addresses as keys and ListenStats
 * objects as the values.
 */
static VALUE tcp_listener_stats(VALUE obj, VALUE addrs)
{
	VALUE *ary;
	long i;
	VALUE rv;
	struct nogvl_args args;

	/*
	 * allocating page_size instead of OP_LEN since we'll reuse the
	 * buffer for recvmsg() later, we already checked for
	 * OPLEN <= page_size at initialization
	 */
	args.iov[2].iov_len = OPLEN;
	args.iov[2].iov_base = alloca(page_size);

	if (TYPE(addrs) != T_ARRAY)
		rb_raise(rb_eArgError, "addrs must be an Array or String");

	rv = rb_hash_new();
	ary = RARRAY_PTR(addrs);
	for (i = RARRAY_LEN(addrs); --i >= 0; ary++)
		rb_hash_aset(rv, *ary, tcp_stats(&args, *ary));

	return rv;
}

void Init_raindrops_linux_inet_diag(void)
{
	VALUE cRaindrops = rb_const_get(rb_cObject, rb_intern("Raindrops"));
	VALUE mLinux = rb_define_module_under(cRaindrops, "Linux");

	cListenStats = rb_const_get(cRaindrops, rb_intern("ListenStats"));

	rb_define_module_function(mLinux, "tcp_listener_stats",
	                          tcp_listener_stats, 1);

#ifndef HAVE_RB_STRUCT_ALLOC_NOINIT
	id_new = rb_intern("new");
#endif
	rb_require("raindrops/linux");

	page_size = getpagesize();

	assert(OPLEN <= page_size && "bytecode OPLEN is not <= PAGE_SIZE");
}
