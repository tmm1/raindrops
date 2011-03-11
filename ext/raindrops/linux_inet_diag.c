#include <ruby.h>
#ifdef __linux__

/* Ruby 1.8.6+ macros (for compatibility with Ruby 1.9) */
#ifndef RSTRING_PTR
#  define RSTRING_PTR(s) (RSTRING(s)->ptr)
#endif
#ifndef RSTRING_LEN
#  define RSTRING_LEN(s) (RSTRING(s)->len)
#endif

#include "rstruct_19.h"

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
#include <netdb.h>
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

struct listen_stats {
	long active;
	long queued;
};

#define OPLEN (sizeof(struct inet_diag_bc_op) + \
	       sizeof(struct inet_diag_hostcond) + \
	       sizeof(struct sockaddr_storage))

struct nogvl_args {
	struct iovec iov[3]; /* last iov holds inet_diag bytecode */
	struct sockaddr_storage query_addr;
	struct listen_stats stats;
};

/* creates a Ruby ListenStats Struct based on our internal listen_stats */
static VALUE rb_listen_stats(struct listen_stats *stats)
{
	VALUE rv = rb_struct_alloc_noinit(cListenStats);
	VALUE active = LONG2NUM(stats->active);
	VALUE queued = LONG2NUM(stats->queued);

#ifdef RSTRUCT_PTR
	VALUE *ptr = RSTRUCT_PTR(rv);
	ptr[0] = active;
	ptr[1] = queued;
#else /* Rubinius */
	rb_funcall(rv, rb_intern("active="), 1, active);
	rb_funcall(rv, rb_intern("queued="), 1, queued);
#endif /* ! Rubinius */
	return rv;
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

struct diag_req {
	struct nlmsghdr nlh;
	struct inet_diag_req r;
};

static void prep_msghdr(
	struct msghdr *msg,
	struct nogvl_args *args,
	struct sockaddr_nl *nladdr)
{
	memset(msg, 0, sizeof(struct msghdr));
	msg->msg_name = (void *)nladdr;
	msg->msg_namelen = sizeof(struct sockaddr_nl);
	msg->msg_iov = args->iov;
	msg->msg_iovlen = 3;
}

static void prep_diag_args(
	struct nogvl_args *args,
	struct sockaddr_nl *nladdr,
	struct rtattr *rta,
	struct diag_req *req,
	struct msghdr *msg)
{
	memset(&args->stats, 0, sizeof(struct listen_stats));
	memset(req, 0, sizeof(struct diag_req));
	memset(nladdr, 0, sizeof(struct sockaddr_nl));

	nladdr->nl_family = AF_NETLINK;

	req->nlh.nlmsg_len = sizeof(struct diag_req) +
	                    RTA_LENGTH(args->iov[2].iov_len);
	req->nlh.nlmsg_type = TCPDIAG_GETSOCK;
	req->nlh.nlmsg_flags = NLM_F_ROOT | NLM_F_MATCH | NLM_F_REQUEST;
	req->nlh.nlmsg_pid = getpid();
	req->r.idiag_states = (1<<TCP_ESTABLISHED) | (1<<TCP_LISTEN);
	rta->rta_type = INET_DIAG_REQ_BYTECODE;
	rta->rta_len = RTA_LENGTH(args->iov[2].iov_len);

	args->iov[0].iov_base = req;
	args->iov[0].iov_len = sizeof(struct diag_req);
	args->iov[1].iov_base = rta;
	args->iov[1].iov_len = sizeof(struct rtattr);

	prep_msghdr(msg, args, nladdr);
}

static void prep_recvmsg_buf(struct nogvl_args *args)
{
	/* reuse buffer that was allocated for bytecode */
	args->iov[0].iov_len = page_size;
	args->iov[0].iov_base = args->iov[2].iov_base;
}

/* does the inet_diag stuff with netlink(), this is called w/o GVL */
static VALUE diag(void *ptr)
{
	struct inet_diag_bc_op *op;
	struct inet_diag_hostcond *cond;
	struct nogvl_args *args = ptr;
	struct sockaddr_nl nladdr;
	struct rtattr rta;
	struct diag_req req;
	struct msghdr msg;
	const char *err = NULL;
	unsigned seq = ++g_seq;
	int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_INET_DIAG);

	if (fd < 0)
		return (VALUE)err_socket;

	prep_diag_args(args, &nladdr, &rta, &req, &msg);
	op = args->iov->iov_base;
	cond = (struct inet_diag_hostcond *)(op + 1);
	req.r.idiag_family = cond->family;
	req.nlh.nlmsg_seq = seq;

	if (sendmsg(fd, &msg, 0) < 0) {
		err = err_sendmsg;
		goto out;
	}

	prep_recvmsg_buf(args);

	while (1) {
		ssize_t readed;
		struct nlmsghdr *h = (struct nlmsghdr *)args->iov[0].iov_base;

		prep_msghdr(&msg, args, &nladdr);
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

/* populates sockaddr_storage struct by parsing +addr+ */
static void parse_addr(struct sockaddr_storage *inet, VALUE addr)
{
	char *host_ptr;
	char *colon = NULL;
	char *rbracket = NULL;
	long host_len;
	struct addrinfo hints;
	struct addrinfo *res;
	int rc;

	if (TYPE(addr) != T_STRING)
		rb_raise(rb_eArgError, "addrs must be an Array of Strings");

	host_ptr = StringValueCStr(addr);
	host_len = RSTRING_LEN(addr);
	if (*host_ptr == '[') { /* ipv6 address format (rfc2732) */
		rbracket = memchr(host_ptr + 1, ']', host_len - 1);

		if (rbracket) {
			if (rbracket[1] == ':') {
				colon = rbracket + 1;
				host_ptr++;
				*rbracket = 0;
			} else {
				rbracket = NULL;
			}
		}
	} else { /* ipv4 */
		colon = memchr(host_ptr, ':', host_len);
	}

	if (!colon)
		rb_raise(rb_eArgError, "port not found in: `%s'", host_ptr);

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

	*colon = 0;
	if (rbracket) *rbracket = 0;
	rc = getaddrinfo(host_ptr, colon + 1, &hints, &res);
	*colon = ':';
	if (rbracket) *rbracket = ']';
	if (rc != 0)
		rb_raise(rb_eArgError, "getaddrinfo(%s): %s",
			 host_ptr, gai_strerror(rc));

	memcpy(inet, res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res);
}

/* generates inet_diag bytecode to match a single addr */
static void gen_bytecode(struct iovec *iov, struct sockaddr_storage *inet)
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
	cond->family = inet->ss_family;
	switch (inet->ss_family) {
	case AF_INET: {
		struct sockaddr_in *in = (struct sockaddr_in *)inet;

		cond->port = ntohs(in->sin_port);
		cond->prefix_len = in->sin_addr.s_addr == 0 ? 0 :
				   sizeof(in->sin_addr.s_addr) * CHAR_BIT;
		*cond->addr = in->sin_addr.s_addr;
		}
		break;
	case AF_INET6: {
		struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)inet;

		cond->port = ntohs(in6->sin6_port);
		cond->prefix_len = memcmp(&in6addr_any, &in6->sin6_addr,
				          sizeof(struct in6_addr)) == 0 ?
				  0 : sizeof(in6->sin6_addr) * CHAR_BIT;
		memcpy(&cond->addr, &in6->sin6_addr, sizeof(struct in6_addr));
		}
		break;
	default:
		assert(0 && "unsupported address family, could that be IPv7?!");
	}
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
		rb_raise(rb_eArgError, "addrs must be an Array of Strings");

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
	page_size = getpagesize();

	assert(OPLEN <= page_size && "bytecode OPLEN is not <= PAGE_SIZE");
}
#endif /* __linux__ */
