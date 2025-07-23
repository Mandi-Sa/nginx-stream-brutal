/*
 * Copyright (C) 2011 Nicolas Viennot <nicolas@viennot.biz>
 *
 * Copyright (C) 2023 Duoduo Song <sduoduo233@gmail.com>
 *
 * Copyright (C) 2025 阿菌•未霜 <799620521@qq.com>
 *
 * This file is subject to the terms and conditions of the MIT License.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <errno.h>

typedef uint64_t u64;
typedef uint32_t u32;

struct brutal_params {
	u64 rate;	/* Send rate in bytes per second */
	u32 cwnd_gain;	/* CWND gain in tenths (10=1.0) */
} __packed;

extern ngx_module_t ngx_stream_tcp_brutal_module;

typedef struct {
	ngx_flag_t enable;
	ngx_uint_t rate;
	ngx_uint_t cwnd_gain;
} ngx_stream_tcp_brutal_conf_t;

static ngx_int_t ngx_stream_tcp_brutal_handler(ngx_stream_session_t *s);
static ngx_int_t ngx_stream_tcp_brutal_init(ngx_conf_t *cf);
static void *ngx_stream_tcp_brutal_create_srv_conf(ngx_conf_t *cf);
static char *ngx_stream_tcp_brutal_merge_srv_conf(ngx_conf_t *cf, void *parent,
						  void *child);

static ngx_command_t ngx_stream_tcp_brutal_commands[] = {
	{
		ngx_string("tcp_brutal"),
		NGX_STREAM_SRV_CONF | NGX_CONF_FLAG,
		ngx_conf_set_flag_slot,
		NGX_STREAM_SRV_CONF_OFFSET,
		offsetof(ngx_stream_tcp_brutal_conf_t, enable),
		NULL
	},
	{
		ngx_string("tcp_brutal_rate"),
		NGX_STREAM_SRV_CONF | NGX_CONF_TAKE1,
		ngx_conf_set_num_slot,
		NGX_STREAM_SRV_CONF_OFFSET,
		offsetof(ngx_stream_tcp_brutal_conf_t, rate),
		NULL
	},
	{
		ngx_string("tcp_brutal_cwnd_gain"),
		NGX_STREAM_SRV_CONF | NGX_CONF_TAKE1,
		ngx_conf_set_num_slot,
		NGX_STREAM_SRV_CONF_OFFSET,
		offsetof(ngx_stream_tcp_brutal_conf_t, cwnd_gain),
		NULL
	},
	ngx_null_command
};

static ngx_stream_module_t ngx_stream_tcp_brutal_module_ctx = {
	NULL,					/* preconfiguration */
	ngx_stream_tcp_brutal_init,		/* postconfiguration */

	NULL,					/* create main configuration */
	NULL,					/* init main configuration */

	ngx_stream_tcp_brutal_create_srv_conf,	/* create server conf */
	ngx_stream_tcp_brutal_merge_srv_conf	/* merge server conf */
};

ngx_module_t ngx_stream_tcp_brutal_module = {
	NGX_MODULE_V1,
	&ngx_stream_tcp_brutal_module_ctx,	/* module context */
	ngx_stream_tcp_brutal_commands,		/* module directives */
	NGX_STREAM_MODULE,			/* module type */
	NULL,					/* init master */
	NULL,					/* init module */
	NULL,					/* init process */
	NULL,					/* init thread */
	NULL,					/* exit thread */
	NULL,					/* exit process */
	NULL,					/* exit master */
	NGX_MODULE_V1_PADDING
};

static void *ngx_stream_tcp_brutal_create_srv_conf(ngx_conf_t *cf)
{
	ngx_stream_tcp_brutal_conf_t *conf;

	conf = ngx_pcalloc(cf->pool, sizeof(ngx_stream_tcp_brutal_conf_t));
	if (conf == NULL)
		return NULL;

	conf->enable = NGX_CONF_UNSET;
	conf->rate = NGX_CONF_UNSET_UINT;
	conf->cwnd_gain = NGX_CONF_UNSET_UINT;

	return conf;
}

static char *ngx_stream_tcp_brutal_merge_srv_conf(ngx_conf_t *cf, void *parent,
						  void *child)
{
	ngx_stream_tcp_brutal_conf_t *prev = parent;
	ngx_stream_tcp_brutal_conf_t *conf = child;

	ngx_conf_merge_value(conf->enable, prev->enable, 0);
	ngx_conf_merge_uint_value(conf->rate, prev->rate, 2);
	ngx_conf_merge_uint_value(conf->cwnd_gain, prev->cwnd_gain, 15);

	if (conf->cwnd_gain < 5 || conf->cwnd_gain > 80) {
		ngx_conf_log_error(
			NGX_LOG_EMERG, cf, 0,
			"Invalid value \"%ui\" for \"tcp_brutal_cwnd_gain\", "
			"must be between 5 and 80",
			conf->cwnd_gain);
		return NGX_CONF_ERROR;
	}

	return NGX_CONF_OK;
}

static ngx_int_t ngx_stream_tcp_brutal_handler(ngx_stream_session_t *s)
{
	ngx_stream_tcp_brutal_conf_t *conf;
	struct brutal_params params;
	const char buf[] = "brutal";
	socklen_t len = sizeof(buf);
	int fd;

	conf = ngx_stream_get_module_srv_conf(s, ngx_stream_tcp_brutal_module);
	if (!conf->enable)
		return NGX_DECLINED;

	fd = s->connection->fd;

	/* only operate on real TCP sockets */
	if (s->connection->sockaddr->sa_family != AF_INET &&
	    s->connection->sockaddr->sa_family != AF_INET6) {
		return NGX_DECLINED;
	}

	/* set brutal congestion control */

	if (setsockopt(fd, IPPROTO_TCP, TCP_CONGESTION, buf, len) != 0) {
		ngx_log_error(NGX_LOG_ERR, s->connection->log, errno,
			      "tcp_brutal: TCP_CONGESTION failed");
		return NGX_ERROR;
	}

	/* set brutal-specific parameters */
	
	params.rate = conf->rate;
	params.cwnd_gain = conf->cwnd_gain;

	/* TCP_BRUTAL_PARAMS = 23301 */
	if (setsockopt(fd, IPPROTO_TCP, 23301, &params, sizeof(params)) != 0) {
		ngx_log_error(NGX_LOG_ERR, s->connection->log, errno,
			      "tcp_brutal: brutal_params failed");
		return NGX_ERROR;
	}

	return NGX_DECLINED;
}

static ngx_int_t ngx_stream_tcp_brutal_init(ngx_conf_t *cf)
{
	ngx_stream_core_main_conf_t *cmcf;
	ngx_stream_handler_pt *h;

	cmcf = ngx_stream_conf_get_module_main_conf(cf, ngx_stream_core_module);

	h = ngx_array_push(&cmcf->phases[NGX_STREAM_PREREAD_PHASE].handlers);
	if (h == NULL)
		return NGX_ERROR;

	*h = ngx_stream_tcp_brutal_handler;

	return NGX_OK;
}
