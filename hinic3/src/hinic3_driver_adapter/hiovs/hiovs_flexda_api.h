/*
 * 版权所有 (c) 华为技术有限公司 2022-2023
 * 功能描述: hiovs_flexda_api相关结构体头文件
 *  * 创建日期: 2025-11-17
 *  */

#ifndef HIOVS_FLEXDA_API_H
#define HIOVS_FLEXDA_API_H

#include <stdint.h>
#include <stddef.h>
#include "rte_flow.h"
#include "hinic3_agent.h"
#include "hinic3_flow_agent_enum.h"
#include "hiovs_api.h"

#ifdef __cplusplus
extern "C"
{
#endif
    extern struct hwoff_flow_ops_adapter g_hwoff_flexda_flow_ops_adapter;

    /**
     *  * @name  注册回调函数
     *  * @param {in}  cb   回调函数地址
     *  * @return  注册完的回调函数地址
     *  */
    hovs_flexda_flow_callback_t hovs_flexda_flow_callback_register(hovs_flexda_flow_callback_t cb);

    hovs_flexda_flow_callback_t hovs_flexda_flow_callback_register(hovs_flexda_flow_callback_t cb);

    /* flexda dpdk adapter */
    /**
     *  * @name 分配rte_flow_ops结构体内存
     *  * @return 分配的流表操作管理结构体指针
     *  * @note 调用方需确保后续使用hovs_flexda_rte_flow_ops_free释放内存
     *  */
    static inline struct rte_flow_ops *hovs_flexda_rte_flow_ops_allocate(void)
    {
        return g_hwoff_flexda_flow_ops_adapter.rte_flow_ops_allocate();
    }

    /**
     *  * @name 释放rte_flow_ops结构体内存
     *  * @param {in} ops 流表操作管理结构体指针
     *  * @note 传入的rte_flow_ops结构体指针所指向的内存由hovs_flexda_rte_flow_ops_allocate函数分配
     *  */
    static inline void hovs_flexda_rte_flow_ops_free(struct rte_flow_ops *flow_ops)
    {
        return g_hwoff_flexda_flow_ops_adapter.rte_flow_ops_free(flow_ops);
    }

    /**
     *  * @name 设置流表创建回调函数
     *  * @param {in} ops 流表操作管理结构体指针
     *  * @param {in} func rte_flow_create原型实现函数指针
     *  * @note 1、传入的rte_flow_ops结构体指针所指向的内存由hovs_flexda_rte_flow_ops_allocate函数分配
     *  * @note 2、回调函数需符合rte_flow_create原型，参见DPDK开源代码
     *  * @note 3、回调函数会经过适配器转换
     *  */
    static inline void hovs_flexda_rte_flow_ops_set_create(struct rte_flow_ops *flow_ops, void *arg)
    {
        return g_hwoff_flexda_flow_ops_adapter.set_create(flow_ops, arg);
    }

    /**
     *  * @name 设置流表销毁回调函数
     *  * @param {in} ops 流表操作管理结构体指针
     *  * @param {in} func rte_flow_destroy原型实现函数指针
     *  * @note 1、传入的rte_flow_ops结构体指针所指向的内存由hovs_flexda_rte_flow_ops_allocate函数分配
     *  * @note 2、回调函数需符合rte_flow_destroy原型，参见DPDK开源代码
     *  */
    static inline void hovs_flexda_rte_flow_ops_set_destroy(struct rte_flow_ops *flow_ops, void *arg)
    {
        return g_hwoff_flexda_flow_ops_adapter.set_destroy(flow_ops, arg);
    }

    /**
     *  * @name 设置流表清空回调函数
     *  * @param {in} ops 流表操作管理结构体指针
     *  * @param {in} func rte_flow_flush原型实现函数指针
     *  * @note 1、传入的rte_flow_ops结构体指针所指向的内存由hovs_flexda_rte_flow_ops_allocate函数分配
     *  * @note 2、回调函数需符合rte_flow_flush原型，参见DPDK开源代码
     *  */
    static inline void hovs_flexda_rte_flow_ops_set_flush(struct rte_flow_ops *flow_ops, void *arg)
    {
        return g_hwoff_flexda_flow_ops_adapter.set_flush(flow_ops, arg);
    }

    /**
     *  * @name 设置流表查询回调函数
     *  * @param {in} ops 流表操作管理结构体指针
     *  * @param {in} func rte_flow_query原型实现函数指针
     *  * @note 1、传入的rte_flow_ops结构体指针所指向的内存由hovs_flexda_rte_flow_ops_allocate函数分配
     *  * @note 2、回调函数需符合rte_flow_query原型，参见DPDK开源代码
     *  */
    static inline void hovs_flexda_rte_flow_ops_set_query(struct rte_flow_ops *flow_ops, void *arg)
    {
        return g_hwoff_flexda_flow_ops_adapter.set_query(flow_ops, arg);
    }

    /* flexda ovs adapter */
    /*
     * 函 数 名 : hovs_flexda_init_adapter
     * 功能描述 : 初始化适配层
     *  * 输入参数 : api,DPAK开放的功能；
     *  * 输出参数 ：customed：用户so是否加载成功
     *  * 返 回 值 : 成功：0 失败：其他
     *  */
    int hovs_flexda_init_adapter(hinic3_flexda_ovs_ops_t *ops, bool *adapted);

    /*
     * 函 数 名 : hovs_flexda_deinit_adapter
     * 功能描述 : 反初始化适配层
     *  * 返 回 值 : 成功：0 失败：其他
     *  */
    int hovs_flexda_deinit_adapter(void);

    /*
     * 函 数 名 : hovs_flexda_init_ctx
     * 功能描述 : 初始化自定义上下文
     *  * 输入参数 : ctx
     *  * 返 回 值 : 成功：0 失败：其他
     *  */
    int hovs_flexda_init_ctx(void **ctx);

    /*
     * 函 数 名 : hovs_flexda_deinit_ctx
     * 功能描述 : 反初始化自定义上下文
     *  * 输入参数 : ctx
     *  * 返 回 值 : 成功：0 失败：其他
     *  */
    int hovs_flexda_deinit_ctx(void **ctx);

    /*
     * 函 数 名 : hovs_flexda_update_ctx
     * 功能描述 : 更新自定义上下文信息
     *  * 输入参数 : ctx，自定义的上下文指针；info，与DPAK约定传递的信息；
     *  * 返 回 值 : 成功：0 失败：其他
     *  */
    int hovs_flexda_update_ctx(void *ctx, const struct hinic3_flexda_ovs_info_t *info);

    /*
     * 函 数 名 : hovs_flexda_extract_hdr
     * 功能描述 : 从报文中提取头信息
     *  * 输入参数 : ctx，自定义的上下文指针；info，与DPAK约定传递的信息；
     *  * 返 回 值 : 成功：0 失败：其他
     *  */
    int hovs_flexda_extract_hdr(void *ctx, const struct hinic3_flexda_ovs_info_t *info);

    /*
     * 函 数 名 : hovs_flexda_construct_key
     * 功能描述 : 构建下发硬件流表key
     *  * 输入参数 : ctx，自定义的上下文指针；info，与DPAK约定传递的信息；hw_key，key的信息；
     *  * 返 回 值 : 成功：0 失败：其他
     *  */
    int hovs_flexda_construct_key(void *ctx, const struct hinic3_flexda_ovs_info_t *info, struct rte_flow_item *hw_key);

    /*
     * 函 数 名 : hovs_flexda_construct_action
     * 功能描述 : 构建下发硬件流表action
     *  * 输入参数 : ctx，自定义的上下文指针；info，与DPAK约定传递的信息；hw_action，action的信息；
     *  * 返 回 值 : 成功：0 失败：其他
     *  */
    int hovs_flexda_construct_action(void *ctx, const struct hinic3_flexda_ovs_info_t *info,
                                     struct rte_flow_action *hw_action);

    /*
     * 函 数 名 : hovs_flexda_construct_attr
     * 功能描述 : 构建下发硬件流表的attr
     *  * 输入参数 : ctx，自定义的上下文指针；info，与DPAK约定传递的信息；attr，attr的信息；
     *  * 返 回 值 : 成功：0 失败：其他
     *  */
    int hovs_flexda_construct_attr(void *ctx, const struct hinic3_flexda_ovs_info_t *info, struct rte_flow_attr *attr);

    /*
     * 函 数 名 : hovs_flexda_check_tunnel
     * 功能描述 : 隧道协议检查
     *  * 输入参数 : devname，隧道名称
     *  * 返 回 值 : 成功：0 失败：其他
     *  */
    int hovs_flexda_check_tunnel(const char *netdev_type);

#ifdef __cplusplus
}
#endif
#endif