/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Huawei Technologies Co., Ltd
 */
#include "hinic3_drv.h"
#include "hinic3_log.h"
#include "hinic3_iface_flow.h"
#include "hinic3_flexda_flow_public.h"
struct hinic3_flexda_table_map {
    bool is_used;
    uint32_t table_id;
    uint16_t table_type;
    char table_name[HOVS_FLEXDA_FLOWTABLE_NAME_MAX_LENGTH];
    uint32_t flow_num;
};

struct hinic3_flexda_key_map {
    bool is_used;
    uint16_t key_type;
    char key_name[HOVS_FLEXDA_FIELD_TYPE_NAME_MAX_LENGTH];
};

struct hinic3_flexda_action_map {
    bool is_used;
    uint16_t action_type;
    char action_name[HOVS_FLEXDA_ACTION_TYPE_NAME_MAX_LENGTH];
};

static hovs_flexda_config_dump_info_t g_flexda_flow_action_dump_info_arr[HINIC3_HYDRA_TYPE_ACTION_MAX];
static hovs_flexda_config_dump_info_t g_flexda_flow_key_dump_info_arr[HINIC3_HYDRA_TYPE_KEY_MAX];
static struct hinic3_flexda_table_map g_flexda_flow_table_arr[HINIC3_HYDRA_TYPE_TABLE_MAX];
static struct hinic3_flexda_action_map g_flexda_flow_action_arr[HINIC3_HYDRA_TYPE_ACTION_MAX];
static struct hinic3_flexda_key_map g_flexda_flow_key_arr[HINIC3_HYDRA_TYPE_KEY_MAX];
static struct hinic3_flexda_config_info_t g_flexda_flow_config = {NULL, 0, 0, 0};

static int hinic3_flexda_flow_parse_struct_members_dump_format(uint16_t member_num,
    hovs_flexda_config_dump_member_info_t *src_member_dump_info,
    hovs_flexda_config_dump_member_info_t *dst_member_dump_info)
{
    uint16_t member_index = 0;
    int ret = 0;

    for (member_index = 0; member_index < member_num; member_index++) {
        if (hinic3_flexda_flow_check_dump_format_type_valid(src_member_dump_info[member_index].dump_format)) {
            HINIC3_LOG(ERR, FLOW,
            "hinic3_flexda_flow_parse_struct_members_dump_format : member dump format is invalid (%d)\n",
            src_member_dump_info[member_index].dump_format);
            return -EINVAL;
        }
        dst_member_dump_info[member_index].dump_format = src_member_dump_info[member_index].dump_format;
        dst_member_dump_info[member_index].bit_offset = src_member_dump_info[member_index].bit_offset;
        dst_member_dump_info[member_index].bit_width = src_member_dump_info[member_index].bit_width;
        strcpy(dst_member_dump_info[member_index].struct_member_name, src_member_dump_info[member_index].struct_member_name);
    }
    return ret;
}

static int hinic3_flexda_flow_parse_key_dump_info(uint16_t key_type, hovs_flexda_config_dump_info_t *dump_info)
{
    int ret = 0;
    hovs_flexda_config_dump_member_info_t *members = NULL;

    if (hinic3_flexda_flow_check_dump_format_type_valid(dump_info->dump_format)) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_key_dump_info key_dump_format is invalid (%d)\n",
        dump_info->dump_format);
        return -EINVAL;
    }
    g_flexda_flow_key_dump_info_arr[key_type].dump_format = dump_info->dump_format;
    g_flexda_flow_key_dump_info_arr[key_type].struct_member_num = dump_info->struct_member_num;
    
    if (dump_info->struct_member_num == 0) {
        g_flexda_flow_key_dump_info_arr[key_type].struct_members = NULL;
        return 0;
    }

    if (dump_info->struct_members == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_key_dump_info : dump_info member is null\n");
        return -EINVAL;
    }

    members = hinic3_calloc(dump_info->struct_member_num, sizeof(hovs_flexda_config_dump_member_info_t), HIOVS_MEM);
    if (members == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_key_dump_info alloc memory error\n");
        return -ENOMEM;
    }

    ret = hinic3_flexda_flow_parse_struct_members_dump_format(dump_info->struct_member_num,
    dump_info->struct_members, members);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_key_dump_info parse struct members error\n");
        hinic3_free(members);
        g_flexda_flow_key_dump_info_arr[key_type].struct_members = NULL;
        return ret;
    }
    g_flexda_flow_key_dump_info_arr[key_type].struct_members = members;
    return ret;
}

static int hinic3_flexda_flow_parse_table_info(hovs_flexda_config_info_flowtable_config_t *table_info)
{
    if (table_info == NULL)
        return -EINVAL;
    
    uint32_t table_index = 0;
    if (hinic3_flexda_flow_check_table_id_valid(table_info->table_id) != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_table_info: table id is invalid(%0x)", table_info->table_id);
        return -EINVAL;
    }

    if (table_info->max_entry_num == 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_table_info: max_entry_num is 0 (table id:%0x)", table_info->table_id);
        return -EINVAL;
    }

    table_index = hinic3_flexda_flow_get_table_index(table_info->table_id);
    if (g_flexda_flow_table_arr[table_index].is_used == true) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_table_info: table id is duplicate(%0x)", table_info->table_id);
        return -EPERM;
    }
    g_flexda_flow_table_arr[table_index].table_id = table_info->table_id;
    g_flexda_flow_table_arr[table_index].table_type = table_info->table_type;
    strcpy(g_flexda_flow_table_arr[table_index].table_name, table_info->table_name);
    if (g_flexda_flow_table_arr[table_index].table_name == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_table_info: strcpy failed");
        return -EPERM;
    }
    g_flexda_flow_table_arr[table_index].flow_num = table_info->max_entry_num;
    g_flexda_flow_table_arr[table_index].is_used = true;
    g_flexda_flow_config.total_flow_num += table_info->max_entry_num;
    return 0;
}

static int hinic3_flexda_flow_parse_key_info(hovs_flexda_config_info_field_list_t *key_info)
{
    int ret = 0;
    uint16_t key_index = 0;
    for (int i = 0; i < key_info->field_num; i++) {
        if (key_info->field_array[i] == NULL) {
            HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_key_info: field_array %d is invalid", i);
            return -EINVAL;
        }
        if (hinic3_flexda_flow_check_key_type_valid(key_info->field_array[i]->field_type) != 0) {
            HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_key_info: field_type is invalid(%d)", key_info->field_array[i]->field_type);
            return -EINVAL;
        }
        
        key_index = key_info->field_array[i]->field_type;
        if (g_flexda_flow_key_arr[key_index].is_used == true) {
            continue;
        }

        g_flexda_flow_key_arr[key_index].key_type = key_info->field_array[i]->field_type;
        strcpy(g_flexda_flow_key_arr[key_index].key_name, key_info->field_array[i]->field_type_name);
        if (g_flexda_flow_key_arr[key_index].key_name == NULL) {
            HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_key_info: strcpy failed");
            return -EPERM;
        }
        ret = hinic3_flexda_flow_parse_key_dump_info(g_flexda_flow_key_arr[key_index].key_type,
            &(key_info->field_array[i]->field_dump_info));
        if (ret != 0) {
            HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_key_dump_info failed\n");
                        return -EPERM;
        }
        g_flexda_flow_key_arr[key_index].is_used = true;
        g_flexda_flow_config.key_num++;
    }
    return 0;
}
static int hinic3_flexda_flow_parse_action_dump_info(uint16_t action_type, hovs_flexda_config_dump_info_t *dump_info)
{
    int ret = 0;
    hovs_flexda_config_dump_member_info_t *members = NULL;

    if (hinic3_flexda_flow_check_dump_format_type_valid(dump_info->dump_format)) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_action_dump_info action_dump_format is invalid (%d)\n",
        dump_info->dump_format);
        return -EINVAL;
    }
    g_flexda_flow_action_dump_info_arr[action_type].dump_format = dump_info->dump_format;
    g_flexda_flow_action_dump_info_arr[action_type].struct_member_num = dump_info->struct_member_num;
    
    if (dump_info->struct_member_num == 0) {
        g_flexda_flow_action_dump_info_arr[action_type].struct_members = NULL;
        return 0;
    }

    if (dump_info->struct_members == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_action_dump_info : dump_info member is null\n");
        return -EINVAL;
    }

    members = hinic3_calloc(dump_info->struct_member_num, sizeof(hovs_flexda_config_dump_member_info_t),
    HIOVS_MEM);
    if (members == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_action_dump_info alloc memory error\n");
        return -ENOMEM;
    }

    ret = hinic3_flexda_flow_parse_struct_members_dump_format(dump_info->struct_member_num,
    dump_info->struct_members, members);
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_action_dump_info parse struct members error\n");
        hinic3_free(members);
        g_flexda_flow_action_dump_info_arr[action_type].struct_members = NULL;
        return ret;
    }
    g_flexda_flow_action_dump_info_arr[action_type].struct_members = members;
    return ret;
}

static int hinic3_flexda_flow_parse_action_info(hovs_flexda_config_info_action_list_t *action_info)
{
    int ret = 0;
    uint16_t action_index = 0;
    for (int i = 0; i < action_info->action_num; i++) {
        if (hinic3_flexda_flow_check_action_type_valid(action_info->action_array[i]->action_type) != 0) {
            HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_action_info: action_type is invalid(%d)", action_info->action_array[i]->action_type);
            return -EINVAL;
        }
        
        action_index = action_info->action_array[i]->action_type;
        if (g_flexda_flow_action_arr[action_index].is_used == true) {
            continue;
        }

        g_flexda_flow_action_arr[action_index].action_type = action_info->action_array[i]->action_type;
        strcpy(g_flexda_flow_action_arr[action_index].action_name, action_info->action_array[i]->action_type_name);
        if (g_flexda_flow_action_arr[action_index].action_name == NULL) {
            HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_action_info: strcpy failed");
            return -EPERM;
        }
        ret = hinic3_flexda_flow_parse_action_dump_info(g_flexda_flow_action_arr[action_index].action_type,
            &(action_info->action_array[i]->action_dump_info));
        if (ret != 0) {
            HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_action_dump_info failed.\n");
            return -EPERM;
        }
        g_flexda_flow_action_arr[action_index].is_used = true;
        g_flexda_flow_config.action_num++;
    }
    return 0;
}

static int hinic3_flexda_flow_init_array_info(void)
{

    for (int i = 0; i < HINIC3_HYDRA_TYPE_TABLE_MAX; i++) {
        g_flexda_flow_table_arr[i].is_used = false;
        g_flexda_flow_table_arr[i].table_type = 0;
        g_flexda_flow_table_arr[i].table_id = 0;
        g_flexda_flow_table_arr[i].flow_num = 0;
        (void)memset(g_flexda_flow_table_arr[i].table_name, 0, HOVS_FLEXDA_FLOWTABLE_NAME_MAX_LENGTH);
    }

    for (int i = 0; i < HINIC3_HYDRA_TYPE_KEY_MAX; i++) {
        g_flexda_flow_key_arr[i].is_used = false;
        g_flexda_flow_key_arr[i].key_type = 0;
        (void)memset(g_flexda_flow_key_arr[i].key_name, 0, HOVS_FLEXDA_FIELD_TYPE_NAME_MAX_LENGTH);
        g_flexda_flow_key_dump_info_arr[i].dump_format = 0;
        g_flexda_flow_key_dump_info_arr[i].struct_member_num = 0;
        g_flexda_flow_key_dump_info_arr[i].struct_members = NULL;
    }

    for (int i = 0; i < HINIC3_HYDRA_TYPE_ACTION_MAX; i++) {
        g_flexda_flow_action_arr[i].is_used = false;
        g_flexda_flow_action_arr[i].action_type = 0;
        (void)memset(g_flexda_flow_action_arr[i].action_name, 0, HOVS_FLEXDA_ACTION_TYPE_NAME_MAX_LENGTH);
        g_flexda_flow_action_dump_info_arr[i].dump_format = 0;
        g_flexda_flow_action_dump_info_arr[i].struct_member_num = 0;
        g_flexda_flow_action_dump_info_arr[i].struct_members = NULL;
    }
    return 0;
}

int hinic3_flexda_flow_parse_config_info(hovs_flexda_config_info_t *hovs_flexda_config)
{

    int ret = 0;
    hovs_flexda_config_flowtable_info_t *table_info = NULL;

    if (hovs_flexda_config == NULL) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_config_info: hovs_flexda_config is NULL ");
        return -EINVAL;
    }

    if (hovs_flexda_config->table_num < 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_config_info: table_num is invalid ");
        return -EINVAL;
    }

    ret = hinic3_flexda_flow_init_array_info();
    if (ret != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_config_info: hinic3_flexda_flow_init_array_info failed ");
        return -EINVAL;
    }
    for (int i = 0; i < hovs_flexda_config->table_num; i++) {
        table_info = hovs_flexda_config->table_array[i];
        if (table_info == NULL) {
            HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_config_info: table_info is NULL ");
            return -EINVAL;
        }

        ret = hinic3_flexda_flow_parse_table_info(&table_info->table_info);
        if (ret != 0) {
            HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_config_info: hinic3_flexda_flow_parse_table_info failed ");
            return ret;
        }

        ret = hinic3_flexda_flow_parse_key_info(&table_info->field_list);
        if (ret != 0) {
            HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_config_info: hinic3_flexda_flow_parse_key_info failed ");
            return ret;
        }

        ret = hinic3_flexda_flow_parse_action_info(&table_info->action_list);
        if (ret != 0) {
            HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_parse_config_info: hinic3_flexda_flow_parse_action_info failed ");
            return ret;
        }
    }
    g_flexda_flow_config.flow_config = hovs_flexda_config;
    return ret;
}

bool hinic3_flexda_flow_action_is_in_table(uint32_t action_type)
{
    if (hinic3_flexda_flow_check_action_type_valid(action_type) != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_action_is_in_table action_type is invalid (%d)", action_type);
                return false;
    }
        
    if (g_flexda_flow_action_arr[action_type].is_used == false) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_action_is_in_table action_type is not used (%d)", action_type);
                return false;
    }
    
    return true;
}


static const char *hinic3_flexda_flow_get_hydra_table_name(uint32_t table_id)
{
    uint32_t index = 0;
    if (hinic3_flexda_flow_check_table_id_valid(table_id) != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_get_hydra_table_name: table id is invalid (%d)!", table_id);
        return NULL;
    }

    index = hinic3_flexda_flow_get_table_index(table_id);
    if (g_flexda_flow_table_arr[index].is_used == false) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_get_hydra_table_name: table id is not update (%d)!", table_id);
        return NULL;
    }

    return (const char *)g_flexda_flow_table_arr[index].table_name;
}

static const char *hinic3_flexda_flow_get_hydra_key_name(uint16_t key_type)
{
    uint32_t index = 0;
    if (hinic3_flexda_flow_check_key_type_valid(key_type) != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_get_hydra_key_name: key_type is invalid (%d)!", key_type);
        return NULL;
    }

    index = key_type;
    if (g_flexda_flow_key_arr[index].is_used == false) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_get_hydra_key_name: key_type is not update (%d)!", key_type);
        return NULL;
    }

    return (const char *)g_flexda_flow_key_arr[index].key_name;
}

static const char *hinic3_flexda_flow_get_hydra_action_name(uint16_t action_type)
{
    uint32_t index = 0;
    if (hinic3_flexda_flow_check_action_type_valid(action_type) != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_get_hydra_action_name: action_type is invalid (%d)!", action_type);
        return NULL;
    }

    index = action_type;
    if (g_flexda_flow_action_arr[index].is_used == false) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_get_hydra_action_name: action_type is not update (%d)!", action_type);
        return NULL;
    }

    return (const char *)g_flexda_flow_action_arr[index].action_name;
}

const char *hinic3_flexda_flow_get_hydra_name(hinic3_hydra_type hydra_type, uint32_t value)
{
    const char *hydra_name = NULL;
    switch (hydra_type) {
        case HIOVS_HYDRA_TYPE_TABLE:
            hydra_name = hinic3_flexda_flow_get_hydra_table_name(value);
            break;
        case HIOVS_HYDRA_TYPE_KEY:
            hydra_name = hinic3_flexda_flow_get_hydra_key_name((uint16_t)value);
            break;
        case HIOVS_HYDRA_TYPE_ACTION:
            hydra_name = hinic3_flexda_flow_get_hydra_action_name((uint16_t)value);
            break;
        default:
            HINIC3_LOG(ERR, FLOW, "there is no hydra_type (%d)!", hydra_type);
            break;
    }
    return hydra_name;
}

bool hinic3_flexda_flow_is_in_main_table(uint32_t table_id)
{
    uint32_t index = 0;
    if (hinic3_flexda_flow_check_table_id_valid(table_id) != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_is_in_main_table: table id is invalid (%d)!", table_id);
        return false;
    }

    index = hinic3_flexda_flow_get_table_index(table_id);
    if (g_flexda_flow_table_arr[index].is_used == false) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_is_in_main_table: table id is not update (%d)!", table_id);
        return false;
    }

    if(g_flexda_flow_table_arr[index].table_type == HOVS_FLEXDA_FLOWTABLE_TYPE_MAIN_TABLE) {
        return true;
    }

    return false;
}

int hinic3_flexda_flow_get_table_flow_num(uint32_t table_id, uint32_t *table_flow_num)
{
    uint32_t index = 0;
    if (hinic3_flexda_flow_check_table_id_valid(table_id) != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_get_table_flow_num table_id is invalid (%d)", table_id);
        return -1;
    }

    index = hinic3_flexda_flow_get_table_index(table_id);
    if (g_flexda_flow_table_arr[index].is_used == false) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_get_table_flow_num table_id is invalid (%d)", table_id);
        return -1;
    }

    *table_flow_num = g_flexda_flow_table_arr[index].flow_num;
    
    return 0;
}

uint32_t hinic3_flexda_flow_get_total_flow_num(void)
{
    return g_flexda_flow_config.total_flow_num;
}
int hinic3_flexda_flow_get_table_num(void)
{
    return (int)g_flexda_flow_config.flow_config->table_num;
}
const struct hinic3_flexda_config_info_t* hinic3_flexda_get_flow_config(void)
{
    return (const struct hinic3_flexda_config_info_t *)&g_flexda_flow_config;
}

void hinic3_flexda_free_flow_config(void)
{
    (void)hinic3_flexda_free_flow_cfg_info(g_flexda_flow_config.flow_config);
    hinic3_free(g_flexda_flow_config.flow_config);
    g_flexda_flow_config.flow_config = NULL;
    g_flexda_flow_config.total_flow_num = 0;
    g_flexda_flow_config.key_num = 0;
    g_flexda_flow_config.action_num = 0;
}

void hinic3_flexda_free_flow_action_dump_info(void)
{
    for (int i = 0; i < HINIC3_HYDRA_TYPE_ACTION_MAX; i++) {
        if (g_flexda_flow_action_dump_info_arr[i].struct_members != NULL) {
            hinic3_free(g_flexda_flow_action_dump_info_arr[i].struct_members);
            g_flexda_flow_action_dump_info_arr[i].struct_members = NULL;
        }
    }
}

static const hovs_flexda_config_dump_info_t* hinic3_flexda_flow_get_hydra_config_key_dump_info(uint32_t key_type)
{
    return &g_flexda_flow_key_dump_info_arr[key_type];
}
 
static const hovs_flexda_config_dump_info_t* hinic3_flexda_flow_get_hydra_config_action_dump_info(uint32_t action_type)
{
    return &g_flexda_flow_action_dump_info_arr[action_type];
}

const hovs_flexda_config_dump_info_t* hinic3_flexda_flow_get_hydra_config_dump_info(hinic3_hydra_type hydra_type,
    uint32_t value)
{
    const hovs_flexda_config_dump_info_t* hydra_dump_info = NULL;
    switch (hydra_type) {
        case HIOVS_HYDRA_TYPE_KEY:
            hydra_dump_info = hinic3_flexda_flow_get_hydra_config_key_dump_info(value);
            break;
        case HIOVS_HYDRA_TYPE_ACTION:
            hydra_dump_info = hinic3_flexda_flow_get_hydra_config_action_dump_info(value);
            break;
        default:
            HINIC3_LOG(ERR, FLOW, "there is no struct member hydra type %d", hydra_type);
            break;
    }
    return hydra_dump_info;
}

bool hinic3_flexda_flow_dump_format_size_check(uint16_t size, uint16_t dump_format)
{
    switch (dump_format) {
        case HOVS_FLEXDA_DUMP_FORMAT_MAC:
            return size == ETH_ALEN;
        case HOVS_FLEXDA_DUMP_FORMAT_IPV4:
            return size == HINIC3_IPV4_ADDR_LEN;
        case HOVS_FLEXDA_DUMP_FORMAT_IPV6:
            return size == HINIC3_IPV6_ADDR_LEN;
        case HOVS_FLEXDA_DUMP_FORMAT_UNIT:
            return size <= sizeof(uint32_t) || size == sizeof(uint64_t);
        case HOVS_FLEXDA_DUMP_FORMAT_HEXBYTE:
        default:
            return true;
    }
    return true;
}

bool hinic3_flexda_flow_key_is_in_table(uint32_t key_type)
{
    if (hinic3_flexda_flow_check_key_type_valid(key_type) != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_key_is_in_table key_type is invalid (%d)", key_type);
        return false;
    }

    if (g_flexda_flow_key_arr[key_type].is_used == false) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_key_is_in_table key_type is not used (%d)", key_type);
        return false;
    }

    return true;
}

int hinic3_flexda_flow_get_table_type(uint32_t table_id)
{
    uint32_t index = 0;
    if (hinic3_flexda_flow_check_table_id_valid(table_id) != 0) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_get_table_type table_id is invalid (%d)", table_id);
        return -1;
    }

    index = hinic3_flexda_flow_get_table_index(table_id);

    if (g_flexda_flow_table_arr[index].is_used == false) {
        HINIC3_LOG(ERR, FLOW, "hinic3_flexda_flow_get_table_type table_id is not update (%d)", table_id);
        return -1;
    }

    return g_flexda_flow_table_arr[index].table_type;
}

bool hinic3_is_flexda_fuzzy_flow_table(uint32_t table_id)
{
    if (!hinic3_check_fuzzy_flow_flexda_switch()) {
        return false;
    }
    int table_type = hinic3_flexda_flow_get_table_type(table_id);
    if (table_type == HOVS_FLEXDA_FLOWTABLE_TYPE_PRE_FUZZY_TABLE || table_type == HOVS_FLEXDA_FLOWTABLE_TYPE_POST_FUZZY_TABLE) {
        return true;
    }
    return false;
}