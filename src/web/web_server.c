#include "web/web_server.h"
#include "web/web_page.h"
#include "storage/storage_manager.h"
#include "net/wifi_manager.h"
#include "core/log.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define HTTP_MAX_CONN 4
#define HTTP_REQ_BUF 1536
#define HTTP_IDLE_POLLS 10

typedef struct
{
    struct tcp_pcb *pcb;
    char req[HTTP_REQ_BUF];
    uint16_t req_len;
    char hdr[256];
    uint16_t hdr_len;
    const char *body;
    uint32_t body_len;
    char small_body[1024];
    uint32_t queued;
    uint32_t acked;
    uint32_t poll_snapshot;
} http_conn_t;

static http_conn_t s_conns[HTTP_MAX_CONN];
static struct tcp_pcb *s_listen_pcb = NULL;

static err_t http_close(http_conn_t *c, bool do_abort)
{
    err_t ret = ERR_OK;
    if (c->pcb)
    {
        tcp_arg(c->pcb, NULL);
        tcp_recv(c->pcb, NULL);
        tcp_sent(c->pcb, NULL);
        tcp_poll(c->pcb, NULL, 0);
        tcp_err(c->pcb, NULL);
        if (do_abort || tcp_close(c->pcb) != ERR_OK)
        {
            tcp_abort(c->pcb);
            ret = ERR_ABRT;
        }
    }
    memset(c, 0, sizeof(*c));
    return ret;
}

static void http_send_more(http_conn_t *c)
{
    uint32_t total = c->hdr_len + c->body_len;
    while (c->queued < total)
    {
        uint16_t space = tcp_sndbuf(c->pcb);
        if (space == 0)
            break;

        const char *src;
        uint32_t remain;
        if (c->queued < c->hdr_len)
        {
            src = c->hdr + c->queued;
            remain = c->hdr_len - c->queued;
        }
        else
        {
            uint32_t off = c->queued - c->hdr_len;
            src = c->body + off;
            remain = c->body_len - off;
        }

        uint16_t chunk = (remain < space) ? (uint16_t)remain : space;
        if (tcp_write(c->pcb, src, chunk, 0) != ERR_OK)
            break;

        c->queued += chunk;
    }
    tcp_output(c->pcb);
}

static err_t http_on_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    (void)pcb;
    http_conn_t *c = (http_conn_t *)arg;
    c->acked += len;
    if (c->acked >= (uint32_t)c->hdr_len + c->body_len)
    {
        return http_close(c, false);
    }
    http_send_more(c);
    return ERR_OK;
}

static void http_respond(http_conn_t *c, int status, const char *status_text,
                         const char *content_type, const char *content_encoding,
                         const void *body, uint32_t body_len)
{
    char enc_hdr[64] = {0};
    if (content_encoding && strlen(content_encoding) > 0)
    {
        snprintf(enc_hdr, sizeof(enc_hdr), "Content-Encoding: %s\r\n", content_encoding);
    }

    c->hdr_len = (uint16_t)snprintf(c->hdr, sizeof(c->hdr),
                                    "HTTP/1.1 %d %s\r\n"
                                    "Content-Type: %s\r\n"
                                    "%s"
                                    "Content-Length: %lu\r\n"
                                    "Connection: close\r\n"
                                    "Access-Control-Allow-Origin: *\r\n"
                                    "Cache-Control: no-store\r\n"
                                    "\r\n",
                                    status, status_text, content_type, enc_hdr, (unsigned long)body_len);

    c->body = (const char *)body;
    c->body_len = body_len;
    c->queued = 0;
    c->acked = 0;
    http_send_more(c);
}

static void http_respond_redirect(http_conn_t *c, const char *location)
{
    c->hdr_len = (uint16_t)snprintf(c->hdr, sizeof(c->hdr),
                                    "HTTP/1.1 302 Found\r\n"
                                    "Location: %s\r\n"
                                    "Content-Length: 0\r\n"
                                    "Connection: close\r\n"
                                    "\r\n",
                                    location);

    c->body = "";
    c->body_len = 0;
    c->queued = 0;
    c->acked = 0;
    http_send_more(c);
}

static void http_respond_404(http_conn_t *c)
{
    static const char nf[] = "404 Not Found";
    http_respond(c, 404, "Not Found", "text/plain", NULL, nf, sizeof(nf) - 1);
}

static void http_respond_400(http_conn_t *c)
{
    static const char br[] = "400 Bad Request";
    http_respond(c, 400, "Bad Request", "text/plain", NULL, br, sizeof(br) - 1);
}

static void api_status_get(http_conn_t *c)
{
    int n = snprintf(c->small_body, sizeof(c->small_body),
                     "{\"uptime_ms\":%lu,\"wifi_connected\":%s,\"slots\":[",
                     (unsigned long)to_ms_since_boot(get_absolute_time()),
                     wifi_manager_get_status().is_connected ? "true" : "false");

    for (uint8_t s = 0; s < INFINITY_NUM_SLOTS; s++)
    {
        const char *path = storage_manager_get_slot_path(s);
        bool active = (path && path[0] != '\0');
        n += snprintf(c->small_body + n, sizeof(c->small_body) - n,
                      "%s{\"slot\":%u,\"active\":%s,\"path\":\"%s\"}",
                      s ? "," : "",
                      s,
                      active ? "true" : "false",
                      active ? path : "");
    }
    n += snprintf(c->small_body + n, sizeof(c->small_body) - n, "]}");
    http_respond(c, 200, "OK", "application/json", NULL, c->small_body, (uint32_t)n);
}

static void api_slot_post(http_conn_t *c, const char *body)
{
    const char *ps = strstr(body, "\"slot\":");
    if (!ps)
    {
        http_respond_400(c);
        return;
    }

    int slot = atoi(ps + 7);
    if (slot < 0 || slot >= INFINITY_NUM_SLOTS)
    {
        http_respond_400(c);
        return;
    }

    if (strstr(body, "\"clear\":true") || strstr(body, "\"clear\":1") || strstr(body, "\"path\":null"))
    {
        storage_manager_clear_slot((uint8_t)slot);
    }
    else
    {
        const char *pp = strstr(body, "\"path\":\"");
        if (pp)
        {
            char path[STORAGE_MAX_PATH_LEN];
            pp += 8;
            const char *end = strchr(pp, '"');
            if (!end || (size_t)(end - pp) >= sizeof(path))
            {
                http_respond_400(c);
                return;
            }
            memcpy(path, pp, end - pp);
            path[end - pp] = '\0';
            storage_manager_set_slot_figure((uint8_t)slot, path);
        }
        else
        {
            http_respond_400(c);
            return;
        }
    }

    static const char ok[] = "{\"ok\":true}";
    http_respond(c, 200, "OK", "application/json", NULL, ok, sizeof(ok) - 1);
}

static void api_wifi_post(http_conn_t *c, const char *body)
{
    const char *ps = strstr(body, "\"ssid\":\"");
    if (!ps)
    {
        http_respond_400(c);
        return;
    }
    ps += 8;
    const char *es = strchr(ps, '"');
    if (!es)
    {
        http_respond_400(c);
        return;
    }

    char ssid[33] = {0};
    size_t slen = es - ps;
    if (slen >= sizeof(ssid))
        slen = sizeof(ssid) - 1;
    memcpy(ssid, ps, slen);

    char pass[65] = {0};
    const char *pp = strstr(body, "\"password\":\"");
    if (pp)
    {
        pp += 12;
        const char *ep = strchr(pp, '"');
        if (ep)
        {
            size_t plen = ep - pp;
            if (plen >= sizeof(pass))
                plen = sizeof(pass) - 1;
            memcpy(pass, pp, plen);
        }
    }

    wifi_manager_set_sta_config(ssid, pass);
    static const char ok[] = "{\"ok\":true}";
    http_respond(c, 200, "OK", "application/json", NULL, ok, sizeof(ok) - 1);
}

static void http_route(http_conn_t *c, const char *method, const char *path, const char *body)
{
    // Captive Portal Redirect
    if (strcmp(path, "/generate_204") == 0 ||
        strcmp(path, "/gen_204") == 0 ||
        strcmp(path, "/hotspot-detect.html") == 0 ||
        strcmp(path, "/ncsi.txt") == 0 ||
        strcmp(path, "/connecttest.txt") == 0 ||
        strcmp(path, "/canonical.html") == 0)
    {
        http_respond_redirect(c, "http://192.168.4.1/");
        return;
    }

    if (strcmp(method, "GET") == 0)
    {
        if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)
        {
            http_respond(c, 200, "OK", "text/html; charset=utf-8", "gzip",
                         WEB_PAGE_INDEX_GZ, WEB_PAGE_INDEX_GZ_LEN);
            return;
        }
        if (strcmp(path, "/api/status") == 0)
        {
            api_status_get(c);
            return;
        }
        if (strcmp(path, "/api/ping") == 0)
        {
            int n = snprintf(c->small_body, sizeof(c->small_body),
                             "{\"uptime_ms\":%lu}",
                             (unsigned long)to_ms_since_boot(get_absolute_time()));
            http_respond(c, 200, "OK", "application/json", NULL, c->small_body, (uint32_t)n);
            return;
        }
    }
    else if (strcmp(method, "POST") == 0)
    {
        if (strcmp(path, "/api/slot") == 0)
        {
            api_slot_post(c, body);
            return;
        }
        if (strcmp(path, "/api/wifi") == 0)
        {
            api_wifi_post(c, body);
            return;
        }
    }

    http_respond_404(c);
}

static err_t http_on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    http_conn_t *c = (http_conn_t *)arg;
    (void)err;

    if (!p)
    {
        return http_close(c, false);
    }

    if (c->hdr_len)
    {
        tcp_recved(pcb, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }

    uint16_t space = (uint16_t)(sizeof(c->req) - 1 - c->req_len);
    uint16_t take = (p->tot_len < space) ? p->tot_len : space;
    pbuf_copy_partial(p, c->req + c->req_len, take, 0);
    c->req_len += take;
    c->req[c->req_len] = '\0';
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    if (take == 0)
    {
        return http_close(c, true);
    }

    char *hdr_end = strstr(c->req, "\r\n\r\n");
    if (!hdr_end)
    {
        return ERR_OK;
    }

    char *body = hdr_end + 4;
    const char *cl = strstr(c->req, "Content-Length:");
    if (!cl)
        cl = strstr(c->req, "content-length:");
    if (cl && cl < hdr_end)
    {
        int content_len = atoi(cl + 15);
        if (content_len < 0 || content_len > (int)(sizeof(c->req) - 1 - (body - c->req)))
        {
            return http_close(c, true);
        }
        if ((int)(c->req_len - (body - c->req)) < content_len)
        {
            return ERR_OK;
        }
    }

    char *method = c->req;
    char *path = strchr(method, ' ');
    if (!path)
        return http_close(c, true);
    *path++ = '\0';

    char *end = strchr(path, ' ');
    if (!end)
        return http_close(c, true);
    *end = '\0';

    char *query = strchr(path, '?');
    if (query)
        *query = '\0';

    http_route(c, method, path, body);
    return ERR_OK;
}

static err_t http_on_poll(void *arg, struct tcp_pcb *pcb)
{
    (void)pcb;
    http_conn_t *c = (http_conn_t *)arg;
    uint32_t progress = c->acked + c->req_len;
    if (progress == c->poll_snapshot)
    {
        return http_close(c, true);
    }
    c->poll_snapshot = progress;
    return ERR_OK;
}

static void http_on_err(void *arg, err_t err)
{
    (void)err;
    http_conn_t *c = (http_conn_t *)arg;
    c->pcb = NULL;
    memset(c, 0, sizeof(*c));
}

static err_t http_on_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || !newpcb)
        return ERR_VAL;

    http_conn_t *c = NULL;
    for (int i = 0; i < HTTP_MAX_CONN; i++)
    {
        if (!s_conns[i].pcb)
        {
            c = &s_conns[i];
            break;
        }
    }
    if (!c)
        return ERR_MEM;

    memset(c, 0, sizeof(*c));
    c->pcb = newpcb;
    tcp_arg(newpcb, c);
    tcp_recv(newpcb, http_on_recv);
    tcp_sent(newpcb, http_on_sent);
    tcp_err(newpcb, http_on_err);
    tcp_poll(newpcb, http_on_poll, HTTP_IDLE_POLLS);

    return ERR_OK;
}

bool web_server_init(void)
{
    if (s_listen_pcb)
    {
        return true;
    }

    memset(s_conns, 0, sizeof(s_conns));

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb || tcp_bind(pcb, IP_ANY_TYPE, WEB_SERVER_PORT) != ERR_OK)
    {
        log_printf("[Web] Error: tcp_bind :%d failed\n", WEB_SERVER_PORT);
        if (pcb)
            tcp_close(pcb);
        return false;
    }

    s_listen_pcb = tcp_listen_with_backlog(pcb, HTTP_MAX_CONN);
    if (!s_listen_pcb)
    {
        log_printf("[Web] Error: tcp_listen failed\n");
        tcp_close(pcb);
        return false;
    }

    tcp_accept(s_listen_pcb, http_on_accept);
    log_printf("[Web] HTTP Server listening on port %d\n", WEB_SERVER_PORT);
    return true;
}

void web_server_deinit(void)
{
    for (int i = 0; i < HTTP_MAX_CONN; i++)
    {
        if (s_conns[i].pcb)
        {
            http_close(&s_conns[i], true);
        }
    }
    if (s_listen_pcb)
    {
        tcp_close(s_listen_pcb);
        s_listen_pcb = NULL;
    }
    log_printf("[Web] HTTP Server stopped\n");
}