/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Huawei Technologies Co., Ltd
 */

#ifndef HINIC3_FILE_USAGE_H
#define HINIC3_FILE_USAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "hinic3_ds.h"

const char *hinic3_get_default_directory(void);
int hinic3_check_output_file_directory(const char *filename, char *absolute_path, unsigned int path_len, struct ds *ds);
uint64_t hinic3_get_default_file_directory_size(bool *is_obtained);
uint64_t hinic3_get_partition_free_space_size(void);
int hinic3_check_file_realpath(const char *absolute_path, char *resolve_path, struct ds *ds);
int hinic3_agent_chown_output_file_path(const char *resolve_path);
int hinic3_agent_chmod_output_file_path(const char *resolve_path);
#endif
