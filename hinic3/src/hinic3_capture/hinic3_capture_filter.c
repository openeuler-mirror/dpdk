/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include <errno.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <grp.h>
#include <limits.h>
#include <string.h>
#include <fnmatch.h>
#include "rte_ip.h"

#include "hinic3_unaligned.h"
#include "hinic3_log.h"
#include "hinic3_util.h"
#include "hinic3_meminfo.h"
#include "hinic3_flow_agent.h"
#include "hinic3_ds.h"
#include "hinic3_file_util.h"
#include "hinic3_ui_string.h"
#include "hinic3_string_format.h"
#include "hinic3_eth_packets.h"
#include "hinic3_capture_filter.h"
#include "hinic3_parse_agent_config.h"

#define PCAP_OFFSET_2          2
#define PCAP_OFFSET_3          3
#define PCAP_BYTE_LEN          8
#define PCAP_BIT_MASK          7

static int pcap_key_count_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
static int pcap_key_dir_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
static int pcap_key_file_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
static int pcap_key_host_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
static int pcap_key_filenum_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);
static int pcap_key_output_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds);

static struct pcap_sub_key_parser g_pcap_sub_key_parser[] = {
    {"-c", sizeof("-c"), pcap_key_count_parse},
    {"-w", sizeof("-w"), pcap_key_file_parse},
    {"-P", sizeof("-P"), pcap_key_dir_parse},
    {"-n", sizeof("-n"), pcap_key_filenum_parse},
    {"-o", sizeof("-o"), pcap_key_output_parse},
    {"-sip", sizeof("-sip"), hinic3_key_sip_parse},
    {"-dip", sizeof("-dip"), hinic3_key_dip_parse},
    {"-host", sizeof("-host"), pcap_key_host_parse},
    {"-smac", sizeof("-smac"), hinic3_key_smac_parse},
    {"-dmac", sizeof("-dmac"), hinic3_key_dmac_parse},
    {"-eth_type", sizeof("-eth_type"), hinic3_key_eth_type_parse},
    {"-ip_proto", sizeof("-ip_proto"), hinic3_key_ip_proto_parse},
    {"-sport", sizeof("-sport"), hinic3_key_sport_parse},
    {"-dport", sizeof("-dport"), hinic3_key_dport_parse},
    {"-vlan", sizeof("-vlan"), hinic3_key_vlan_parse},
    {"-vxlan_vni", sizeof("-vxlan_vni"), hinic3_key_vni_parse},
    {"-t", sizeof("-t"), hinic3_key_time_parse},
};

static const char g_blacklist_chars[] = {
    '|', '&', ';', '`', '$', '!',      /* 命令分隔 */
    '<', '>', '(', ')', '{', '}',      /* 重定向 */
    '[', ']', '*', '?', '~',           /* 通配符与特殊符号 */
    ' ', '\t', '\n', '\r', '\v', '\f', /* 空白与控制字符 */
    '\'', '"', '\\'                     /* 引号与转义 */
};

static const char *g_protected_paths[] = {
    "/home/*/.local/share/*",
    "/home/*/.local/share/",
    "/home/*/.local/share",
    "/home/*/.gnupg/*",
    "/home/*/.gnupg/",
    "/home/*/.gnupg",
    "/home/*/.mozilla/*",
    "/home/*/.mozilla/",
    "/home/*/.mozilla",
    "/home/*/.cache/*",
    "/home/*/.cache/",
    "/home/*/.cache",
    "/home/*/.aws/*",
    "/home/*/.aws/",
    "/home/*/.aws",
    "/home/*/.gcp/*",
    "/home/*/.gcp/",
    "/home/*/.gcp",
    "/home/*/.azure/*",
    "/home/*/.azure/",
    "/home/*/.azure",
    "/home/*/.config/*",
    "/home/*/.config/",
    "/home/*/.config",
    "/home/*/.ssh/*",
    "/home/*/.ssh/",
    "/home/*/.ssh",
    "/home/*/.huawei/*",
    "/home/*/.huawei/",
    "/home/*/.huawei",
    "/home/*/.bash_history/*",
    "/home/*/.bash_history/",
    "/home/*/.bash_history",
    "/home/*/.zsh_history/*",
    "/home/*/.zsh_history/",
    "/home/*/.zsh_history",
    "/home/*/.history/*",
    "/home/*/.history/",
    "/home/*/.history",
    "/opt/huawei",
    "/var/spool",
    "/var/mail",
    "/root",
    "/boot",
    "/etc",
    "/proc",
    "/sys",
    "/dev",
    "/bin",
    "/sbin",
    "/usr/bin",
    "/usr/sbin",
    "/lib",
    "/usr/lib",
    "/run",
    NULL
};

static const char *pcap_check_invalid_chars(const char *path)
{
    size_t i, j;

    if (path == NULL) {
        return NULL;
    }
    for (i = 0; i < strlen(path); i++) {
        for (j = 0; j < sizeof(g_blacklist_chars); j++) {
            if (path[i] == g_blacklist_chars[j]) {
                return &path[i];
            }
        }
    }

    return NULL;
}

static const char *pcap_check_protected_path(const char *path)
{
    size_t i;
    size_t pattern_len;

    if (path == NULL) {
        return NULL;
    }
    for (i = 0; g_protected_paths[i] != NULL; i++) {
        if (strchr(g_protected_paths[i], '*') != NULL) {
            if (fnmatch(g_protected_paths[i], path, 0) == 0) {
                return g_protected_paths[i];
            }
        } else {
            pattern_len = strlen(g_protected_paths[i]);
            if (strncmp(path, g_protected_paths[i], pattern_len) == 0) {
                return g_protected_paths[i];
            }
        }
    }

    return NULL;
}

static inline uint32_t
pcap_masklen_to_netmask(uint8_t masklen)
{
    return  0xffffffff << (PCAP_OFFSET_32 - masklen);
}

static inline void
pcap_move_pkt_itr(struct pcap_pkt_parse_ctx *ctx, uint32_t len)
{
    ctx->pkt_itr += len;
}

static inline bool
pcap_check_pkt_boundary(const uint8_t *p_start, uint32_t size, const uint8_t *p_end)
{
    if ((p_start + size) <= p_end)
        return true;
    else
        return false;
}

static inline bool
pcap_is_pkt_ip(uint16_t eth_type)
{
    if (eth_type == htons(ETH_TYPE_IP))
        return true;

    if (eth_type == htons(ETH_TYPE_IPV6))
        return true;

    return false;
}

static int
pcap_ip_parse(char *value, struct pcap_ip_t *ip, uint32_t *ip_type)
{
    if (inet_addr(value) != INADDR_NONE) {
        ip->ip4 = inet_addr(value);
        *ip_type = PCAP_IP_ADDR_V4;
        return 0;
    }
    if (inet_pton(AF_INET6, value, &ip->ip6) == 1) {
        *ip_type = PCAP_IP_ADDR_V6;
        return 0;
    }
    return -1;
}

static int
pcap_mask_parse(const char *mask_str, uint8_t *mask_len, uint32_t ip_type)
{
    uint32_t len;
    char *endPtr = NULL;

    if (!mask_str) {
        if (ip_type == PCAP_IP_ADDR_V4)
            *mask_len = PCAP_IPV4_MAX_MASK_LEN;
        else
            *mask_len = PCAP_IPV6_MAX_MASK_LEN;

        return 0;
    }

    len = strtoul(mask_str, &endPtr, STR_TO_DEC_NUM);
    if (endPtr == NULL || *endPtr != '\0')
        return -1;

    if (ip_type == PCAP_IP_ADDR_V4) {
        if (len > 0 && len <= PCAP_IPV4_MAX_MASK_LEN) {
            *mask_len = (uint8_t)len;
            return 0;
        }
        return -1;
    }
    if (len > 0 && len <= PCAP_IPV6_MAX_MASK_LEN) {
        *mask_len = (uint8_t)len;
        return 0;
    }
    return -1;
}

int
pcap_ip_mask_parse(const char *value, struct pcap_ip_t *ip, uint8_t *mask_len, uint32_t *p_ip_type)
{
    int ret;
    uint32_t ip_type;
    char *copy = NULL;
    char *ip_str = NULL;
    char *mask_str = NULL;

    copy = hinic3_xstrdup(value, HINIC3_CAPTURE);
    if (copy == NULL) {
        HINIC3_LOG(ERR, CAPTURE, "Malloc value failed when parse ip and mask!");
        return -1;
    }

    ip_str = copy;
    mask_str = strchr(copy, '/');
    if (mask_str != NULL) {
        *mask_str = '\0';
        mask_str++;
    }

    ret = pcap_ip_parse(ip_str, ip, &ip_type);
    if (ret != 0) {
        HINIC3_LOG(ERR, CAPTURE, "Ip type is invalid, err is %d!", ret);
        hinic3_free(copy);
        return -1;
    }

    ret = pcap_mask_parse(mask_str, mask_len, ip_type);
    if (ret != 0) {
        HINIC3_LOG(ERR, CAPTURE, "Mask string is invalid, err is %d!", ret);
        hinic3_free(copy);
        return -1;
    }

    if (*p_ip_type == 0)
        *p_ip_type = ip_type;
    else if (ip_type != *p_ip_type) {
        HINIC3_LOG(ERR, CAPTURE, "Ip should all be ipv4 or ipv6, ip type %u is invalid!", ip_type);
        hinic3_free(copy);
        return -1;
    }

    hinic3_free(copy);
    return 0;
}

int
parse_l4_proto(const char *value, uint8_t *output)
{
    if (strcmp(value, "TCP") == 0)
        *output = IPPROTO_TCP;
    else if (strcmp(value, "UDP") == 0)
        *output = IPPROTO_UDP;
    else if (strcmp(value, "ICMP") == 0)
        *output = IPPROTO_ICMP;
    else if (strcmp(value, "SCTP") == 0)
        *output = IPPROTO_SCTP;
    else if (strcmp(value, "ICMPV6") == 0)
        *output = IPPROTO_ICMPV6;
    else {
        HINIC3_LOG(ERR, CAPTURE, "L4 protocol is invalid!");
        return -1;
    }

    return 0;
}

static int
pcap_key_count_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    char *endPtr = NULL;
    unsigned long long count;

    if ((cap_key->flags & PCAP_FLAG_KEY_COUNT) != 0) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR "Duplicate parameter \"%s\"!\n", key_name);
        return -1;
    }

    count = strtoull(value, &endPtr, STR_TO_DEC_NUM);
    if (endPtr == NULL || *endPtr != '\0') {
        HINIC3_LOG(ERR, CAPTURE, "Parameter count is invalid, err is %llu!", count);
        return -1;
    }

    if (count <= 0 || count > PCAP_MAX_PKT_CNT) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR "Value of parameter \"%s\" is invalid!\n", key_name);
        return -1;
    }

    cap_key->count = (uint32_t)count;
    cap_key->flags |= PCAP_FLAG_KEY_COUNT;
    return 0;
}

static int
pcap_key_filenum_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    char *endPtr = NULL;
    long long filenum;

    if ((cap_key->flags & PCAP_FLAG_KEY_FILENUM) != 0) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR "Duplicate parameter \"%s\"!\n", key_name);
        return -1;
    }

    filenum = strtoll(value, &endPtr, STR_TO_DEC_NUM);
    if (endPtr == NULL || *endPtr != '\0' || filenum < 1 || filenum > (long long)UINT32_MAX) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR "Value of parameter \"%s\" "
            "must be >= 1 and <= %u!\n", key_name, UINT32_MAX);
        return -1;
    }

    cap_key->filenum = (uint32_t)filenum;
    cap_key->flags |= PCAP_FLAG_KEY_FILENUM;
    return 0;
}

static int
pcap_key_file_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    const char *invalid_char = NULL;

    if ((cap_key->flags & PCAP_FLAG_KEY_FILE) != 0) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR "Duplicate parameter \"%s\"!\n", key_name);
        return -1;
    }

    if (strlen(value) >= PCAP_MAX_FILE_NAME) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR HINIC3_UI_FILE_PATH_LENGTH_TIPS_STRING);
        return -1;
    }

    if (strchr(value, '/') != NULL) {
        hinic3_ds_put_format(ds, HINIC3_UI_LEADING_SIGN_ERROR "Value of parameter \"%s\" contains '/'!\n", key_name);
        return -1;
    }

    invalid_char = pcap_check_invalid_chars(value);
    if (invalid_char != NULL) {
        hinic3_ds_put_format(ds, "%sfilename \"%s\" contains invalid character '%c'!\n",
            HINIC3_UI_LEADING_SIGN_ERROR, value, *invalid_char);
        return -1;
    }

    snprintf(cap_key->filename, sizeof(cap_key->filename), "%s", value);
    cap_key->flags |= PCAP_FLAG_KEY_FILE;
    return 0;
}

static int
pcap_key_dir_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    if ((cap_key->flags & PCAP_FLAG_KEY_DIRECTION) != 0) {
        hinic3_ds_put_format(ds, "%sDuplicate parameter \"%s\"!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    if (strcmp("in", value) == 0)
        cap_key->direction = PCAP_DIR_RX;
    else if (strcmp("out", value) == 0)
        cap_key->direction = PCAP_DIR_TX;
    else if (strcmp("inout", value) == 0)
        cap_key->direction = PCAP_DIR_TX | PCAP_DIR_RX;
    else {
        hinic3_ds_put_format(ds, "%sValue of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    cap_key->flags |= PCAP_FLAG_KEY_DIRECTION;
    return 0;
}

static int
pcap_key_host_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    int ret;

    if ((cap_key->flags & PCAP_FLAG_KEY_HOST) != 0) {
        hinic3_ds_put_format(ds, "%sDuplicate parameter \"%s\"!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    if ((cap_key->flags & PCAP_FLAG_KEY_SIP) != 0) {
        hinic3_ds_put_format(ds, "%sAlready config \"-sip\", cann't config \"%s\"!\n", HINIC3_UI_LEADING_SIGN_ERROR,
            key_name);
        return -1;
    }

    if ((cap_key->flags & PCAP_FLAG_KEY_DIP) != 0) {
        hinic3_ds_put_format(ds, "%sAlready config \"-dip\", cann't config \"%s\"!\n", HINIC3_UI_LEADING_SIGN_ERROR,
            key_name);
        return -1;
    }

    ret = pcap_ip_mask_parse(value, &cap_key->host_ip, &cap_key->host_masklen, &cap_key->ip_type);
    if (ret != 0) {
        hinic3_ds_put_format(ds, "%sValue of parameter \"%s\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    cap_key->flags |= PCAP_FLAG_KEY_HOST;
    return 0;
}

static void
pcap_normalize_path_slashes(char *path, size_t path_size)
{
    size_t i, j;

    for (i = 0, j = 0; path[i] != '\0' && j < path_size - 1; i++) {
        if (path[i] == '/' && path[i + 1] == '/') {
            continue;
        }
        path[j++] = path[i];
    }
    path[j] = '\0';
}

static int
pcap_validate_output_path_format(const char *key_name, const char *value, struct ds *ds)
{
    const char *invalid_char = NULL;

    if (strlen(value) >= PCAP_MAX_FILE_NAME) {
        hinic3_ds_put_format(ds, "%sPath too long: %s\n",
            HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    invalid_char = pcap_check_invalid_chars(value);
    if (invalid_char != NULL) {
        hinic3_ds_put_format(ds, "%spath \"%s\" contains invalid character '%c'!\n",
            HINIC3_UI_LEADING_SIGN_ERROR, value, *invalid_char);
        return -1;
    }

    if (value[0] != '/') {
        hinic3_ds_put_format(ds, "%spath must be absolute path!\n",
            HINIC3_UI_LEADING_SIGN_ERROR, value);
        return -1;
    }

    return 0;
}

static int pcap_key_output_parse(struct pcap_key_t *cap_key, const char *key_name,
    const char *value, struct ds *ds)
{
    char path_copy[PCAP_MAX_FILE_NAME];
    const char *protected_path = NULL;

    if (cap_key->flags & PCAP_FLAG_KEY_OUTPUT) {
        hinic3_ds_put_format(ds, "%sDuplicate parameter: %s\n",
            HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    if (pcap_validate_output_path_format(key_name, value, ds) != 0)
        return -1;

    snprintf(path_copy, sizeof(path_copy), "%s", value);

    pcap_normalize_path_slashes(path_copy, sizeof(path_copy));

    protected_path = pcap_check_protected_path(path_copy);
    if (protected_path != NULL) {
        hinic3_ds_put_format(ds, "%sprotected path \"%s\" is not allowed!\n",
            HINIC3_UI_LEADING_SIGN_ERROR, protected_path);
        return -1;
    }

    snprintf(cap_key->output_path, sizeof(cap_key->output_path), "%s", path_copy);
    cap_key->flags |= PCAP_FLAG_KEY_OUTPUT;
    return 0;
}

static int
pcap_sub_key_parse(struct pcap_key_t *cap_key, const char *key_name, const char *value, struct ds *ds)
{
    size_t i;
    int ret;
    struct pcap_sub_key_parser *item = NULL;
    pcap_sub_key_parse_func func = NULL;

    for (i = 0; i < ARRAY_SIZE(g_pcap_sub_key_parser); i++) {
        item = &g_pcap_sub_key_parser[i];
        if (strcmp(key_name, item->key_name) == 0) {
            func = item->func;
            break;
        }
    }

    if (!func) {
        hinic3_ds_put_format(ds, "%sIllegal parameter=%s!\n", HINIC3_UI_LEADING_SIGN_ERROR, key_name);
        return -1;
    }

    ret = func(cap_key, key_name, value, ds);
    return ret;
}

int
pcap_key_parse(struct pcap_key_t *cap_key, int argc, const char *argv[], struct ds *ds, struct ds *save_param)
{
    int i;
    int ret;
    int work_argc = argc;
    const char **work_argv = argv;

    work_argc -= 1;
    work_argv += 1;

    memset(cap_key, 0, sizeof(struct pcap_key_t));
    cap_key->direction = PCAP_DIR_RX | PCAP_DIR_TX;
    cap_key->filenum = 1;
    cap_key->count = PCAP_DEF_PKT_CNT;
    cap_key->count_total = (uint64_t)cap_key->count * cap_key->filenum;
    snprintf(cap_key->output_path, sizeof(cap_key->output_path), "%s",
        hinic3_get_default_directory());

    i = 0;
    while (i < work_argc) {
        /* -xxx模式的参数 */
        if (strcmp("-vxlan_inner", work_argv[i]) == 0) {
            cap_key->vxlan_inner = true;
            hinic3_ds_put_format(save_param, " %s", (const char *)work_argv[i]);
            i++;
            continue;
        }

        if (strcmp("-thread", work_argv[i]) == 0) {
            cap_key->write_way = WRITE_BY_SEP_THREAD;
            hinic3_ds_put_format(save_param, " %s", (const char *)work_argv[i]);
            i++;
            continue;
        }

        /* -x xxx模式的参数 */
        if (i + 1 >= work_argc) {
            hinic3_ds_put_format(ds, "%sWrong command format.\n", HINIC3_UI_LEADING_SIGN_ERROR);
            return -1;
        }

        ret = pcap_sub_key_parse(cap_key, (const char *)work_argv[i], (const char *)work_argv[i + 1], ds);
        if (ret != 0)
            return -1;

        if (strcmp(work_argv[i], "-w") == 0)
            hinic3_ds_put_format(save_param, " %s %s", (const char *)work_argv[i], cap_key->filename);
        else
            hinic3_ds_put_format(save_param, " %s %s", (const char *)work_argv[i], (const char *)work_argv[i + 1]);
        i += PCAP_DOUBLE;
    }

    if ((cap_key->flags & PCAP_FLAG_KEY_FILENUM) != 0 &&
        (cap_key->flags & PCAP_FLAG_KEY_COUNT) == 0) {
        HINIC3_LOG(WARNING, CAPTURE, "Option -n is used without explicit -c, "
            "will use default packet count(%u) per file!", PCAP_DEF_PKT_CNT);
    }

    if ((cap_key->flags & PCAP_FLAG_KEY_COUNT) != 0) {
        cap_key->count_total = (uint64_t)cap_key->count *
            ((cap_key->filenum > 0) ? (uint64_t)cap_key->filenum : 1ULL);
    } else if ((cap_key->flags & PCAP_FLAG_KEY_FILENUM) != 0) {
        cap_key->count_total = (uint64_t)cap_key->count * (uint64_t)cap_key->filenum;
    }

    ret = hinic3_build_output_absolute_path(cap_key->output_path, cap_key->filename,
        cap_key->absolute_path, sizeof(cap_key->absolute_path), ds);
    if (ret != 0)
        return -1;

    /* check whether neccesary parameters exist */
    if ((cap_key->flags & PCAP_FILTER_NECESSARY_MASK) != PCAP_FILTER_NECESSARY_MASK) {
        hinic3_ds_put_format(ds, "%s%s, should provide parameter \"-w filename\" and \"-t tasktime\"!\n", HINIC3_UI_LEADING_SIGN_ERROR,
            HINIC3_UI_ERROR_INCOMPLETE_COMMAND);
        return -1;
    }

    return 0;
}

int
pcap_show_param_parse(int argc, const char *argv[], struct pcap_show_param *param, struct ds *ds)
{
    if (strcmp("-pcap_id", argv[0]) == 0 && argc == PCAP_CMD_SHOW_MAX_PARAM) {
        char *endPtr = NULL;
        param->pcap_id = strtoul(argv[PCAP_CMD_STOP_MAX_PARAM - 1], &endPtr, STR_TO_DEC_NUM);
        if (endPtr == NULL || *endPtr != '\0')
            return -1;
        if (param->pcap_id == 0) {
            hinic3_ds_put_format(ds, "%sValue of parameter \"-pcap_id\" is invalid!\n", HINIC3_UI_LEADING_SIGN_ERROR);
            return -1;
        }
        return 0;
    }

    if (strcmp("all", argv[0]) == 0 && argc == PCAP_CMD_SHOW_MIN_PARAM) {
        param->is_all = true;
        return 0;
    }

    hinic3_ds_put_format(ds, "%sInvalid command format, please type -h or --help for help.\n",
        HINIC3_UI_LEADING_SIGN_ERROR);
    return -1;
}

static inline bool
pcap_have_any_filter(const struct pcap_key_t *key)
{
    if ((key->flags & PCAP_FILTER_ALL_MASK) != 0)
        return true;
    if (key->vxlan_inner)
        return true;
    return false;
}

static void
pcap_convert_key_l2(struct pcap_key_t *key, struct hinic3_pcap_probe_filter_t *filter)
{
    if ((key->flags & PCAP_FLAG_KEY_SMAC) != 0) {
        filter->cap_mask.smac_en = 1;
        memcpy(filter->rule_cfg.smac, key->smac, RTE_ETHER_ADDR_LEN);
    }

    if ((key->flags & PCAP_FLAG_KEY_DMAC) != 0) {
        filter->cap_mask.dmac_en = 1;
        memcpy(filter->rule_cfg.dmac, key->dmac, RTE_ETHER_ADDR_LEN);
    }

    if ((key->flags & PCAP_FLAG_KEY_VLAN) != 0) {
        filter->cap_mask.vlan_en = 1;
        filter->rule_cfg.vlan_id = key->vlan_id;
    }

    if ((key->flags & PCAP_FLAG_KEY_ETH_TYPE) != 0) {
        filter->cap_mask.eth_type_en = 1;
        filter->rule_cfg.eth_type = key->eth_type;
    }

    return;
}

static void
pcap_convert_key_l3(struct pcap_key_t *key, struct hinic3_pcap_probe_filter_t *filter)
{
    if ((key->flags & PCAP_FLAG_KEY_SIP) != 0) {
        filter->cap_mask.sip_en = 1;
        if (key->ip_type == PCAP_IP_ADDR_V4) {
            filter->rule_cfg.sip_mask = pcap_masklen_to_netmask(key->sip_masklen);
            filter->rule_cfg.sip = key->sip.ip4 & htonl(filter->rule_cfg.sip_mask);
        } else {
            filter->cap_mask.ipv6_en = 1;
            filter->rule_cfg.sip_mask = key->sip_masklen;
            memcpy(&(filter->rule_ipv6_cfg.sip6), &(key->sip.ip6), sizeof(struct in6_addr));
        }
    }

    if ((key->flags & PCAP_FLAG_KEY_DIP) != 0) {
        filter->cap_mask.dip_en = 1;
        if (key->ip_type == PCAP_IP_ADDR_V4) {
            filter->rule_cfg.dip_mask = pcap_masklen_to_netmask(key->dip_masklen);
            filter->rule_cfg.dip = key->dip.ip4 & htonl(filter->rule_cfg.dip_mask);
        } else {
            filter->cap_mask.ipv6_en = 1;
            filter->rule_cfg.dip_mask = key->dip_masklen;
            memcpy(&(filter->rule_ipv6_cfg.dip6), &(key->dip.ip6), sizeof(struct in6_addr));
        }
    }

    if ((key->flags & PCAP_FLAG_KEY_HOST) != 0) {
        filter->cap_mask.host = 1;
        if (key->ip_type == PCAP_IP_ADDR_V4) {
            filter->rule_cfg.host_mask = pcap_masklen_to_netmask(key->host_masklen);
            filter->rule_cfg.host_ip = key->host_ip.ip4 & htonl(filter->rule_cfg.host_mask);
        } else {
            filter->cap_mask.ipv6_en = 1;
            filter->rule_cfg.host_mask = key->host_masklen;
            memcpy(&(filter->rule_ipv6_cfg.hip6), &(key->host_ip.ip6), sizeof(struct in6_addr));
        }
    }

    if ((key->flags & PCAP_FLAG_KEY_IP_PROTO) != 0) {
        filter->cap_mask.proto_en = 1;
        filter->rule_cfg.ip_proto = key->ip_proto;
    }

    return;
}

static void
pcap_convert_key_l4(struct pcap_key_t *key, struct hinic3_pcap_probe_filter_t *filter)
{
    if ((key->flags & PCAP_FLAG_KEY_SPORT) != 0) {
        filter->cap_mask.sport_en = 1;
        filter->rule_cfg.sport = key->sport;
    }

    if ((key->flags & PCAP_FLAG_KEY_DPORT) != 0) {
        filter->cap_mask.dport_en = 1;
        filter->rule_cfg.dport = key->dport;
    }

    if ((key->flags & PCAP_FLAG_KEY_VXLAN_VNI) != 0) {
        filter->cap_mask.vni_en = 1;
        filter->rule_cfg.vni = key->vxlan_vni;
    }

    return;
}

void
pcap_convert_key_to_driver_filter(struct pcap_task_t *task)
{
    struct pcap_key_t *key = &task->key;
    struct hinic3_pcap_probe_filter_t *filter = &task->driver.driver_filter;

    /* count */
    filter->cap_mask.cnt_en = 1;
    filter->rule_cfg.pcap_cfg_cnt_h = key->count_total >> PCAP_OFFSET_32;
    filter->rule_cfg.pcap_cfg_cnt_l = key->count_total & (0xFFFFFFFF);

    /* direction */
    if ((key->direction & PCAP_DIR_RX) != 0)
        filter->cap_mask.direct_rx = 1;
    if ((key->direction & PCAP_DIR_TX) != 0)
        filter->cap_mask.direct_tx = 1;

    if (!pcap_have_any_filter(key) && (!key->vxlan_inner)) {
        filter->cap_mask.all_pass = 1;
        return;
    }

    pcap_convert_key_l2(key, filter);
    pcap_convert_key_l3(key, filter);
    pcap_convert_key_l4(key, filter);
    if (key->vxlan_inner)
        filter->cap_mask.vxlan_inner = 1;

    return;
}

static inline void
pcap_pkt_parse_not_ip(struct pcap_pkt_parse_ctx *ctx HINIC3_UNUSED, struct pcap_pkt_sub_hdr *sub_hdr)
{
    struct pcap_pkt_l2 *l2 = &sub_hdr->l2;

    if ((l2->eth_type == htons(ETH_TYPE_ARP)) || (l2->eth_type == htons(ETH_TYPE_RARP)))
        sub_hdr->save_len += HINIC3_ARP_ETH_HEADER_LEN;
}

static inline bool
pcap_is_ipv6_ext_hdr(uint8_t nexthdr)
{
    return (nexthdr == HINIC3_NEXTHDR_HOP) ||
           (nexthdr == HINIC3_NEXTHDR_ROUTING) ||
           (nexthdr == HINIC3_NEXTHDR_FRAGMENT) ||
           (nexthdr == HINIC3_NEXTHDR_AUTH) ||
           (nexthdr == HINIC3_NEXTHDR_NONE) ||
           (nexthdr == HINIC3_NEXTHDR_DEST);
}

static int
pcap_parse_ipv6_ext_hdr(struct pcap_pkt_parse_ctx *ctx, struct pcap_pkt_sub_hdr *sub_hdr,
                                   uint8_t nexthdr_para, bool *has_l4)
{
    uint16_t hdr_len;
    uint16_t hdr_cnt = 0;
    uint8_t xdr_auth_offset = 2;
    uint8_t xdr_bit_move = 3;
    struct hinic3_ipv6_opt_hdr *ext_hdr = NULL;
    struct pcap_pkt_l3 *l3 = &sub_hdr->l3;
    uint8_t nexthdr = nexthdr_para;

    while (pcap_is_ipv6_ext_hdr(nexthdr) && hdr_cnt < HINIC3_NEXTHDR_MAX) {
        if (nexthdr == HINIC3_NEXTHDR_NONE || nexthdr == HINIC3_NEXTHDR_AUTH)
            break;

        ext_hdr = (struct hinic3_ipv6_opt_hdr *)ctx->pkt_itr;
        if (!pcap_check_pkt_boundary(ctx->pkt_itr, sizeof(struct hinic3_ipv6_opt_hdr), ctx->pkt_end)) {
            HINIC3_LOG(ERR, CAPTURE, "Length of packet is too small when extract ipv6 ext header!");
            return -1;
        }

        if (nexthdr == HINIC3_NEXTHDR_FRAGMENT) {
            hdr_len = PCAP_IPV6_FRAG_HDR_SIZE;
            *has_l4 = false;
        } else if (nexthdr == HINIC3_NEXTHDR_AUTH)
            hdr_len = ((ext_hdr->hdrlen + xdr_auth_offset) << xdr_auth_offset);
        else
            hdr_len = ((ext_hdr->hdrlen + 1) << xdr_bit_move);

        if (hdr_len >= UINT8_MAX || !pcap_check_pkt_boundary(ctx->pkt_itr, hdr_len, ctx->pkt_end)) {
            HINIC3_LOG(ERR, CAPTURE, "Hdr len bigger than UINT8_MAX or ength of packet is too small when extract ipv6 ext header!");
            return -1;
        }

        hdr_cnt++;
        nexthdr = ext_hdr->nexthdr;
        sub_hdr->save_len += hdr_len;
        pcap_move_pkt_itr(ctx, hdr_len);
    }

    l3->ip_proto = nexthdr;
    sub_hdr->valid_flags |= PCAP_FLAG_KEY_IP_PROTO;
    return 0;
}

static int
pcap_pkt_parse_l2(struct pcap_pkt_parse_ctx *ctx, struct pcap_pkt_sub_hdr *sub_hdr)
{
    uint16_t eth_type;
    struct eth_header *eth = NULL;
    struct vlan_header *vh = NULL;
    struct pcap_pkt_l2 *l2 = &sub_hdr->l2;

    eth = (struct eth_header *)ctx->pkt_itr;
    if (!pcap_check_pkt_boundary(ctx->pkt_itr, ETH_HEADER_LEN, ctx->pkt_end)) {
        HINIC3_LOG(ERR, CAPTURE, "Length of packet is too small when extract ethernet header!");
        return -1;
    }

    eth_type = eth->eth_type;
    memcpy(l2->dmac, &eth->eth_dst, RTE_ETHER_ADDR_LEN);
    memcpy(l2->smac, &eth->eth_src, RTE_ETHER_ADDR_LEN);
    sub_hdr->valid_flags |= PCAP_FLAG_KEY_SMAC | PCAP_FLAG_KEY_DMAC;
    sub_hdr->save_len += ETH_HEADER_LEN;
    pcap_move_pkt_itr(ctx, ETH_HEADER_LEN);
    if (!eth_type_vlan(eth_type)) {
        l2->eth_type = eth_type;
        sub_hdr->valid_flags |= PCAP_FLAG_KEY_ETH_TYPE;

        return 0;
    }

    /* vlan packet */
    if (!pcap_check_pkt_boundary(ctx->pkt_itr, VLAN_HEADER_LEN, ctx->pkt_end)) {
        HINIC3_LOG(ERR, CAPTURE, "Length of packet is too small when extract vlan header!");
        return -1;
    }

    vh = (struct vlan_header *)ctx->pkt_itr;
    l2->vlan_id = ntohs(vh->vlan_tci) & 0xfff;
    sub_hdr->valid_flags |= PCAP_FLAG_KEY_VLAN;
    eth_type = vh->vlan_next_type;
    sub_hdr->save_len += VLAN_HEADER_LEN;
    pcap_move_pkt_itr(ctx, VLAN_HEADER_LEN);
    if (!eth_type_vlan(eth_type)) {
        l2->eth_type = eth_type;
        sub_hdr->valid_flags |= PCAP_FLAG_KEY_ETH_TYPE;
        return 0;
    }

    /* extrace inner vlan */
    if (!pcap_check_pkt_boundary(ctx->pkt_itr, sizeof(struct vlan_header), ctx->pkt_end)) {
        HINIC3_LOG(ERR, CAPTURE, "Length of packet is too small when extract inner vlan header!");
        return -1;
    }

    vh = vh + 1;
    l2->inner_vlan_id = ntohs(vh->vlan_tci) & 0xfff;
    l2->eth_type = vh->vlan_next_type;
    sub_hdr->valid_flags |= PCAP_FLAG_KEY_ETH_TYPE;
    sub_hdr->save_len += VLAN_HEADER_LEN;
    pcap_move_pkt_itr(ctx, VLAN_HEADER_LEN);
    return 0;
}

static int
pcap_pkt_parse_l3_ipv4(struct pcap_pkt_parse_ctx *ctx, struct pcap_pkt_sub_hdr *sub_hdr, bool *has_l4)
{
    uint32_t len;
    struct ip_header *iph = (struct ip_header *)ctx->pkt_itr;
    struct pcap_pkt_l3 *l3 = &sub_hdr->l3;

    if (!pcap_check_pkt_boundary(ctx->pkt_itr, sizeof(struct ip_header), ctx->pkt_end)) {
        HINIC3_LOG(ERR, CAPTURE, "Length of packet is too small when extract ip header!");
        return -1;
    }

    l3->ip_type = PCAP_IP_ADDR_V4;
    l3->ip_proto = iph->ip_proto;
    l3->sip.ip4 = hinic3_get_16aligned_be32(&iph->ip_src);
    l3->dip.ip4 = hinic3_get_16aligned_be32(&iph->ip_dst);
    sub_hdr->valid_flags |= PCAP_FLAG_KEY_IP_PROTO | PCAP_FLAG_KEY_SIP | PCAP_FLAG_KEY_DIP | PCAP_FLAG_KEY_HOST;

    len = IP_IHL(iph->ip_ihl_ver) * RTE_IPV4_IHL_MULTIPLIER;
    sub_hdr->save_len += len;
    pcap_move_pkt_itr(ctx, len);

    if (IP_IS_FRAGMENT(iph->ip_frag_off)) {
        if (iph->ip_frag_off == htons(IP_MORE_FRAGMENTS))
            *has_l4 = true;
        else
            *has_l4 = false;
    } else
        *has_l4 = true;

    return 0;
}

static int
pcap_pkt_parse_l3_ipv6(struct pcap_pkt_parse_ctx *ctx, struct pcap_pkt_sub_hdr *sub_hdr, bool *has_l4)
{
    int ret;
    uint8_t nexthdr;
    struct hinic3_ip6_header *ip6h = NULL;
    struct pcap_pkt_l3 *l3 = &sub_hdr->l3;

    ip6h = (struct hinic3_ip6_header *)(ctx->pkt_itr);
    if (!pcap_check_pkt_boundary(ctx->pkt_itr, HINIC3_IP6_HEADER_LEN, ctx->pkt_end)) {
        HINIC3_LOG(ERR, CAPTURE, "Length of packet is too small when extract ipv6 header!");
        return -1;
    }

    l3->ip_type = PCAP_IP_ADDR_V6;
    nexthdr = ip6h->nexthdr;
    memcpy(&l3->dip.ip6, &ip6h->daddr, sizeof(struct in6_addr));
    memcpy(&l3->sip.ip6, &ip6h->saddr, sizeof(struct in6_addr));
    sub_hdr->valid_flags |= PCAP_FLAG_KEY_SIP | PCAP_FLAG_KEY_DIP | PCAP_FLAG_KEY_HOST;
    sub_hdr->save_len += HINIC3_IP6_HEADER_LEN;
    pcap_move_pkt_itr(ctx, HINIC3_IP6_HEADER_LEN);

    /* extract all ipv6 extension headers */
    *has_l4 = true;
    ret = pcap_parse_ipv6_ext_hdr(ctx, sub_hdr, nexthdr, has_l4);
    if (ret != 0)
        return -1;

    return 0;
}

static int
pcap_pkt_parse_l3(struct pcap_pkt_parse_ctx *ctx, struct pcap_pkt_sub_hdr *sub_hdr, bool *has_l4)
{
    if (sub_hdr->l2.eth_type == htons(ETH_TYPE_IP))
        return pcap_pkt_parse_l3_ipv4(ctx, sub_hdr, has_l4);
    else
        return pcap_pkt_parse_l3_ipv6(ctx, sub_hdr, has_l4);
}

static int
pcap_pkt_parse_l4_udp(struct pcap_pkt_parse_ctx *ctx, struct pcap_pkt_sub_hdr *sub_hdr)
{
    struct pcap_pkt_l4 *l4 = &sub_hdr->l4;
    struct udp_header *udp = (struct udp_header *)(ctx->pkt_itr);

    if (!pcap_check_pkt_boundary(ctx->pkt_itr, sizeof(struct udp_header), ctx->pkt_end)) {
        HINIC3_LOG(ERR, CAPTURE, "Length of packet is too small when extract udp header!");
        return -1;
    }

    l4->sport = udp->udp_src;
    l4->dport = udp->udp_dst;
    sub_hdr->valid_flags |= PCAP_FLAG_KEY_SPORT | PCAP_FLAG_KEY_DPORT;
    sub_hdr->save_len += UDP_HEADER_LEN;
    pcap_move_pkt_itr(ctx, UDP_HEADER_LEN);
    return 0;
}

static int
pcap_pkt_parse_l4_tcp(struct pcap_pkt_parse_ctx *ctx, struct pcap_pkt_sub_hdr *sub_hdr)
{
    uint32_t tcp_len;
    struct pcap_pkt_l4 *l4 = &sub_hdr->l4;
    struct tcp_header *tcp = (struct tcp_header *)(ctx->pkt_itr);

    if (!pcap_check_pkt_boundary(ctx->pkt_itr, sizeof(struct tcp_header), ctx->pkt_end)) {
        HINIC3_LOG(ERR, CAPTURE, "Length of packet is too small when extract tcp header!");
        return -1;
    }

    l4->sport = tcp->tcp_src;
    l4->dport = tcp->tcp_dst;
    sub_hdr->valid_flags |= PCAP_FLAG_KEY_SPORT | PCAP_FLAG_KEY_DPORT;

    tcp_len = tcp_offset(tcp->tcp_ctl) << PCAP_OFFSET_2;
    if (tcp_len < TCP_HEADER_LEN)
        return -1;
    if (!pcap_check_pkt_boundary(ctx->pkt_itr, tcp_len, ctx->pkt_end)) {
        HINIC3_LOG(ERR, CAPTURE, "Length of packet is too small when extract tcp header!");
        return -1;
    }

    sub_hdr->save_len += tcp_len;
    pcap_move_pkt_itr(ctx, tcp_len);
    return 0;
}

static int
pcap_pkt_parse_l4_comm(struct pcap_pkt_parse_ctx *ctx, struct pcap_pkt_sub_hdr *sub_hdr)
{
    struct pcap_pkt_l4 *l4 = &sub_hdr->l4;

    if (!pcap_check_pkt_boundary(ctx->pkt_itr, PCAP_PORT_LEN * PCAP_DOUBLE, ctx->pkt_end)) {
        HINIC3_LOG(ERR, CAPTURE, "Length of packet is too small when extract l4 port!");
        return -1;
    }

    l4->sport = *((uint16_t *)ctx->pkt_itr);
    sub_hdr->valid_flags |= PCAP_FLAG_KEY_SPORT;
    pcap_move_pkt_itr(ctx, PCAP_PORT_LEN);

    l4->dport = *((uint16_t *)ctx->pkt_itr);
    sub_hdr->valid_flags |= PCAP_FLAG_KEY_DPORT;
    pcap_move_pkt_itr(ctx, PCAP_PORT_LEN);
    return 0;
}

static int
pcap_pkt_parse_l4(struct pcap_pkt_parse_ctx *ctx, struct pcap_pkt_sub_hdr *sub_hdr)
{
    int len;
    struct pcap_pkt_l3 *l3 = &sub_hdr->l3;

    if (l3->ip_proto == IPPROTO_TCP)
        return pcap_pkt_parse_l4_tcp(ctx, sub_hdr);

    if ((l3->ip_proto == IPPROTO_UDP) || (l3->ip_proto == IPPROTO_UDPLITE))
        return pcap_pkt_parse_l4_udp(ctx, sub_hdr);

    if (l3->ip_proto == IPPROTO_ICMP) {
        if (!pcap_check_pkt_boundary(ctx->pkt_itr, HINIC3_ICMP_HEADER_LEN, ctx->pkt_end)) {
            HINIC3_LOG(ERR, CAPTURE, "Length of packet is too small when extract icmp header!");
            return -1;
        }
        sub_hdr->save_len += HINIC3_ICMP_HEADER_LEN;
        return 0;
    }

    if (l3->ip_proto == IPPROTO_ICMPV6) {
        len = HINIC3_ICMP6_HEADER_LEN + HINIC3_ICMP6_MESSAGE_LEN;
        if (!pcap_check_pkt_boundary(ctx->pkt_itr, len, ctx->pkt_end)) {
            HINIC3_LOG(ERR, CAPTURE, "Length of packet is too small when extract icmp6 header!");
            return -1;
        }
        sub_hdr->save_len += (uint32_t)len;
        return 0;
    }

    if (l3->ip_proto == IPPROTO_IGMP) {
        if (!pcap_check_pkt_boundary(ctx->pkt_itr, HINIC3_IGMP_HEADER_LEN, ctx->pkt_end)) {
            HINIC3_LOG(ERR, CAPTURE, "Length of packet is too small when extract igmp header!");
            return -1;
        }
        sub_hdr->save_len += HINIC3_IGMP_HEADER_LEN;
        return pcap_pkt_parse_l4_comm(ctx, sub_hdr);
    }

    if (l3->ip_proto == IPPROTO_SCTP) {
        if (!pcap_check_pkt_boundary(ctx->pkt_itr, HINIC3_SCTP_HEADER_LEN, ctx->pkt_end)) {
            HINIC3_LOG(ERR, CAPTURE, "Length of packet is too small when extract sctp header!");
            return -1;
        }
        sub_hdr->save_len += HINIC3_SCTP_HEADER_LEN;
        return pcap_pkt_parse_l4_comm(ctx, sub_hdr);
    }

    return 0;
}

static int
pcap_pkt_parse_vxlan(struct pcap_pkt_parse_ctx *ctx, struct pcap_pkt_header *pkt_hdr,
                                struct pcap_pkt_sub_hdr *sub_hdr)
{
    struct hinic3_vxlanhdr *vxh = (struct hinic3_vxlanhdr *)(ctx->pkt_itr);

    if (!pcap_check_pkt_boundary(ctx->pkt_itr, sizeof(struct hinic3_vxlanhdr), ctx->pkt_end)) {
        HINIC3_LOG(ERR, CAPTURE, "Length of packet is too small when extract vxlan header!");
        return -1;
    }

    pkt_hdr->vxlan_vni = hinic3_get_16aligned_be32(&vxh->vx_vni);
    pkt_hdr->vxlan_vni = ((uint32_t)ntohl(pkt_hdr->vxlan_vni)) >> PCAP_VNI_TRANSFORM_OFFSET;
    sub_hdr->save_len += sizeof(struct hinic3_vxlanhdr);
    pcap_move_pkt_itr(ctx, sizeof(struct hinic3_vxlanhdr));
    return 0;
}

static int
pcap_pkt_parse_sub(struct pcap_pkt_parse_ctx *ctx, struct pcap_pkt_sub_hdr *sub_hdr, bool *is_vxlan)
{
    int ret;
    bool has_l4 = false;
    struct hinic3_dp_extend_info *extend_info = hinic3_get_offload_extend_info();
    if (extend_info == NULL) {
        HINIC3_LOG(ERR, CAPTURE, "The extend_info is null!");
        return -1;
    }
    struct hinic3_flow_agent_db *hinic3_db = (struct hinic3_flow_agent_db *)extend_info->hw_offload;
    if (hinic3_db == NULL) {
        HINIC3_LOG(ERR, CAPTURE, "The hinic3_db is null!");
        return -1;
    }

    ret = pcap_pkt_parse_l2(ctx, sub_hdr);
    if (ret != 0)
        return ret;

    if (!pcap_is_pkt_ip(sub_hdr->l2.eth_type)) {
        pcap_pkt_parse_not_ip(ctx, sub_hdr);
        return 0;
    }

    ret = pcap_pkt_parse_l3(ctx, sub_hdr, &has_l4);
    if (ret != 0)
        return ret;
    if (!has_l4)
        return 0;

    ret = pcap_pkt_parse_l4(ctx, sub_hdr);
    if (ret != 0)
        return ret;

    /* check if vxlan */
    if (is_vxlan) {
        if ((sub_hdr->valid_flags & PCAP_FLAG_KEY_DPORT) && (sub_hdr->l4.dport == htons(hinic3_db->vxlan_dst_port)))
            *is_vxlan = true;
        else
            *is_vxlan = false;
    }

    return 0;
}

void
pcap_pkt_parse(struct pcap_pkt_summary *pkt_summary)
{
    int ret;
    bool is_vxlan = false;
    struct pcap_pkt_sub_hdr *sub_hdr = NULL;
    struct pcap_pkt_parse_ctx *ctx = &pkt_summary->ctx;
    struct pcap_pkt_header *pkt_hdr = &pkt_summary->pkt_header;

    ctx->pkt_itr = pkt_summary->data;
    ctx->pkt_end = (uint8_t *)pkt_summary->data + pkt_summary->len;
    ctx->parse_result = true;

    /* outer protocol cluster */
    sub_hdr = &pkt_hdr->out_header;
    sub_hdr->save_head = ctx->pkt_itr;
    ret = pcap_pkt_parse_sub(ctx, sub_hdr, &is_vxlan);
    if (ret != 0) {
        ctx->parse_result = false;
        return;
    }

    pkt_hdr->is_vxlan = is_vxlan;
    if (!is_vxlan)
        return;

    /* inner protocol cluster */
    ret = pcap_pkt_parse_vxlan(ctx, pkt_hdr, sub_hdr);
    if (ret != 0) {
        ctx->parse_result = false;
        return;
    }

    sub_hdr = &pkt_hdr->inner_header;
    sub_hdr->save_head = ctx->pkt_itr;
    ret = pcap_pkt_parse_sub(ctx, sub_hdr, NULL);
    if (ret != 0) {
        ctx->parse_result = false;
        return;
    }

    return;
}

struct pcap_pkt_sub_hdr *
pcap_pkt_dst_hdr_get(struct pcap_pkt_summary *buf, const struct pcap_key_t *key)
{
    if (!key->vxlan_inner)
        return &buf->pkt_header.out_header;
    if (!buf->pkt_header.is_vxlan)
        return &buf->pkt_header.out_header;
    return &buf->pkt_header.inner_header;
}

static bool
pcap_pkt_nbits_cmp(uint8_t *src, uint8_t *dst, uint8_t bit_len)
{
    uint8_t byte_cnt;
    uint8_t left_bit_len;
    uint8_t last_byte_mask;

    if (src == NULL || dst == NULL)
        return false;

    byte_cnt = bit_len >> PCAP_OFFSET_3;
    left_bit_len = bit_len & PCAP_BIT_MASK;

    if (byte_cnt != 0) {
        if (memcmp(src, dst, byte_cnt) != 0)
            return false;
    }

    if (left_bit_len == 0)
        return true;

    last_byte_mask = (uint8_t)(0xff << (PCAP_BYTE_LEN - left_bit_len));
    if ((src[byte_cnt] & last_byte_mask) != (dst[byte_cnt] & last_byte_mask))
        return false;
    return true;
}

static inline bool
pcap_pkt_mem_cmp(struct pcap_mem_cmp_t *item)
{
    int ret;

    if (item->src == NULL || item->dst == NULL)
        return false;

    if (item->type == PCAP_CMP_MEM_BIT)
        return pcap_pkt_nbits_cmp(item->src, item->dst, item->len);

    ret = memcmp(item->src, item->dst, item->len);
    if (ret == 0)
        return true;
    else
        return false;
}

static inline bool
pcap_pkt_host_filter(struct pcap_pkt_sub_hdr *dst_hdr, struct pcap_key_t *key)
{
    bool flag = false;

    flag = pcap_pkt_nbits_cmp((uint8_t *)&dst_hdr->l3.sip, (uint8_t *)&key->host_ip, key->host_masklen);
    if (flag)
        return true;

    flag = pcap_pkt_nbits_cmp((uint8_t *)&dst_hdr->l3.dip, (uint8_t *)&key->host_ip, key->host_masklen);
    return flag;
}

static bool
pcap_pkt_detail_filter(struct pcap_pkt_header *hdr, struct pcap_pkt_sub_hdr *dst_hdr,
                                   struct pcap_key_t *key)
{
    size_t i;
    bool flag = false;
    struct pcap_mem_cmp_t *item = NULL;
    struct pcap_int_value_cmp_t *int_item = NULL;
    struct pcap_mem_cmp_t pcap_mem_cmp_map[] = {
        {PCAP_FLAG_KEY_SIP, (uint8_t*)&dst_hdr->l3.sip, (uint8_t*)&key->sip, key->sip_masklen, PCAP_CMP_MEM_BIT},
        {PCAP_FLAG_KEY_DIP, (uint8_t*)&dst_hdr->l3.dip, (uint8_t*)&key->dip, key->dip_masklen, PCAP_CMP_MEM_BIT},
        {PCAP_FLAG_KEY_SMAC, dst_hdr->l2.smac, key->smac, RTE_ETHER_ADDR_LEN, PCAP_CMP_MEM_BYTE},
        {PCAP_FLAG_KEY_DMAC, dst_hdr->l2.dmac, key->dmac, RTE_ETHER_ADDR_LEN, PCAP_CMP_MEM_BYTE},
    };

    struct pcap_int_value_cmp_t pcap_value_cmp_map[] = {
        {PCAP_FLAG_KEY_ETH_TYPE, dst_hdr->l2.eth_type, key->eth_type},
        {PCAP_FLAG_KEY_IP_PROTO, dst_hdr->l3.ip_proto, key->ip_proto},
        {PCAP_FLAG_KEY_SPORT, dst_hdr->l4.sport, key->sport},
        {PCAP_FLAG_KEY_DPORT, dst_hdr->l4.dport, key->dport},
        {PCAP_FLAG_KEY_VLAN, dst_hdr->l2.vlan_id, key->vlan_id},
        {PCAP_FLAG_KEY_VXLAN_VNI, hdr->vxlan_vni, key->vxlan_vni},
    };

    for (i = 0; i < ARRAY_SIZE(pcap_mem_cmp_map); i++) {
        item = &pcap_mem_cmp_map[i];
        if ((key->flags & item->filter) == 0)
            continue;
        flag = pcap_pkt_mem_cmp(item);
        if (!flag)
            return false;
    }

    for (i = 0; i < ARRAY_SIZE(pcap_value_cmp_map); i++) {
        int_item = &pcap_value_cmp_map[i];
        if ((key->flags & int_item->filter) == 0)
            continue;

        if (int_item->value1 != int_item->value2)
            return false;
    }

    /* special process for filter host */
    if ((key->flags & PCAP_FLAG_KEY_HOST) != 0) {
        flag = pcap_pkt_host_filter(dst_hdr, key);
        if (!flag)
            return false;
    }

    return true;
}

bool
pcap_pkt_filter(struct pcap_pkt_summary *buf, struct pcap_key_t *key)
{
    uint32_t tmp_flags;
    struct pcap_pkt_sub_hdr *dst_hdr;

    dst_hdr = pcap_pkt_dst_hdr_get(buf, key);
    tmp_flags = key->flags & PCAP_FILTER_ALL_MASK;
    if ((tmp_flags & dst_hdr->valid_flags) != tmp_flags)
        return false;

    if (tmp_flags == 0)
        return true;

    return pcap_pkt_detail_filter(&buf->pkt_header, dst_hdr, key);
}

FILE *
pcap_file_open(const char *file_name, const char *mode)
{
    FILE *file = NULL;

    if ((strcmp(mode, "wb") != 0) && (strcmp(mode, "ab") != 0)) {
        HINIC3_LOG(ERR, CAPTURE, "The mode %s to open file is wrong!", mode);
        return NULL;
    }

    file = fopen(file_name, mode);
    if (file == NULL) {
        HINIC3_LOG(ERR, CAPTURE, "pcap_file_open fail, file_name=%s, errno is %d!", file_name, errno);
        return NULL;
    }

    if (hinic3_user_scenario_get() != COM_BD) {
        // 如果非combd场景 需要改变文件用户组
        if (hinic3_agent_chown_output_file_path(file_name) != 0) {
            fclose(file);
            return NULL;
        }
    }

    if (hinic3_agent_chmod_output_file_path(file_name) != 0) {
        fclose(file);
        return NULL;
    }

    return file;
}

static int
pcap_generate_rotated_filename(const char *filename, char *new_filename, size_t name_size)
{
    const char *dot = strrchr(filename, '.');
    struct timeval tv;
    struct tm *tm_now;

    (void)gettimeofday(&tv, NULL);
    tm_now = localtime(&tv.tv_sec);
    if (tm_now == NULL) {
        HINIC3_LOG(ERR, CAPTURE, "pcap_file_rotate localtime fail!");
        return -1;
    }

    if (dot != NULL && dot != filename) {
        size_t base_len = dot - filename;
        snprintf(new_filename, name_size, "%.*s_%04d%02d%02d%02d%02d%02d%03ld.pcap",
            (int)base_len, filename,
            tm_now->tm_year + 1900,
            tm_now->tm_mon + 1,
            tm_now->tm_mday,
            tm_now->tm_hour,
            tm_now->tm_min,
            tm_now->tm_sec,
            tv.tv_usec / 1000);
    } else {
        snprintf(new_filename, name_size, "%s_%04d%02d%02d%02d%02d%02d%03ld",
            filename,
            tm_now->tm_year + 1900,
            tm_now->tm_mon + 1,
            tm_now->tm_mday,
            tm_now->tm_hour,
            tm_now->tm_min,
            tm_now->tm_sec,
            tv.tv_usec / 1000);
    }

    return 0;
}

static int
pcap_open_rotated_file(struct pcap_task_t *task)
{
    int ret;
    task->save_file = pcap_file_open(task->key.absolute_path, "wb");
    if (!task->save_file) {
        HINIC3_LOG(ERR, CAPTURE, "pcap_file_rotate open file %s fail!", task->key.absolute_path);
        return -1;
    }

    ret = pcap_file_write_header(task->save_file);
    if (ret != 0) {
        (void)fclose(task->save_file);
        task->save_file = NULL;
        return -1;
    }

    return 0;
}

int
pcap_file_rotate(struct pcap_task_t *task)
{
    int ret;
    char new_filename[PCAP_MAX_FILE_NAME];

    if (task->filenum <= 1)
        return 0;

    ret = fflush(task->save_file);
    if (ret != 0) {
        HINIC3_LOG(ERR, CAPTURE, "fflush fail before rotate, err is %d!", ret);
        return -1;
    }

    (void)fclose(task->save_file);
    task->save_file = NULL;

    ret = pcap_generate_rotated_filename(task->key.filename, new_filename, sizeof(new_filename));
    if (ret != 0)
        return -1;

    ret = hinic3_get_absolute_file_path(task->key.output_path, new_filename,
        task->key.absolute_path, sizeof(task->key.absolute_path));
    if (ret != 0)
        return -1;

    ret = pcap_open_rotated_file(task);
    if (ret != 0)
        return -1;

    task->file_pkt_cnt = 0;
    return 0;
}

int
pcap_file_write_header(FILE *file)
{
    ssize_t ret;
    struct pcap_hdr ph;
    struct iovec iov_f;

    ph.magic_num = PCAP_MAGIC_NUMBER;
    ph.ver_major = PCAP_VERSION_MAJOR;
    ph.ver_minor = PCAP_VERSION_MINOR;
    ph.local_zone = 0;
    ph.acc_ts = 0;
    ph.max_pkt_len = PCAP_SNAPLEN;
    ph.link_type = PCAP_LINKTYPE;             /* Ethernet */

    iov_f.iov_base = &ph;
    iov_f.iov_len = sizeof(struct pcap_hdr);
    ret = writev(fileno((FILE *)file), &iov_f, 1);
    if (ret < 0) {
        HINIC3_LOG(ERR, CAPTURE, "pcap_file_write_header fail, errno is %d!", errno);
        return -1;
    }

    return 0;
}

int
pcap_rte_mempool_get(struct rte_mempool *mp, void **obj_p)
{
    return rte_mempool_get(mp, obj_p);
}

void
pcap_rte_mempool_put(struct rte_mempool *mp, void *obj)
{
    rte_mempool_put(mp, obj);
}

void
pcap_rte_mempool_put_bulk(struct rte_mempool *mp, void * const *obj_table, unsigned int n)
{
    rte_mempool_put_bulk(mp, obj_table, n);
}

unsigned int
pcap_rte_ring_enqueue_burst(struct rte_ring *r, void * const *obj_table, unsigned int n,
                                         unsigned int *free_space)
{
    return rte_ring_enqueue_burst(r, obj_table, n, free_space);
}

unsigned int
pcap_rte_ring_dequeue_burst(struct rte_ring *r, void **obj_table, unsigned int n, unsigned int *available)
{
    return rte_ring_dequeue_burst(r, obj_table, n, available);
}

unsigned int
pcap_rte_ring_count(const struct rte_ring *r)
{
    return rte_ring_count(r);
}
