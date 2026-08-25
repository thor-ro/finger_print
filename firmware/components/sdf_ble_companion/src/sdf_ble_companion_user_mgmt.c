/* User-management request/reply protocol for the Enrollment characteristic
 * (companion-user-mgmt). Target-independent on purpose: the parser, reply
 * formatters and admission decision carry no BLE-stack or cJSON dependency,
 * so they compile into the host (linux) test runner alongside their suite -
 * see test_sdf_ble_companion_um.c and test_runner/main/CMakeLists.txt.
 *
 * Wire format:
 *   request:  {"req":<id>,"verb":"list"|"enroll"|"delete"|
 *              "set_permission"|"rename"[,"user_id":N][,"permission":P]
 *              [,"name":"..."]}
 *   reply:    {"req":<id>,"result":"<outcome>"}
 *   list part:{"req":<id>,"verb":"list","part":i,"end":true|false,
 *              "users":[...]}
 */

#include "sdf_ble_companion.h"
#include "sdf_ble_companion_gatt_scratch.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

/* Mirrors sdf_ble_companion.c's derivation: a request may be no longer than
 * what the GATT staging layer accepts. */
#define SDF_BLE_COMPANION_ATTR_MAX_LEN SDF_BLE_COMPANION_GATT_SCRATCH_LEN

static const char *TAG = "sdf_ble_companion_um";

/* -------------------------------------------------------------------------
 * Minimal flat-JSON scanner. The request is a flat object whose values are
 * numbers or one string; this scanner deliberately avoids cJSON so the wire
 * layer stays compilable on the host target (cjson is gated off for linux in
 * the component manager manifest) and so a malformed request can still echo
 * its request id back: `req` is captured as soon as it parses, even if a
 * later field makes the whole request invalid.
 * ------------------------------------------------------------------------- */

static void um_skip_ws(const char **p, const char *end) {
    while (*p < end &&
           (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n')) {
        (*p)++;
    }
}

static bool um_parse_string(const char **p, const char *end, char *out,
                            size_t out_cap) {
    if (*p >= end || **p != '"') {
        return false;
    }
    (*p)++;
    size_t n = 0;
    while (*p < end && **p != '"') {
        char c = **p;
        if (c == '\\') {
            (*p)++;
            if (*p >= end) {
                return false;
            }
            switch (**p) {
            case '"': c = '"'; break;
            case '\\': c = '\\'; break;
            case '/': c = '/'; break;
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u':
                /* Names are short identifiers; an escaped codepoint beyond
                 * the simple set is not something the protocol carries.
                 * Reject rather than guess. */
                return false;
            default:
                return false;
            }
            (*p)++;
        } else if ((unsigned char)c < 0x20) {
            return false;
        } else {
            (*p)++;
        }
        if (n + 1 >= out_cap) {
            return false; /* too long for the field */
        }
        out[n++] = c;
    }
    if (*p >= end) {
        return false;
    }
    (*p)++; /* closing quote */
    out[n] = '\0';
    return true;
}

static bool um_parse_number(const char **p, const char *end, long *out) {
    bool negative = false;
    if (*p < end && **p == '-') {
        negative = true;
        (*p)++;
    }
    if (*p >= end || **p < '0' || **p > '9') {
        return false;
    }
    long value = 0;
    while (*p < end && **p >= '0' && **p <= '9') {
        value = value * 10 + (**p - '0');
        if (value > 1000000) {
            /* Reject rather than clamp: a clamped request id would be
             * echoed back as a number the client never sent, and no
             * protocol field legitimately reaches this magnitude. */
            return false;
        }
        (*p)++;
    }
    /* A fractional or exponent tail is out of protocol. */
    if (*p < end && (**p == '.' || **p == 'e' || **p == 'E')) {
        return false;
    }
    *out = negative ? -value : value;
    return true;
}

static bool um_verb_from_name(const char *name,
                              sdf_ble_companion_um_verb_t *verb_out) {
    if (strcmp(name, "list") == 0) {
        *verb_out = SDF_BLE_COMPANION_UM_VERB_LIST;
    } else if (strcmp(name, "enroll") == 0) {
        *verb_out = SDF_BLE_COMPANION_UM_VERB_ENROLL;
    } else if (strcmp(name, "delete") == 0) {
        *verb_out = SDF_BLE_COMPANION_UM_VERB_DELETE;
    } else if (strcmp(name, "set_permission") == 0) {
        *verb_out = SDF_BLE_COMPANION_UM_VERB_SET_PERMISSION;
    } else if (strcmp(name, "rename") == 0) {
        *verb_out = SDF_BLE_COMPANION_UM_VERB_RENAME;
    } else {
        return false;
    }
    return true;
}

bool sdf_ble_companion_um_parse_request(const uint8_t *data, size_t len,
                                        sdf_ble_companion_um_request_t *req) {
    if (req == NULL) {
        return false;
    }

    /* Cleared before any early return: the caller reads req->req_id_valid
     * on the failure path to echo the request id back in its error reply,
     * and a zero-length or oversized write must not send it reading an
     * indeterminate struct. */
    memset(req, 0, sizeof(*req));
    req->req_id_valid = false;

    if (data == NULL || len == 0 || len >= SDF_BLE_COMPANION_ATTR_MAX_LEN) {
        return false;
    }

    const char *p = (const char *)data;
    const char *end = p + len;

    um_skip_ws(&p, end);
    if (p >= end || *p != '{') {
        return false;
    }
    p++;
    um_skip_ws(&p, end);
    if (p < end && *p == '}') {
        p++;
        return false; /* empty object: no verb, no id */
    }

    bool verb_seen = false;
    bool user_id_seen = false;
    bool permission_seen = false;
    bool name_seen = false;
    long user_id = 0;
    long permission = 0;
    char verb_name[32];
    char name[SDF_BLE_COMPANION_UM_NAME_MAX];

    while (p < end) {
        um_skip_ws(&p, end);
        char key[32];
        if (!um_parse_string(&p, end, key, sizeof(key))) {
            return false;
        }
        um_skip_ws(&p, end);
        if (p >= end || *p != ':') {
            return false;
        }
        p++;
        um_skip_ws(&p, end);

        if (strcmp(key, "req") == 0) {
            long v;
            if (!um_parse_number(&p, end, &v) || v < 0) {
                return false;
            }
            req->req_id = (uint32_t)v;
            req->req_id_valid = true; /* captured even if a later field fails */
        } else if (strcmp(key, "verb") == 0) {
            if (!um_parse_string(&p, end, verb_name, sizeof(verb_name))) {
                return false;
            }
            if (!um_verb_from_name(verb_name, &req->verb)) {
                ESP_LOGW(TAG, "UM request: unknown verb '%s'", verb_name);
                return false;
            }
            verb_seen = true;
        } else if (strcmp(key, "user_id") == 0) {
            if (!um_parse_number(&p, end, &user_id)) {
                return false;
            }
            user_id_seen = true;
        } else if (strcmp(key, "permission") == 0) {
            if (!um_parse_number(&p, end, &permission)) {
                return false;
            }
            permission_seen = true;
        } else if (strcmp(key, "name") == 0) {
            if (!um_parse_string(&p, end, name, sizeof(name))) {
                return false;
            }
            name_seen = true;
        } else {
            /* Unknown key: skip its value without interpreting it. */
            if (p < end && *p == '"') {
                char discard[SDF_BLE_COMPANION_UM_NAME_MAX];
                if (!um_parse_string(&p, end, discard, sizeof(discard))) {
                    return false;
                }
            } else if (!um_parse_number(&p, end, &(long){0})) {
                return false;
            }
        }

        um_skip_ws(&p, end);
        if (p < end && *p == ',') {
            p++;
            continue;
        }
        break;
    }

    um_skip_ws(&p, end);
    if (p >= end || *p != '}') {
        return false;
    }
    p++;
    um_skip_ws(&p, end);
    if (p != end) {
        return false; /* trailing garbage */
    }

    if (!verb_seen || !req->req_id_valid) {
        /* Includes the legacy bare enrolment payload {"user_id":N,
         * "permission":P}: no verb and no request id - answered upstream as
         * an invalid request, never executed. */
        return false;
    }

    /* Per-verb field validation. Out-of-range fields are invalid requests,
     * answered with a reply - never silently clamped. */
    switch (req->verb) {
    case SDF_BLE_COMPANION_UM_VERB_LIST:
        break;
    case SDF_BLE_COMPANION_UM_VERB_ENROLL:
    case SDF_BLE_COMPANION_UM_VERB_SET_PERMISSION:
        if (!user_id_seen || !permission_seen || user_id < 1 ||
            user_id > SDF_FINGERPRINT_USER_ID_MAX || permission < 1 ||
            permission > 3) {
            return false;
        }
        break;
    case SDF_BLE_COMPANION_UM_VERB_DELETE:
        if (!user_id_seen || user_id < 1 ||
            user_id > SDF_FINGERPRINT_USER_ID_MAX) {
            return false;
        }
        break;
    case SDF_BLE_COMPANION_UM_VERB_RENAME:
        if (!user_id_seen || !name_seen || user_id < 1 ||
            user_id > SDF_FINGERPRINT_USER_ID_MAX || name[0] == '\0') {
            return false;
        }
        break;
    default:
        return false;
    }

    req->user_id = (uint16_t)user_id;
    req->permission = (uint8_t)permission;
    if (name_seen) {
        strncpy(req->name, name, sizeof(req->name) - 1);
        req->name[sizeof(req->name) - 1] = '\0';
    } else {
        req->name[0] = '\0';
    }
    return true;
}

/* -------------------------------------------------------------------------
 * Reply formatting
 * ------------------------------------------------------------------------- */

int sdf_ble_companion_um_format_reply(uint32_t req_id, const char *result,
                                      char *buf, size_t cap) {
    if (buf == NULL || result == NULL) {
        return -1;
    }
    int n = snprintf(buf, cap, "{\"req\":%lu,\"result\":\"%s\"}",
                     (unsigned long)req_id, result);
    return (n <= 0 || (size_t)n >= cap) ? -1 : n;
}

int sdf_ble_companion_um_format_list_part(uint32_t req_id, int part, bool end,
                                          const char *users_json, char *buf,
                                          size_t cap) {
    if (buf == NULL || users_json == NULL) {
        return -1;
    }
    int n = snprintf(buf, cap,
                     "{\"req\":%lu,\"verb\":\"list\",\"part\":%d,"
                     "\"end\":%s,\"users\":%s}",
                     (unsigned long)req_id, part, end ? "true" : "false",
                     users_json);
    return (n <= 0 || (size_t)n >= cap) ? -1 : n;
}

/* -------------------------------------------------------------------------
 * Admission decision (ble-companion-service "Setup-Phase Admission To The
 * Enrollment Characteristic")
 *
 * Live admin authority admits every verb. A setup-phase connection to a
 * device with no enrolled users admits ONLY the enrolment verb - neither an
 * account nor an admin exists yet, which is exactly why no scan can be
 * required - and the exception closes the moment any user is enrolled.
 * ------------------------------------------------------------------------- */

bool sdf_ble_companion_um_admits(const sdf_ble_companion_um_request_t *req,
                                 bool conn_has_admin_authority,
                                 bool setup_phase_armed,
                                 bool no_users_enrolled) {
    if (req == NULL) {
        return false;
    }
    if (conn_has_admin_authority) {
        return true;
    }
    return setup_phase_armed && no_users_enrolled &&
           req->verb == SDF_BLE_COMPANION_UM_VERB_ENROLL;
}
