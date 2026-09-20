/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __HTTPD_JSON_H
#define __HTTPD_JSON_H
#include <vsprintf.h>
#include <hexdump.h>
#include <linux/errno.h>
#include <linux/ctype.h>
#define JSMN_STATIC
#define JSMN_STRICT
#include <net/httpd-jsmn.h>

struct http_json {
	char *data;
	size_t size;
	size_t len;
	bool overflow;
};

static void json_printf(struct http_json *j, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (j->overflow)
		return;
	va_start(ap, fmt);
	n = vsnprintf(j->data + j->len, j->size - j->len, fmt, ap);
	va_end(ap);
	if (n < 0 || n >= j->size - j->len) {
		j->overflow = true;
		return;
	}
	j->len += n;
}

static void json_string(struct http_json *j, const char *s)
{
	const unsigned char *p = (const unsigned char *)(s ? s : "");

	json_printf(j, "\"");
	while (*p && !j->overflow) {
		if (*p == '"' || *p == '\\')
			json_printf(j, "\\%c", *p);
		else if (*p < 32)
			json_printf(j, "\\u%04x", *p);
		else
			json_printf(j, "%c", *p);
		p++;
	}
	json_printf(j, "\"");
}

struct http_json_request {
	const char *body;
	jsmntok_t tokens[40];
	int count;
};

static int json_request_init(struct http_json_request *r, const char *body)
{
	jsmn_parser parser;
	int i;

	jsmn_init(&parser);
	r->body = body;
	r->count = jsmn_parse(&parser, body, strlen(body), r->tokens,
			     ARRAY_SIZE(r->tokens));
	if (r->count < 1 || r->tokens[0].type != JSMN_OBJECT ||
	    !(r->count & 1))
		return -EINVAL;
	for (i = r->tokens[0].end; body[i]; i++)
		if (!isspace((unsigned char)body[i]))
			return -EINVAL;
	for (i = 1; i < r->count; i += 2) {
		jsmntok_t *t = &r->tokens[i];

		if (t->type != JSMN_STRING ||
		    (r->tokens[i + 1].type != JSMN_STRING &&
		     r->tokens[i + 1].type != JSMN_PRIMITIVE))
			return -EINVAL;
	}
	return 0;
}

static const jsmntok_t *json_field(struct http_json_request *r, const char *key)
{
	int i;

	for (i = 1; i < r->count; i += 2)
		if (r->tokens[i].end - r->tokens[i].start == strlen(key) &&
		    !memcmp(r->body + r->tokens[i].start, key, strlen(key)))
			return &r->tokens[i + 1];
	return NULL;
}

static int json_hex4(const char *p)
{
	int i, n = 0, v;

	for (i = 0; i < 4; i++) {
		v = hex_to_bin(p[i]);
		if (v < 0)
			return -EINVAL;
		n = (n << 4) | v;
	}
	return n;
}

static int json_text(struct http_json_request *r, const char *key,
		     char *out, size_t size)
{
	const jsmntok_t *t = json_field(r, key);
	const char *p, *end;
	size_t n = 0;
	unsigned int ch;
	int v, low;

	if (!t || t->type != JSMN_STRING || !size)
		return -EINVAL;
	p = r->body + t->start;
	end = r->body + t->end;
	while (p < end) {
		ch = (unsigned char)*p++;
		if (ch == '\\') {
			if (p == end)
				return -EINVAL;
			switch (*p++) {
			case '"': ch = '"'; break;
			case '\\': ch = '\\'; break;
			case '/': ch = '/'; break;
			case 'n': ch = '\n'; break;
			case 'r': ch = '\r'; break;
			case 't': ch = '\t'; break;
			case 'b': ch = '\b'; break;
			case 'f': ch = '\f'; break;
			case 'u':
				if (end - p < 4 || (v = json_hex4(p)) < 0)
					return -EINVAL;
				ch = v; p += 4;
				if (ch >= 0xd800 && ch <= 0xdbff) {
					if (end - p < 6 || p[0] != '\\' || p[1] != 'u' ||
					    (low = json_hex4(p + 2)) < 0xdc00 || low > 0xdfff)
						return -EINVAL;
					ch = 0x10000 + ((ch - 0xd800) << 10) + low - 0xdc00;
					p += 6;
				} else if (ch >= 0xdc00 && ch <= 0xdfff) {
					return -EINVAL;
				}
				if (n + 4 >= size || !ch)
					return -EINVAL;
				if (ch >= 0x10000) {
					out[n++] = 0xf0 | (ch >> 18);
					out[n++] = 0x80 | ((ch >> 12) & 63);
					out[n++] = 0x80 | ((ch >> 6) & 63);
					ch = 0x80 | (ch & 63);
				} else if (ch >= 0x800) {
					out[n++] = 0xe0 | (ch >> 12);
					out[n++] = 0x80 | ((ch >> 6) & 63);
					ch = 0x80 | (ch & 63);
				} else if (ch >= 0x80) {
					out[n++] = 0xc0 | (ch >> 6);
					ch = 0x80 | (ch & 63);
				}
				break;
			default: return -EINVAL;
			}
		} else if (ch < 32) {
			return -EINVAL;
		}
		if (!ch || n + 1 >= size)
			return -ENOSPC;
		out[n++] = ch;
	}
	out[n] = 0;
	return 0;
}

static int json_uint(struct http_json_request *r, const char *key, u64 *value)
{
	const jsmntok_t *t = json_field(r, key);
	char text[32], *end;
	size_t n;

	if (!t) {
		*value = 0;
		return 0;
	}
	n = t->end - t->start;
	if (!n || n >= sizeof(text))
		return -EINVAL;
	memcpy(text, r->body + t->start, n);
	text[n] = 0;
	if (!isdigit((unsigned char)text[0]))
		return -EINVAL;
	*value = simple_strtoull(text, &end, 0);
	return *end ? -EINVAL : 0;
}
#endif
