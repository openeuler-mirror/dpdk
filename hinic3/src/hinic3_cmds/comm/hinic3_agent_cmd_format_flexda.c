/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2021 Huawei Technologies Co., Ltd
 */

#include "hinic3_agent_cmd_format.h"
#include "hinic3_agent_cmd_format_flexda.h"
#include "hinic3_util.h"
#include "hinic3_eth_packets.h"
#include "hinic3_ui_string.h"
#include "hinic3_log.h"
#include "hinic3_util.h"
#include "hinic3_packets_types.h"
#include "hinic3_ds.h"
#include "hinic3_packets.h"
#include "hinic3_string_util.h"
#include "hinic3_parse_agent_config.h"
#include "hinic3_offload_flow_port.h"
#include "hinic3_flow_agent_public.h"
#include "hinic3_agent_flow_cmd_dump.h"
#include "hinic3_flow_agent.h"

#define HINIC3_DFLEXDA_DUMP_VNI_PADDING 8
#define HINIC3_MEGA_PORT_IFINDEX_MASK 0xFFFF

#define HINIC3_FLOW_QOS_BW 0b01
#define HINIC3_FLOW_QOS_PPS 0b10
#define HINIC3_FLOW_QOS_BW_PPS 0b11

static void
hinic3_flexda_flow_key_format_output_src_port(const hinic3_nlattr_itr nla, struct ds *ds, int type)
{
    uint8_t u8_val = hinic3_nlattr_get_itr_u8(nla);
    uint16_t u16_val = hinic3_nlattr_get_itr_u16(nla);
    if (type == HINIC3_FLOW_KEY_DEF_ICMP_TYPE) {
                hinic3_ds_put_format(ds, "%s(%hu), ", HINIC3_UI_KEY_ICMP_TYPE_STR, u8_val);
    } else if (type == HINIC3_FLOW_KEY_DEF_ICMP_CODE) {
                hinic3_ds_put_format(ds, "%s(%hu), ", HINIC3_UI_KEY_ICMP_CODE_STR, u8_val);
    } else if (type == HINIC3_FLOW_KEY_DEF_ICMP6_TYPE) {
                hinic3_ds_put_format(ds, "%s(%hu), ", HINIC3_UI_KEY_ICMP6_TYPE_STR, u8_val);
    } else if (type == HINIC3_FLOW_KEY_DEF_ICMP6_CODE) {
                hinic3_ds_put_format(ds, "%s(%hu), ", HINIC3_UI_KEY_ICMP6_CODE_STR, u8_val);
    } else if (type == HINIC3_FLOW_KEY_DEF_TCP_SPORT) {
                hinic3_ds_put_format(ds, "%s(%hu), ", HINIC3_UI_KEY_SRC_PORT_STR, ntohs(u16_val));
    } else if (type == HINIC3_FLOW_KEY_DEF_UDP_SPORT) {
                hinic3_ds_put_format(ds, "%s(%hu), ", HINIC3_UI_KEY_SRC_PORT_STR, ntohs(u16_val));
    }
}

static void
hinic3_flexda_flow_key_format_output_dst_port(const hinic3_nlattr_itr nla, struct ds *ds, int type)
{
    uint16_t u16_val = hinic3_nlattr_get_itr_u16(nla);
    if (type == HINIC3_FLOW_KEY_DEF_ICMP_IDENT) {
        hinic3_ds_put_format(ds, "%s(%hu), ", HINIC3_UI_KEY_ICMP_ID_STR, ntohs(u16_val));
    } else if (type == HINIC3_FLOW_KEY_DEF_TCP_DPORT) {
        hinic3_ds_put_format(ds, "%s(%hu), ", HINIC3_UI_KEY_DST_PORT_STR, ntohs(u16_val));
    } else if (type == HINIC3_FLOW_KEY_DEF_UDP_DPORT) {
        hinic3_ds_put_format(ds, "%s(%hu), ", HINIC3_UI_KEY_DST_PORT_STR, ntohs(u16_val));
    }
}

static void
hinic3_flexda_flow_process_vni(const hinic3_nlattr_itr nla, struct ds *ds)
{
    uint32_t u32_val = hinic3_nlattr_get_itr_u32(nla);
    hinic3_ds_put_format(ds, "%s(%u), ", HINIC3_UI_KEY_VNI_STR, ntohl(u32_val << HINIC3_DFLEXDA_DUMP_VNI_PADDING));
}

static void hinic3_flexda_flow_process_dl_type(const hinic3_nlattr_itr nla, struct ds *ds)
{
    uint16_t u16_val = hinic3_nlattr_get_itr_u16(nla);
    hinic3_ds_put_format(ds, "%s(%04x), ", HINIC3_UI_KEY_DL_TYPE_STR, ntohs(u16_val));
}

static void
hinic3_flexda_flow_process_ip(const hinic3_nlattr_itr nla, int type, struct ds *ds)
{
    const char *ip_string;
    if (type == HINIC3_FLOW_KEY_SRC_IP || type == HINIC3_FLOW_KEY_DEF_IPV4_SIP) {
        ip_string = HINIC3_UI_KEY_SRC_IP_STR;
    } else {
        ip_string = HINIC3_UI_KEY_DST_IP_STR;
    }
    uint32_t u32_val;
    u32_val = hinic3_nlattr_get_itr_u32(nla);
    hinic3_ds_put_format(ds, "%s(" IP_FMT "), ", ip_string, IP_ARGS(u32_val));
}


static void
hinic3_flexda_flow_process_ipv6(const hinic3_nlattr_itr nla, int type, struct ds *ds)
{
    const char *ip_string;
    if (type == HINIC3_FLOW_KEY_SRC_IPV6 || type == HINIC3_FLOW_KEY_DEF_IPV6_SIP) {
        ip_string = HINIC3_UI_KEY_SRC_IP_STR;
    } else {
        ip_string = HINIC3_UI_KEY_DST_IP_STR;
    }
    const struct in6_addr *ipv6_addr = (const struct in6_addr *)hinic3_nlattr_get_itr_data(nla);
    hinic3_ds_put_format(ds, "%s(", ip_string);
    hinic3_ipv6_format_addr(ipv6_addr, ds);
    hinic3_ds_put_format(ds, "), ");
}

static void
hinic3_flexda_flow_process_vlan_vid(const hinic3_nlattr_itr nla, int type, struct ds *ds)
{
    const char *vid_string =
        (type == HINIC3_FLOW_KEY_INNER_VID) ? HINIC3_UI_KEY_INNER_VLAN_STR : HINIC3_UI_KEY_OUTER_VLAN_STR;
    uint16_t u16_val = hinic3_nlattr_get_itr_u16(nla);
    hinic3_ds_put_format(ds, "%s(%hu), ", vid_string, ntohs(u16_val));
}

static void
hinic3_flexda_flow_process_vport(const hinic3_nlattr_itr nla, struct ds *ds)
{
    if (hinic3_forward_mode_get() == OVS_KEY_EXTRACT_EXTEND_MODE_7TUPLE)
    {
        return;
    }
    uint16_t port_id = 0;
    uint16_t ifindex = hinic3_nlattr_get_itr_u16(nla);
    if (ifindex) {
    int ret = hinic3_get_port_id_by_ifindex(ntohs(ifindex), &port_id);
        if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "get port_id failed, ifindex(0x%x)", ifindex);
        hinic3_ds_put_format(ds, "%s(%s), ", HINIC3_UI_KEY_VPORT_STR, "error");
        return;
        }
        hinic3_ds_put_format(ds, "%s(%hu), %s(%hu), ", HINIC3_UI_KEY_VPORT_STR, port_id,
                    HINIC3_UI_KEY_IFINDEX_STR, ntohs(ifindex));
    }
}

static void
hinic3_flexda_flow_process_mac(const hinic3_nlattr_itr nla, int type, struct ds *ds)
{
    const char *mac_string = (type == HINIC3_FLOW_KEY_SRC_MAC ||
        type == HINIC3_FLOW_KEY_DEF_ETH_SMAC) ? HINIC3_UI_KEY_SRC_MAC_STR : HINIC3_UI_KEY_DST_MAC_STR;
    const uint8_t *mac = (const uint8_t *)hinic3_nlattr_get_itr_unspec(nla, ETH_ALEN);
    hinic3_ds_put_format(ds, "%s(" HINIC3_MAC_FMT "), ", mac_string, HINIC3_OUTPUT_MAC(mac));
}

static uint8_t
hinic3_flexda_flow_process_nw_protocol(const hinic3_nlattr_itr nla, struct ds *ds)
{
    uint8_t nw_protocol = hinic3_nlattr_get_itr_u8(nla);
    hinic3_ds_put_format(ds, "%s(%hhu), ", HINIC3_UI_KEY_NW_PROTO_STR, nw_protocol);
    return nw_protocol;
}


// 计算对齐后的字节长度
static uint16_t hinic3_cal_hydra_ket_member_align_len(uint16_t bit_width)
{
    return DIV_8_UNIT(bit_width + BYTE_ALIGN_MASK);
}

// 检查偏移量和宽度是否都是8字节对齐
static bool hinic3_hydra_key_member_byte_align_flag(uint16_t offset, uint16_t width)
{
    return (offset % BITS_PER_BYTE == 0) && (width % BITS_PER_BYTE == 0);
}

static void hinic3_flow_hydra_unit_format_output(const uint8_t* data, uint16_t size, struct ds *ds)
{
    uint64_t value = 0;
    for (size_t i = 0; i < size && i < sizeof(uint64_t); i++) {
        value = (value << UINT8_WIDTH) | data[i];
    }
    hinic3_ds_put_format(ds, "%u", value);
}

static void hinic3_flow_hydra_info_format_output(const uint8_t* dump_data,
    uint16_t size, uint16_t dump_format, struct ds *ds)
{
    switch (dump_format) {
        case HOVS_FLEXDA_DUMP_FORMAT_MAC:
        hinic3_ds_put_format(ds, HINIC3_MAC_FMT, HINIC3_OUTPUT_MAC(dump_data));
        break;
        case HOVS_FLEXDA_DUMP_FORMAT_IPV4:
        hinic3_ds_put_format(ds, IP_FMT, IP_ARGS(*(const uint32_t *)dump_data));
        break;
        case HOVS_FLEXDA_DUMP_FORMAT_IPV6:
        hinic3_ipv6_format_addr((const struct in6_addr *)dump_data, ds);
        break;
        case HOVS_FLEXDA_DUMP_FORMAT_UNIT:
        hinic3_flow_hydra_unit_format_output(dump_data, size, ds);
        break;
        case HOVS_FLEXDA_DUMP_FORMAT_HEXBYTE:
        hinic3_ds_put_format(ds, "0X");
        for (size_t i = 0; i < size; i++) {
            hinic3_ds_put_format(ds, "%02X", dump_data[i]);
        }
        break;
        default:
        break;
    }
}

static void hinic3_flow_process_hydra_struct_member_data(const uint8_t* input_data, uint8_t* output_data,
uint16_t bit_offset, uint16_t bit_width)
{
    size_t num_bytes = hinic3_cal_hydra_ket_member_align_len(bit_width);
    uint64_t total_bit_index = 0;
    uint64_t in_byte_index = 0;
    uint32_t in_bit_index = 0;
    uint8_t bit = 0;
    uint64_t output_bit_index = 0;
    uint64_t out_byte_index = 0;
    uint32_t out_bit_index = 0;

    for (uint32_t i = 0; i < bit_width; i++) {
        total_bit_index = bit_offset + i;
        in_byte_index = total_bit_index / BITS_PER_BYTE;
        in_bit_index = total_bit_index % BITS_PER_BYTE;
        bit = (input_data[in_byte_index] >> ((BITS_PER_BYTE - 1) - in_bit_index)) & 1;

        output_bit_index = (uint64_t)num_bytes * BITS_PER_BYTE - bit_width + i;
        out_byte_index = output_bit_index / BITS_PER_BYTE;
        out_bit_index = output_bit_index % BITS_PER_BYTE;

        if (bit != 0) {
            output_data[out_byte_index] |= (1 << ((BITS_PER_BYTE - 1) - out_bit_index));
        }
    }
}

static int hinic3_flow_process_hydra_struct_member(hinic3_hydra_type hydra_type,
const hovs_flexda_config_dump_member_info_t *member_info, const hinic3_nlattr_itr nla, struct ds *ds)
{
    uint16_t nla_type = hinic3_nlattr_get_itr_type(nla);
    const uint8_t* nla_data = (const void *)hinic3_nlattr_get_itr_data(nla);
    uint16_t nla_size = hinic3_nlattr_get_itr_size(nla);
    uint16_t member_width_size = DIV_8_UNIT(member_info->bit_width);
    uint16_t member_offset_size = DIV_8_UNIT(member_info->bit_offset);

    if ((member_info->bit_offset + member_info->bit_width) > MULTI_8_UNIT(nla_size)) {
        if (hydra_type == HIOVS_HYDRA_TYPE_KEY) {
            HINIC3_LOG(ERR, FLOW, "hydra key type (0X%02X) struct member (%s) width is invalid\n", nla_type,
            member_info->struct_member_name);
        } else {
            HINIC3_LOG(ERR, FLOW, "hydra action type (0X%02X) struct member (%s) width is invalid\n", nla_type,
            member_info->struct_member_name);
        }
        return -EINVAL;
    }
    
    hinic3_ds_put_format(ds, "%s=", member_info->struct_member_name);
    // 对齐场景直接输出
    if (hinic3_hydra_key_member_byte_align_flag(member_info->bit_offset, member_info->bit_width)) {
        if (hinic3_flexda_flow_dump_format_size_check(member_width_size, member_info->dump_format)) {
            hinic3_flow_hydra_info_format_output(nla_data + member_offset_size,
            member_width_size, member_info->dump_format, ds);
        } else {
            hinic3_flow_hydra_info_format_output(nla_data + member_offset_size,
            member_width_size, HOVS_FLEXDA_DUMP_FORMAT_HEXBYTE, ds);
            }
            return 0;
    }
    // 若非对齐场景需重新计算
    member_width_size = hinic3_cal_hydra_ket_member_align_len(member_info->bit_width);
    
    uint8_t* dump_data = (uint8_t*)hinic3_calloc(1, member_width_size, HINIC3_OVS_FLOW);
    if (dump_data == NULL) {
        hinic3_ds_put_format(ds, "dump_data_error");
        HINIC3_LOG(ERR, FLOW, "hinic3_flow_process_hydra_struct_member_data dump_data calloc failed.\n");
        return 0;
    }
    // 重新计算dump数据
    hinic3_flow_process_hydra_struct_member_data(nla_data, dump_data,
    member_info->bit_offset, member_info->bit_width);
    if (hinic3_flexda_flow_dump_format_size_check(member_width_size, member_info->dump_format)) {
        hinic3_flow_hydra_info_format_output(dump_data, member_width_size, member_info->dump_format, ds);
    } else {
        hinic3_flow_hydra_info_format_output(dump_data, member_width_size, HOVS_FLEXDA_DUMP_FORMAT_HEXBYTE, ds);
    }
    hinic3_free(dump_data);
    return 0;
}

static void hinic3_flow_process_hydra_info_format(hinic3_hydra_type hydra_type, const hinic3_nlattr_itr nla,
struct ds *ds)
{
    uint16_t type = hinic3_nlattr_get_itr_type(nla);
    uint16_t size = hinic3_nlattr_get_itr_size(nla);
    const uint8_t *data = (const void *)hinic3_nlattr_get_itr_data(nla);

    const hovs_flexda_config_dump_info_t* hydra_dump_info = hinic3_flexda_flow_get_hydra_config_dump_info(hydra_type,
    type);

    if (hydra_dump_info == NULL) {
        HINIC3_LOG(ERR, FLOW, "get hydra config dump info failed!");
        return;
    }
    // 非结构体格式化，主要输出ip，mac等信息
    if (hydra_dump_info->struct_member_num == 0) {
        if (hinic3_flexda_flow_dump_format_size_check(size, hydra_dump_info->dump_format)) {
            hinic3_flow_hydra_info_format_output(data, size, hydra_dump_info->dump_format, ds);
        } else {
            hinic3_flow_hydra_info_format_output(data, size, HOVS_FLEXDA_DUMP_FORMAT_HEXBYTE, ds);
        }
        return;
    }
    uint16_t member_index = 0;
    int ret = 0;
    // 结构体格式化，解析每个结构体信息
    for (member_index = 0; member_index < hydra_dump_info->struct_member_num; member_index++) {
        const hovs_flexda_config_dump_member_info_t *member_info = &hydra_dump_info->struct_members[member_index];
        ret = hinic3_flow_process_hydra_struct_member(hydra_type, member_info, nla, ds);
        if (member_index < hydra_dump_info->struct_member_num - 1 && ret == 0) {
            hinic3_ds_put_format(ds, ", ");
        }
    }
}

void hinic3_flow_process_hydra_info(hinic3_hydra_type hydra_type, const hinic3_nlattr_itr nla, struct ds *ds)
{
    uint16_t type = hinic3_nlattr_get_itr_type(nla);
    uint16_t size = hinic3_nlattr_get_itr_size(nla);

    const char *name = hinic3_flexda_flow_get_hydra_name(hydra_type, type);
    if (name == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flow_process_hydra_info : hinic3_flexda_flow_get_hydra_name error\n");
        return;
    }
    if (size == 0) {
        hinic3_ds_put_format(ds, "%s(", name);
        hinic3_ds_put_format(ds, "true");
    } else {
        // 增加自定义key和自定义action的展示
        hinic3_ds_put_format(ds, "%s(", name);
        hinic3_flow_process_hydra_info_format(hydra_type, nla, ds);
    }
    hinic3_ds_put_format(ds, "), ");
}

static int hinic3_flexda_flow_key_format_output_sub(const hinic3_nlattr_itr nla, struct ds *ds, int type, uint8_t *nw_protocol)
{
    switch (type)
    {
    case HINIC3_FLOW_KEY_DPDK_VXLAN:
        hinic3_flexda_flow_process_vni(nla, ds);
        break;
    case HINIC3_FLOW_KEY_DEF_ETH_TYPE:
        hinic3_flexda_flow_process_dl_type(nla, ds);
        break;
    case HINIC3_FLOW_KEY_DEF_IPV4_PROTOCOL:
    case HINIC3_FLOW_KEY_DEF_IPV6_PROTOCOL:
        *nw_protocol = hinic3_flexda_flow_process_nw_protocol(nla, ds);
        break;
    case HINIC3_FLOW_KEY_DEF_IPV4_SIP:
    case HINIC3_FLOW_KEY_DEF_IPV4_DIP:
        hinic3_flexda_flow_process_ip(nla, type, ds);
        break;
    case HINIC3_FLOW_KEY_DEF_IPV6_SIP:
    case HINIC3_FLOW_KEY_DEF_IPV6_DIP:
        hinic3_flexda_flow_process_ipv6(nla, type, ds);
        break;
    case HINIC3_FLOW_KEY_DEF_ICMP_TYPE:
    case HINIC3_FLOW_KEY_DEF_ICMP_CODE:
    case HINIC3_FLOW_KEY_DEF_ICMP6_TYPE:
    case HINIC3_FLOW_KEY_DEF_ICMP6_CODE:
    case HINIC3_FLOW_KEY_DEF_TCP_SPORT:
    case HINIC3_FLOW_KEY_DEF_UDP_SPORT:
        hinic3_flexda_flow_key_format_output_src_port(nla, ds, type);
        break;
    case HINIC3_FLOW_KEY_DEF_ICMP_IDENT:
    case HINIC3_FLOW_KEY_DEF_TCP_DPORT:
    case HINIC3_FLOW_KEY_DEF_UDP_DPORT:
        hinic3_flexda_flow_key_format_output_dst_port(nla, ds, type);
        break;
    case HINIC3_FLOW_KEY_DEF_VLAN_TCI:
        hinic3_flexda_flow_process_vlan_vid(nla, type, ds);
        break;
    case HINIC3_FLOW_KEY_IN_PORT:
    case HINIC3_FLOW_KEY_DPDK_PORT_ID:
        hinic3_flexda_flow_process_vport(nla, ds);
        break;
    case HINIC3_FLOW_KEY_DEF_ETH_SMAC:
    case HINIC3_FLOW_KEY_DEF_ETH_DMAC:
        hinic3_flexda_flow_process_mac(nla, type, ds);
        break;
    default:
        return -1;
    }
    return 0;
}

void hinic3_flexda_flow_key_format_output(const struct hinic3_nlattr *key, struct ds *ds)
{
    hinic3_nlattr_itr nla = NULL;
    uint8_t nw_protocol = 0;
    HINIC3_NLATTR_FOR_EACH(nla, key)
    {
        int type = hinic3_nlattr_get_itr_type(nla);
        int ret = hinic3_flexda_flow_key_format_output_sub(nla, ds, type, &nw_protocol);
        if (ret == 0) {
            continue;
        }
        if ((type >= HINIC3_HYDRA_TYPE_KEY_START && type <= HINIC3_HYDRA_TYPE_KEY_END) ||
            hinic3_flexda_flow_key_is_in_table(type)) {
            hinic3_flow_process_hydra_info(HIOVS_HYDRA_TYPE_KEY, nla, ds);
            continue;
        }
    }
}
