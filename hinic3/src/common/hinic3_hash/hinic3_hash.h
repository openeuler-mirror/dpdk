/* Copyright(c) 2023 Huawei Technologies Co., Ltd
 * 
 * This file contains code segments derived from Nicira, Inc.
 * Original copyright notice:
 * 
 * Copyright (c) 2008, 2009, 2010, 2012, 2013, 2014, 2016 Nicira, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _HINIC3_HASH_H_
#define _HINIC3_HASH_H_

#include <stddef.h>
#include <stdint.h>
#include <arpa/inet.h>

uint32_t hinic3_hash_add(uint32_t hash, uint32_t data);
uint32_t hinic3_hash_add64(uint32_t hash, uint64_t data);
uint32_t hinic3_hash_bytes(const void *point, size_t n, uint32_t basis);
uint32_t hinic3_hash_string(const char *s, uint32_t basis);

uint32_t hinic3_hash_2words(uint32_t x, uint32_t y);
uint32_t hinic3_hash_int(uint32_t x, uint32_t basis);
uint32_t hinic3_hash_uint64_basis(const uint64_t x, const uint32_t basis);
uint32_t hinic3_hash_uint64(const uint64_t x);

#endif
