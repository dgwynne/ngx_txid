/*
 * XXX license?
 */
#include <stdlib.h>

#include <nginx.h>
#include <ngx_http.h>
#include <ngx_http_variables.h>

void	ngx_txid_base32_encode(unsigned char *dst, unsigned char *src, size_t n);
size_t	ngx_txid_base32_encode_len(size_t n);

static ngx_msec_t	txid_last_msec = 0;

/*
 * returns monotonically increasing msec per process based on the msec
 * variable incrementing at `timer_resolution` intervals.  The amount of
 * relevant entropy should cover the global requests per msec rate
 */
ngx_msec_t
ngx_txid_next_tick()
{
	if (ngx_current_msec > txid_last_msec)
		txid_last_msec = ngx_current_msec;

	return (txid_last_msec);
}

static ngx_int_t
ngx_txid_get(ngx_http_request_t *r, ngx_http_variable_value_t *v,
    uintptr_t data)
{
	u_char rnd[12]; /* 96 bits */
	ngx_msec_t msec;
	u_char *out;
	size_t len;

	/*
	 * makes a roughly sortable identifier in at least 96 bytes.
	 * +-------------64bit BE-----------remaining > 32 bits---+
	 * | 42 bits msec | 22 bits rand | random...
	 * +------------------------------------------------------+
	 */
	arc4random_buf(rnd, sizeof(rnd));
	msec = ngx_txid_next_tick() << 22;

	rnd[0] = (msec >> 56) & 0xff;
	rnd[1] = (msec >> 48) & 0xff;
	rnd[2] = (msec >> 32) & 0xff;
	rnd[3] = (msec >> 16) & 0xff;
	rnd[4] |= (msec >> 8)  & 0xff;  // share the 5th byte with entropy

	len = ngx_txid_base32_encode_len(sizeof(rnd));

	out = ngx_pnalloc(r->pool, len);
	if (out == NULL) {
		v->valid = 0;
		v->not_found = 1;
		return (NGX_ERROR);
	}

	ngx_txid_base32_encode(out, rnd, sizeof(rnd));

	v->len = (sizeof(rnd) * 8 + 4) / 5; /* strip any padding chars */
	v->data = out;

	v->valid = 1;
	v->not_found = 0;
	v->no_cacheable = 0;

	return (NGX_OK);
}

static ngx_int_t
ngx_txid_init_module(ngx_cycle_t *cycle)
{
	return (NGX_OK);
}

static ngx_str_t ngx_txid_variable_name = ngx_string("txid");

static ngx_int_t
ngx_txid_add_variables(ngx_conf_t *cf)
{
	ngx_http_variable_t* var = ngx_http_add_variable(cf,
	    &ngx_txid_variable_name, NGX_HTTP_VAR_NOHASH);

	if (var == NULL)
		return (NGX_ERROR);

	var->get_handler = ngx_txid_get;

	return (NGX_OK);
}

static ngx_http_module_t ngx_txid_module_ctx = {
	ngx_txid_add_variables,	/* preconfiguration */
	NULL,			/* postconfiguration */

	NULL,			/* create main configuration */
	NULL,			/* init main configuration */

	NULL,			/* create server configuration */
	NULL,			/* merge server configuration */

	NULL,			/* create location configuration */
	NULL			/* merge location configuration */
};

static ngx_command_t ngx_txid_module_commands[] = {
	ngx_null_command
};

ngx_module_t ngx_txid_module = {
	NGX_MODULE_V1,
	&ngx_txid_module_ctx,		/* module context */
	ngx_txid_module_commands,	/* module directives */
	NGX_HTTP_MODULE,		/* module type */
	NULL,				/* init master */
	ngx_txid_init_module,		/* init module */
	NULL,				/* init process */
	NULL,				/* init thread */
	NULL,				/* exit thread */
	NULL,				/* exit process */
	NULL,				/* exit master */
	NGX_MODULE_V1_PADDING
};
