// Small services: parental control, add-on content, and other mostly-stubbed interfaces.
#include <string.h>

#include "../hle.h"
#include "../ipc.h"

static void ok_logger(Service *s, IpcReq *req, IpcResp *resp) {
    LOG("[svc] %s cmd %u (ok)\n", s->name, req->cmd);
    resp_size(resp, 0x10);
}

static uint32_t new_event_handle(void) {
    uint32_t w, r;
    kernel_create_event_pair(&w, &r);
    return r;
}

// ------------------------------------------------------------------ pctl
static void pctl_service(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 1031: case 1035: case 1018: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;  // restrictions off
    case 1032: OUT(uint32_t, 0) = 0; resp_size(resp, 4); break;
    default: LOG("[pctl] IParentalControlService cmd %u (ok)\n", req->cmd); resp_size(resp, 0x10);
    }
}

static void pctl_root(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    if (req->cmd == 0 || req->cmd == 1)
        ipc_return_object(resp, service_new("IParentalControlService", pctl_service, NULL));
}
SERVICE("pctl", pctl) { pctl_root(s, req, resp); }
SERVICE("pctl:s", pctl_s) { pctl_root(s, req, resp); }
SERVICE("pctl:a", pctl_a) { pctl_root(s, req, resp); }
SERVICE("pctl:r", pctl_r) { pctl_root(s, req, resp); }

// ------------------------------------------------------------------ aoc:u: the DLC in <game files>/aoc (services/fs.c)
#define AOC_BASE_ID 0x01009CC00C97D000ull  // (the app id + 0x1000; a DLC's index is its data id less this)
int aoc_count(void);
uint64_t aoc_id(int i);

// ListAddOnContent(offset, count): the indices of the DLC from the offset-th on
static void aoc_list(IpcReq *req, IpcResp *resp) {
    uint32_t offset = IN(uint32_t, 0), count = IN(uint32_t, 4), n = 0;
    IpcBuf *b = out_buf(req, 0);
    uint32_t room = b ? (uint32_t)(b->size / 4) : 0;
    if (room) plat_os_write(b->addr, (uint64_t)room * 4);
    for (int i = (int)offset; i < aoc_count() && n < count && n < room; i++)
        ((uint32_t *)(uintptr_t)b->addr)[n++] = (uint32_t)(aoc_id(i) - AOC_BASE_ID);
    OUT(uint32_t, 0) = n;
    resp_size(resp, 4);
}

SERVICE("aoc:u", aoc_u) {
    (void)s;
    switch (req->cmd) {
    case 0: case 2: OUT(uint32_t, 0) = (uint32_t)aoc_count(); resp_size(resp, 4); break;  // CountAddOnContent
    case 1: case 3: aoc_list(req, resp); break;                                         // ListAddOnContent
    case 4: case 5: OUT(uint64_t, 0) = AOC_BASE_ID; resp_size(resp, 8); break;         // GetAddOnContentBaseId
    case 6: case 7: LOG("[aoc] PrepareAddOnContent %u\n", IN(uint32_t, 0)); break;
    case 8: resp->copy_handles[resp->ncopy++] = new_event_handle(); break;
    default: ok_logger(s, req, resp);
    }
}
