#include <ruby.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#ifdef TCP_INFO
#include "my_fileno.h"

#define TCPI_ATTR_READER(x) \
static VALUE tcp_info_##x(VALUE self) \
{ \
	struct tcp_info *info = DATA_PTR(self); \
	return UINT2NUM((uint32_t)info->tcpi_##x); \
}

TCPI_ATTR_READER(state)
TCPI_ATTR_READER(ca_state)
TCPI_ATTR_READER(retransmits)
TCPI_ATTR_READER(probes)
TCPI_ATTR_READER(backoff)
TCPI_ATTR_READER(options)
TCPI_ATTR_READER(snd_wscale)
TCPI_ATTR_READER(rcv_wscale)
TCPI_ATTR_READER(rto)
TCPI_ATTR_READER(ato)
TCPI_ATTR_READER(snd_mss)
TCPI_ATTR_READER(rcv_mss)
TCPI_ATTR_READER(unacked)
TCPI_ATTR_READER(sacked)
TCPI_ATTR_READER(lost)
TCPI_ATTR_READER(retrans)
TCPI_ATTR_READER(fackets)
TCPI_ATTR_READER(last_data_sent)
TCPI_ATTR_READER(last_ack_sent)
TCPI_ATTR_READER(last_data_recv)
TCPI_ATTR_READER(last_ack_recv)
TCPI_ATTR_READER(pmtu)
TCPI_ATTR_READER(rcv_ssthresh)
TCPI_ATTR_READER(rtt)
TCPI_ATTR_READER(rttvar)
TCPI_ATTR_READER(snd_ssthresh)
TCPI_ATTR_READER(snd_cwnd)
TCPI_ATTR_READER(advmss)
TCPI_ATTR_READER(reordering)
TCPI_ATTR_READER(rcv_rtt)
TCPI_ATTR_READER(rcv_space)
TCPI_ATTR_READER(total_retrans)

static VALUE alloc(VALUE klass)
{
	struct tcp_info *info = xmalloc(sizeof(struct tcp_info));

	/* Data_Make_Struct has an extra memset 0 which is so wasteful */
	return Data_Wrap_Struct(klass, NULL, -1, info);
}

static VALUE init(VALUE self, VALUE io)
{
	int fd = my_fileno(io);
	struct tcp_info *info = DATA_PTR(self);
	socklen_t len = (socklen_t)sizeof(struct tcp_info);
	int rc = getsockopt(fd, IPPROTO_TCP, TCP_INFO, info, &len);

	if (rc != 0)
		rb_sys_fail("getsockopt");

	return self;
}

void Init_raindrops_linux_tcp_info(void)
{
	VALUE cRaindrops = rb_const_get(rb_cObject, rb_intern("Raindrops"));
	VALUE cTCP_Info;

	cTCP_Info = rb_define_class_under(cRaindrops, "TCP_Info", rb_cObject);
	rb_define_alloc_func(cTCP_Info, alloc);
	rb_define_private_method(cTCP_Info, "initialize", init, 1);

#define TCPI_DEFINE_METHOD(x) \
	rb_define_method(cTCP_Info, #x, tcp_info_##x, 0)

	TCPI_DEFINE_METHOD(state);
	TCPI_DEFINE_METHOD(ca_state);
	TCPI_DEFINE_METHOD(retransmits);
	TCPI_DEFINE_METHOD(probes);
	TCPI_DEFINE_METHOD(backoff);
	TCPI_DEFINE_METHOD(options);
	TCPI_DEFINE_METHOD(snd_wscale);
	TCPI_DEFINE_METHOD(rcv_wscale);
	TCPI_DEFINE_METHOD(rto);
	TCPI_DEFINE_METHOD(ato);
	TCPI_DEFINE_METHOD(snd_mss);
	TCPI_DEFINE_METHOD(rcv_mss);
	TCPI_DEFINE_METHOD(unacked);
	TCPI_DEFINE_METHOD(sacked);
	TCPI_DEFINE_METHOD(lost);
	TCPI_DEFINE_METHOD(retrans);
	TCPI_DEFINE_METHOD(fackets);
	TCPI_DEFINE_METHOD(last_data_sent);
	TCPI_DEFINE_METHOD(last_ack_sent);
	TCPI_DEFINE_METHOD(last_data_recv);
	TCPI_DEFINE_METHOD(last_ack_recv);
	TCPI_DEFINE_METHOD(pmtu);
	TCPI_DEFINE_METHOD(rcv_ssthresh);
	TCPI_DEFINE_METHOD(rtt);
	TCPI_DEFINE_METHOD(rttvar);
	TCPI_DEFINE_METHOD(snd_ssthresh);
	TCPI_DEFINE_METHOD(snd_cwnd);
	TCPI_DEFINE_METHOD(advmss);
	TCPI_DEFINE_METHOD(reordering);
	TCPI_DEFINE_METHOD(rcv_rtt);
	TCPI_DEFINE_METHOD(rcv_space);
	TCPI_DEFINE_METHOD(total_retrans);
}
#endif /* TCP_INFO */
