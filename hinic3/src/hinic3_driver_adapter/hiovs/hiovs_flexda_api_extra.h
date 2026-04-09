/*
 * 版权所有 (c) 华为技术有限公司 2022-2023
 * 功能描述: hiovs_flexda_api_extra 相关结构体头文件
 *  * 创建日期: 2025-11-17
 *  */

#ifndef HIOVS_FLEXDA_API_EXTRA_H
#define HIOVS_FLEXDA_API_EXTRA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "rte_mtr.h"
#include "hinic3_packets_types.h"
#include "hinic3_flow_agent_enum.h"

/* 名称的最大长度 */
#define HOVS_FLEXDA_FIELD_TYPE_NAME_MAX_LENGTH 64  /* field名称的最大长度 */
#define HOVS_FLEXDA_ACTION_TYPE_NAME_MAX_LENGTH 64 /* action名称的最大长度 */
#define HOVS_FLEXDA_FLOWTABLE_NAME_MAX_LENGTH 64   /* table名称的最大长度 */
#define HOVS_FLEXDA_STRUCT_MEMBER_NAME_MAX_LENGTH 64 /* 自定义key和action结构体的member名称最大长度 */
#define HOVS_FLEXDA_PCAP_ITEM_NAME_MAX_LENGTH 64   /* pcap item名称的最大长度 */

/* Action执行阶段 */
#define HOVS_FLEXDA_ACTION_STAGE_PRE_ACTION 0  /* 前置action，，STL执行 */
#define HOVS_FLEXDA_ACTION_STAGE_ACTION 1      /* 中置action，，STF执行 */
#define HOVS_FLEXDA_ACTION_STAGE_POST_ACTION 2 /* 后置action，，报文发送后执行 */

/* Action排序方式 */
#define HOVS_FLEXDA_ACTION_ORDERING_DEFAULT_BY_STAGE 0 /* 默认优先级且按照阶段排序 */
#define HOVS_FLEXDA_ACTION_ORDERING_PRIORITIZE_BY_TYPE 1 /* 高优先级且按照ID顺序 */

/* 流表类型 */
#define HOVS_FLEXDA_FLOWTABLE_TYPE_MAIN_TABLE 0             /* 主表 */
#define HOVS_FLEXDA_FLOWTABLE_TYPE_PRE_FUZZY_TABLE 1        /* 模糊前辅表 */
#define HOVS_FLEXDA_FLOWTABLE_TYPE_PRE_EXACT_TABLE 2        /* 精确前辅表 */
#define HOVS_FLEXDA_FLOWTABLE_TYPE_POST_FUZZY_TABLE 3       /* 模糊后辅表 */
#define HOVS_FLEXDA_FLOWTABLE_TYPE_POST_EXACT_TABLE 4       /* 精确后辅表 */
#define HOVS_FLEXDA_FLOWTABLE_TYPE_RELATED_DATA_TABLE 5     /* 关联数据表 */

/* 流表子类型 */
#define HOVS_FLEXDA_FLOWTABLE_SUBTYPE_MIRROR_TABLE 0  /* 流镜像表 */

typedef struct {
    char struct_member_name[HOVS_FLEXDA_STRUCT_MEMBER_NAME_MAX_LENGTH];  /* dump的结构体元素名称 */
    uint16_t bit_offset;                                              /* 位偏移 */
    uint16_t bit_width;                                               /* 位宽 */
    uint16_t dump_format;                                             /* dump格式类型 */
} hovs_flexda_config_dump_member_info_t;

typedef struct {
    uint16_t dump_format;                                             /* dump格式类型 */
    uint16_t struct_member_num;                                       /* dump的结构体元素数量 */
    hovs_flexda_config_dump_member_info_t *struct_members;               /* 结构体元素的dump信息 */
} hovs_flexda_config_dump_info_t;

/* Action数据长度范围 */
typedef struct {
    uint16_t min_length;
    uint16_t max_length;
} hovs_flexda_action_data_length_t;

/* Action信息结构体 */
typedef struct {
    uint16_t action_type;                                           /* action类型ID，不允许重复 */
    hovs_flexda_action_data_length_t data_length;                     /* 数据长度，定长则min==max，变长则min<max */
    uint8_t name_length ;                                           /* 名称实际长度 */
    char action_type_name[HOVS_FLEXDA_ACTION_TYPE_NAME_MAX_LENGTH];   /* action名称，不允许重复 */
    hovs_flexda_config_dump_info_t action_dump_info;                  /* action dump信息 */
} hovs_flexda_config_info_action_attr_t;

/* flexda配置文件结构：Action部分 */
typedef struct {
    int16_t action_num;
    hovs_flexda_config_info_action_attr_t **action_array;
} hovs_flexda_config_info_action_list_t;

/* Field信息结构体 */
typedef struct {
    uint16_t field_type;                                            /* field类型ID，不允许重复 */
    uint16_t data_length;                                           /* field */
    uint8_t name_length ;                                           /* 名称实际长度 */
    char field_type_name[HOVS_FLEXDA_FIELD_TYPE_NAME_MAX_LENGTH];     /* field名称，不允许重复 */
    hovs_flexda_config_dump_info_t field_dump_info;                   /* field dump信息 */
} hovs_flexda_config_info_field_attr_t;

/* flexda配置文件结构：Field部分 */
typedef struct {
    int16_t field_num;
    hovs_flexda_config_info_field_attr_t **field_array;
} hovs_flexda_config_info_field_list_t;

typedef struct {
    uint32_t table_id;                                      /* 流表ID，不允许重复 */
    uint32_t max_entry_num;                                 /* 流表容量配置,流表类型为关联数据表且子表为流镜像表时表示流镜像表的规模 */
    uint16_t table_type;                                    /* 流表类型，0主表，1模糊前辅表，2精确前辅表，3模糊后辅表，4精确后辅表，5关联数据表 */
    uint16_t table_sub_type;                                /* 流表子类型，目前仅关联数据表需要提供，0流镜像表 */
    uint8_t aging_enabled;                                  /* 是否开启流表老化，1开启，0关闭 仅主表有效 */
    uint32_t aging_time_ms;                                 /* 老化时间，单位：毫秒   仅主表有效 */
    uint8_t name_length;                                    /* 流表名称实际长度 */
    char table_name[HOVS_FLEXDA_FLOWTABLE_NAME_MAX_LENGTH];   /* 流表名称，不允许重复 */
} hovs_flexda_config_info_flowtable_config_t;

/* flexda配置文件结构：Table配置 */
typedef struct {
    hovs_flexda_config_info_flowtable_config_t table_info;
    hovs_flexda_config_info_field_list_t field_list;
    hovs_flexda_config_info_action_list_t action_list;
} hovs_flexda_config_flowtable_info_t;

typedef struct {
    int16_t table_num;
    hovs_flexda_config_flowtable_info_t **table_array;
} hovs_flexda_config_info_t;

/* flexda ovs adapter所需结构体*/
struct hinic3_flexda_ovs_info_t {
    unsigned pmd_core_id;
    struct rte_flow *mega_flow;
    uint16_t input_port_id;
    uint8_t input_port_type;
    bool is_ct;
    bool is_recirc;
    uint8_t max_key_size;
    uint8_t max_action_size;
    struct dp_packet *pkt;
    struct rte_flow_action *src_actions;
};

#define HOVS_MAX_TLV_BUF_LEN        2048
#define MAX_PHY_DEV_NUM             1024
#define HWPT_DRV_TYPE_SHIFT         1
#define HINIC3_MAX_SESSION_ONE_FLOW  2

enum tag_hiovs_install_mode {
    HIOVS_DPDK_MODE = 1, /* *< Hinic3 offload OVS only support DPDK_MODE. */
    HIOVS_MAX_MODE
};

enum hiovs_work_mode {
    HIOVS_WORK_MODE_DEFAULT,          /* *< Hinic3 offload OVS works in default mode. */
    HIOVS_WORK_MODE_BMGW,             /* *< Hinic3 offload OVS works in BMGW mode. */
    HIOVS_WORK_MODE_PT_CONTAINER = 3, /* *< hinic3 offload OVS works in Container Pass-through mode. */
    HIOVS_WORK_MODE_ZERO_VM_PT        /* *< Hinic3 offload OVS works in Zero VM Pass-through mode. */
};

struct hovs_melem {
    void *addr;       /* *< virtual address */
    uint64_t iova;    /* *< IO address */
    uint64_t page_sz; /* *< page size of underlying memory */
    int socket_id;    /* *< NUMA socket ID */
    int rsvd;
};

struct hovs_mbuf {
    void *buf_addr;    /* *< Virtual address of segment buffer. */
    uint64_t buf_iova; /* *< physical address */

    uint16_t data_off;
    volatile int16_t refcnt_atomic;
    uint16_t nb_segs; /* *< Number of segments. */
    uint16_t port;

    uint64_t ol_flags; /* *< Offload features. */

    uint32_t packet_type; /* *< L2/L3/L4 and tunnel information. */

    uint32_t pkt_len;  /* *< Total pkt len: sum of all segments. */
    uint16_t data_len; /* *< Amount of data in segment buffer. */
    uint16_t vlan_tci; /* *< VLAN TCI (CPU order), valid if PKT_RX_VLAN is set. */

    uint64_t rss; /* *< RSS hash result if RSS enabled */

    uint16_t vlan_tci_outer; /* *< Outer VLAN TCI (CPU order), valid if PKT_RX_QINQ is set. */
    uint16_t buf_len;        /* *< Length of segment buffer. */

    void *pool;             /* *< Pool from which mbuf was allocated. */
    struct hovs_mbuf *next; /* *< Next segment of scattered packet. */

    uint64_t tx_offload;

    void *shinfo;

    uint16_t priv_size;
    uint16_t timesync;
    uint32_t dynfield0;

    uint64_t udata64; /* *< Allow 8-byte userdata on 32-bit */

    uint32_t internal;     /* *< internal use mbuf, app set it to 0 */
    uint32_t dynfield1[5]; /* *< Reserved for dynamic fields. */
};

struct hovs_callback_ops {
    int (*hovs_malloc)(const char *type, size_t size, int socket_arg, unsigned int flags, size_t align, size_t bound,
        bool contig, struct hovs_melem *mem);
    int (*hovs_mfree)(void *addr);

    struct hovs_mbuf *(*hovs_mbuf_alloc)(void *mp);
    int (*hovs_mbuf_alloc_bulk)(void *mp, struct hovs_mbuf **mbufs, uint32_t count);
    void (*hovs_mbuf_free)(struct hovs_mbuf *m);
};

struct hovs_callback_ext_ops {
    void *(*hovs_malloc_socket)(const char *type, size_t size, uint32_t align, int32_t socket_arg);
    void (*hovs_free)(void *addr);

    const void *(*hovs_memzone_reserve)(const char *name, size_t len, int socket_id, uint32_t flags);
    const void *(*hovs_memzone_reserve_aligned)(const char *name, size_t len,
        int socket_id, uint32_t flags, uint32_t align);
    int (*hovs_memzone_free)(void *mz);
    const struct rte_memzone *(*hovs_memzone_lookup)(const char *name);
};

struct hiovs_lib_arg {
    enum tag_hiovs_install_mode mode;   /**< hiovs lib only support HIOVS_DPDK_MODE */
    uint8_t ext_cb_flag;                /**< extend callback flag */
    uint8_t ext_mp_flag;                /**< whether use external mempool */
    uint8_t mega_flow_mode;             /**< hiovs lib supports HINIC3_MEGA_FLOW_MODE_CLOUD and HINIC3_MEGA_FLOW_MODE_COMPUTE*/
    uint8_t clean_qos;                  /**< hiovs lib clean qos */
    void *mempool;                      /**< packet memory pool */
    uint32_t role;                      /**< for hot replacement: 0-INVALID, 1-OLD, 2-NEW.
                                             Set this paramenter to OLD when the lib is started for the first time.
                                             Set this paramenter to NEW when the lib is started for hot replacement. */
    uint32_t state;                     /**< Indicates whether to clear resources when lib initialization fails.
                                             0-Do not clean resources, 1-Clean resources when init fail. */
    enum hiovs_work_mode work_mode;     /**< Indicates hinic3 offload OVS work mode. */
    uint32_t rx_thread_num;             /**< Indicates the number of thread instances for hardware flow offloading. */
    uint32_t flow_num;                  /**< Indicates the number of hareware offload flows. The unit is M. */

    uint32_t data_room_size;            /**< data room size of member 'mempool' and 'fm_mempool' */
    struct hovs_callback_ops cb_ops;    /**< member pointer should not be null */
    void *fm_mempool;                   /**< flow monitor memory pool */
    uint16_t bond_rx_depth;             /**< bond mgmt pf upcall queue depth */
    uint16_t bond_tx_depth;             /**< bond mgmt pf reinject queue depth */
    uint16_t vport_rx_depth;            /**< vport mgmt pf upcall queue depth */
    uint16_t vport_tx_depth;            /**< vport mgmt pf reinject queue depth */
    uint16_t dp_hash_flow_num;
    uint16_t max_lcore;
    uint32_t reserved[4];                /**< reserved data */
    struct hovs_callback_ext_ops ext_cb_ops;
};

enum hiovs_hwpt_phy_dev_type {
    HWPT_PHY_DEV_TYPE_PF, /* *< Device type PF. */
    HWPT_PHY_DEV_TYPE_VF, /* *< Device type VF. */
};

enum hovs_qos_dir {
    QOS_DIR_INGRESS,
    QOS_DIR_EGRESS,
    QOS_DIR_INGRESS_EGRESS,
    QOS_DIR_MAX
};

enum hovs_qos_limit_type {
    QOS_LIMIT_BANDWIDTH,
    QOS_LIMIT_PPS,
    QOS_LIMIT_MAX
};

enum hinic3_netdev_features {
    HINIC3_NETDEV_F_10MB_HD = 1 << 0,     /* *< 10 Mb half-duplex rate support. */
    HINIC3_NETDEV_F_10MB_FD = 1 << 1,     /* *< 10 Mb full-duplex rate support. */
    HINIC3_NETDEV_F_100MB_HD = 1 << 2,    /* *< 100 Mb half-duplex rate support. */
    HINIC3_NETDEV_F_100MB_FD = 1 << 3,    /* *< 100 Mb full-duplex rate support. */
    HINIC3_NETDEV_F_1GB_HD = 1 << 4,      /* *< 1 Gb half-duplex rate support. */
    HINIC3_NETDEV_F_1GB_FD = 1 << 5,      /* *< 1 Gb full-duplex rate support. */
    HINIC3_NETDEV_F_10GB_FD = 1 << 6,     /* *< 10 Gb full-duplex rate support. */
    HINIC3_NETDEV_F_40GB_FD = 1 << 7,     /* *< 40 Gb full-duplex rate support. */
    HINIC3_NETDEV_F_100GB_FD = 1 << 8,    /* *< 100 Gb full-duplex rate support. */
    HINIC3_NETDEV_F_1TB_FD = 1 << 9,      /* *< 1 Tb full-duplex rate support. */
    HINIC3_NETDEV_F_OTHER = 1 << 10,      /* *< Other rate, not in the list. */
    HINIC3_NETDEV_F_COPPER = 1 << 11,     /* *< Copper medium. */
    HINIC3_NETDEV_F_FIBER = 1 << 12,      /* *< Fiber medium. */
    HINIC3_NETDEV_F_AUTONEG = 1 << 13,    /* *< Auto-negotiation. */
    HINIC3_NETDEV_F_PAUSE = 1 << 14,      /* *< Pause. */
    HINIC3_NETDEV_F_PAUSE_ASYM = 1 << 15, /* *< Asymmetric pause. */

    HINIC3_NETDEV_F_25GB_FD = 1 << 31, /* *< 25 Gb full-duplex rate support. */
};

/**
 * Port type.
 */
enum hovs_port_type {
    HOVS_PORT_TYPE_HWBOND = 14, /* *< port which is connected to hwbond */
    HOVS_PORT_TYPE_VIRTIO_VF,   /* *< port which is connected to vhostagent */
    HOVS_PORT_TYPE_HWPT,        /* *< port which is worked in hardware pass-through mode */
};

enum hovs_key_format {
    HOVS_KEY_FMT_4TUPLE, /* *< Key = LOCALTAG + SMAC + DMAC + ETHER_TYPE */
    HOVS_KEY_FMT_6TUPLE, /* *< Key = LOCALTAG + SMAC + DMAC + ETHER_TYPE + SIP + DIP */
    HOVS_KEY_FMT_9TUPLE, /* *< Key = LOCALTAG + SMAC + DMAC + ETHER_TYPE + SIP + DIP + PROTOCOL + SPORT + DPORT */
};

struct hinic3_flexda_config_info_t {
    hovs_flexda_config_info_t *flow_config;
    uint16_t key_num;
    uint16_t action_num;
    uint32_t total_flow_num;
};

struct hiovs_mirror_info {
    uint32_t sessions[HINIC3_MAX_SESSION_ONE_FLOW];  // tx方向session_id在下标为1的位置，rx方向在下标为0的位置
    union {
        struct {
            uint32_t mirror_rx          : 1;
            uint32_t mirror_tx          : 1;
            uint32_t sport              : 16;
            uint32_t rsvd               : 4;
            uint32_t sampling_interval  : 10;
        } ws;
        struct {
            uint32_t mirror_rx          : 1;
            uint32_t mirror_tx          : 1;
            uint32_t sport              : 16;
            uint32_t rsvd               : 4;
            uint32_t sampling_interval  : 10;
        } bs;
        uint32_t value;
    };
};

struct hovs_high_priority_protocol_cfg {
    uint16_t protocol_l2;
    uint16_t protocol_l3;
    uint16_t reserve0;
    uint16_t reserve1;
};

struct hovs_qos_action {
    uint16_t qos_type : 2;
    uint16_t qos_id : 10;
    uint16_t qos_flag : 1;
    uint16_t rsvd : 3;
};

struct hiovs_mirror_session_info {
    uint8_t enabled : 1;
    uint8_t is_vlan : 1;
    uint8_t is_ipv4 : 1;
    uint8_t type    : 1; // 0:vxlan镜像 1:GRE镜像
    uint8_t is_shim_header : 1;
    uint8_t rsvd0   : 3;
    uint8_t session_id;
    uint8_t cutoff_len; // 镜像报文截断长度，0代表不截断
    uint8_t shim_header_len; // shim_header长度
    uint16_t dest_port;
    uint16_t vlan_tag;
    uint8_t smac[ETH_ALEN];
    uint8_t dmac[ETH_ALEN];
    uint32_t sip[4];
    uint32_t dip[4];
    union {
        struct {
            uint32_t rsvd2 : 8;
            uint32_t vxlan_rsvd0 : 24;
            uint32_t vni : 24;
            uint32_t rsvd3 : 8;
        } vxlan;
        struct {
            uint16_t c_rsvd0_ver;
            uint16_t protocol;
        } gre;
        struct {
            uint32_t flag : 8;
            uint32_t vxlan_rsvd0 : 16;
            uint32_t protocol : 8;
            uint32_t vni : 24;
            uint32_t rsvd3 : 8;
            uint32_t data[5]; //shim_header具体内容
        } vxlan_shim_header;
    } tunnel;
};

enum {
    HOVS_ENG_CAP_F_BATCH_DEL,
    HOVS_ENG_CAP_F_OFFLOAD_RAW_IP,
    HOVS_ENG_CAP_F_OFFLOAD_ETHERNET,
    HOVS_ENG_CAP_F_STATS_SYNC_PULL,
    HOVS_ENG_CAP_F_OFFLOAD_ICMP,
    HOVS_ENG_CAP_F_OFFLOAD_IPV6,
    HOVS_ENG_CAP_F_OFFLOAD_ICMPV6,
    HOVS_ENG_CAP_F_OFFLOAD_3TUPLE,
    HOVS_ENG_CAP_F_ASYNC_FLUSH,
    HOVS_ENG_CAP_F_MAX
};

typedef enum tag_ovs_vport_type {
    OVS_VPORT_TYPE_VF = 1,
    OVS_VPORT_TYPE_PF, /* not used */
    OVS_VPORT_TYPE_BOND,
    OVS_VPORT_TYPE_MAX
} ovs_vport_type_e;

struct hovs_flexda_flow_stats {
    uint64_t packet_count; /* *< Number of packets matched. */
    uint64_t byte_count;   /* *< Number of bytes matched. */
    uint64_t ct_loss_pkts; /* *< Number of packets dropped for CT failure. */
    uint32_t live_time;    /* *< Remain life time of flow, unit : ms */
    uint32_t age_time;     /* *< Total life time of flow, unit : ms */
    uint16_t tcp_flags;
    uint8_t block[128];
};

struct hovs_flexda_dpif_flow_for_get {
    struct nlattr *key;     /* *< Flow to get */
    size_t key_len;         /* *< length of key in bytes */
    struct nlattr *mask;    /* *< mask to get */
    size_t mask_len;        /* *< length of mask in bytes */
    struct nlattr *actions; /* *< actions to perform on flow */
    size_t action_len;
    uint8_t mask_present;
    uint8_t err_code;
    uint8_t rsvd[6];
    uint64_t hw_ufid;
    uint64_t related_hw_ufid;
    struct hovs_flexda_flow_stats stats;
};

typedef void* (*hinic3_malloc_fn)(size_t size, enum hinic3_module module_id);
typedef void* (*hinic3_calloc_fn)(size_t num, size_t size, enum hinic3_module module_id);
typedef void (*hinic3_free_fn)(void *ptr);
typedef void* (*dp_packet_data_fn)(const struct dp_packet *b);
typedef uint32_t (*dp_packet_size_fn)(const struct dp_packet *b);
typedef struct rte_mbuf* (*dp_packet_get_mbuf_addr_fn)(const struct dp_packet *b);
typedef int (*hinic3_log_flexda_fn)(uint32_t level, uint32_t module, const char *format, ...);
typedef int (*hinic3_log_module_register_fn)(char *name);

/* flexda ovs adapter 所需函数指针结构体*/
typedef struct hinic3_flexda_ovs_ops_t {
    hinic3_malloc_fn hinic3_malloc;
    hinic3_calloc_fn hinic3_calloc;
    hinic3_free_fn hinic3_free;
    dp_packet_data_fn dp_packet_data;
    dp_packet_size_fn dp_packet_size;
    dp_packet_get_mbuf_addr_fn dp_packet_get_mbuf_addr;
    hinic3_log_flexda_fn hinic3_log_flexda;
    hinic3_log_module_register_fn hinic3_log_module_register;
}hinic3_flexda_ovs_ops_t;
#endif