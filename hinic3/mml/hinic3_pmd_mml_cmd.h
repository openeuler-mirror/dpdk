/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 */

#ifndef _HINIC3_PMD_MML_CMD
#define _HINIC3_PMD_MML_CMD

#include <stdint.h>

#define COMMAND_HELP_POSTION(argc)    ((argc) == 1 || (argc) == 2)
#define COMMAND_VERSION_POSTION(argc) ((argc) == 2)
#define SUB_COMMAND_OFFSET	      2

#define COMMAND_MAX_MAJORS	 128
#define COMMAND_MAX_OPTIONS	 64
#define PARAM_MAX_STRING	 128
#define COMMAND_MAX_STRING	 512
#define COMMANDER_ERR_MAX_STRING 128

#define MAX_NAME_LEN	 32
#define MAX_DES_LEN	 128
#define MAX_SHOW_STR_LEN 2048

struct tag_major_cmd_t;
struct tag_cmd_adapter_t;

typedef int (*command_record_t)(struct tag_major_cmd_t *major, char *param);
typedef void (*command_executeute_t)(struct tag_major_cmd_t *major);

typedef struct {
	const char *little;
	const char *large;
	unsigned int have_param;
	command_record_t record;
} cmd_option_t;

typedef struct tag_major_cmd_t {
	struct tag_cmd_adapter_t *adapter;
	char name[MAX_NAME_LEN];
	int option_count;
	cmd_option_t options[COMMAND_MAX_OPTIONS];
	uint32_t options_repeat_flag[COMMAND_MAX_OPTIONS];

	command_executeute_t execute;

	int err_no;
	char err_str[COMMANDER_ERR_MAX_STRING];
	char show_str[MAX_SHOW_STR_LEN];
	int show_len;
	char description[MAX_DES_LEN];
	void *cmd_st;
} major_cmd_t;

typedef struct tag_cmd_adapter_t {
	const char *name;
	const char *version;
	major_cmd_t *p_major_cmd[COMMAND_MAX_MAJORS];
	int major_cmds;
	char show_str[MAX_SHOW_STR_LEN];
	int show_len;
	char *cmd_buf;
} cmd_adapter_t;

void major_command_option(major_cmd_t *major_cmd, const char *little,
			  const char *large, uint32_t have_param,
			  command_record_t record);
void major_command_register(cmd_adapter_t *adapter,
			    major_cmd_t *major_cmd);
void command_parse(cmd_adapter_t *adapter, int argc, char **argv, void *buf_out, uint32_t *out_len);

void tool_target_init(int *bus_num, char *dev_name, int len);
int cmd_show_q_init(cmd_adapter_t *adapter);
int cmd_show_xstats_init(cmd_adapter_t *adapter);
int cmd_show_dump_init(cmd_adapter_t *adapter);

#endif /* HINIC3_PMD_MML_CMD */
