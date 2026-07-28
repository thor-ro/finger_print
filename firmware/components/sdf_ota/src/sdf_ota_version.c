#include "sdf_ota.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "esp_log.h"

static const char *TAG = "sdf_ota_version";

typedef struct {
    int major;
    int minor;
    int patch;
    char pre_release[64];
    bool has_pre_release;
} semver_t;

static bool parse_semver(const char *version, semver_t *out)
{
    if (version == NULL || out == NULL) {
        return false;
    }

    memset(out, 0, sizeof(semver_t));

    const char *p = version;

    /* Skip leading 'v' if present */
    if (*p == 'v' || *p == 'V') {
        p++;
    }

    /* Parse major.minor.patch */
    char *end;
    out->major = (int)strtol(p, &end, 10);
    if (end == p || *end != '.') return false;
    p = end + 1;

    out->minor = (int)strtol(p, &end, 10);
    if (end == p || *end != '.') return false;
    p = end + 1;

    out->patch = (int)strtol(p, &end, 10);
    if (end == p) return false;
    p = end;

    /* Parse pre-release suffix (starts with '-') */
    if (*p == '-') {
        p++;
        const char *pre_start = p;
        while (*p && *p != '+') {
            p++;
        }
        size_t pre_len = p - pre_start;
        if (pre_len >= sizeof(out->pre_release)) {
            pre_len = sizeof(out->pre_release) - 1;
        }
        memcpy(out->pre_release, pre_start, pre_len);
        out->pre_release[pre_len] = '\0';
        out->has_pre_release = true;
    }

    /* Ignore build metadata (starts with '+') */

    return true;
}

static int compare_pre_release(const char *a, const char *b)
{
    /* Both have pre-release: compare alphanumerically */
    if (a && b) {
        return strcmp(a, b);
    }
    /* Release (no pre-release) > pre-release */
    if (!a && b) return 1;
    if (a && !b) return -1;
    /* Both are releases */
    return 0;
}

sdf_ota_version_cmp_t sdf_ota_version_compare(const char *current, const char *incoming)
{
    semver_t cur = {0}, inc = {0};

    if (!parse_semver(current, &cur) || !parse_semver(incoming, &inc)) {
        ESP_LOGW(TAG, "Failed to parse version strings: current='%s', incoming='%s'",
                 current ? current : "NULL", incoming ? incoming : "NULL");
        return SDF_OTA_VERSION_EQUAL;
    }

    ESP_LOGI(TAG, "Version compare: current=v%d.%d.%d%s, incoming=v%d.%d.%d%s",
             cur.major, cur.minor, cur.patch, cur.has_pre_release ? cur.pre_release : "",
             inc.major, inc.minor, inc.patch, inc.has_pre_release ? inc.pre_release : "");

    /* Compare major.minor.patch numerically */
    if (cur.major != inc.major) {
        return (cur.major < inc.major) ? SDF_OTA_VERSION_NEWER : SDF_OTA_VERSION_OLDER;
    }
    if (cur.minor != inc.minor) {
        return (cur.minor < inc.minor) ? SDF_OTA_VERSION_NEWER : SDF_OTA_VERSION_OLDER;
    }
    if (cur.patch != inc.patch) {
        return (cur.patch < inc.patch) ? SDF_OTA_VERSION_NEWER : SDF_OTA_VERSION_OLDER;
    }

    /* Same major.minor.patch, compare pre-release */
    int pre_cmp = compare_pre_release(cur.has_pre_release ? cur.pre_release : NULL,
                                       inc.has_pre_release ? inc.pre_release : NULL);

    if (pre_cmp < 0) {
        return SDF_OTA_VERSION_NEWER;   /* current is older (has pre-release, incoming is release) */
    } else if (pre_cmp > 0) {
        return SDF_OTA_VERSION_OLDER;   /* current is newer (is release, incoming has pre-release) */
    }

    return SDF_OTA_VERSION_EQUAL;
}