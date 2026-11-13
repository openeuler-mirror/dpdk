#ifndef _HNS5_POE_H_
#define _HNS5_POE_H_

#include <stdint.h>

#include <rte_ethdev.h>

#include "ndrv_poe_pf_api.h"
#include "ndrv_poe_vf_api.h"

#include "hns5_eth.h"

#define HNS5_ETH_GROUP_MAX         31   /* CPU group space */
#define HNS5_POE_ORDER_TMOUT_TIME  3000
#define HNS5_POE_CPU_GROUP_MAX_NUM 24
#define HNS5_POE_BP_GROUP_MAX_NUM  16

#define HNS5_POE_CPU_GRP_QUE_NUM   8
#define HNS5_POE_TH_TPMLTE_ID      0

/*
 * POE group plan (platform spec) -- single source of truth, 32 physical
 * groups per side:
 *   - CPU side: groups 0..31, 8 queues each. A virtual group bound to a
 *     logical CPU must be numbered from 0. The packet path (share_group)
 *     lives here, but group 1 belongs to the kernel's SSH path and groups
 *     24..31 must not be used, so share_group is valid in 0..23 minus
 *     {1, 12}. Group 12 is the SEC completion group reserved by
 *     crypto/hns5.
 *   - ACC side: groups 32..63, 4 queues each. A virtual group bound to an
 *     accelerator must be numbered from 32, and groups 50..63 must not be
 *     used, so the SEC engine may only use 32..49 -- enforced against the
 *     BSP-reported window in crypto/hns5 (hns5_sec_platform.c).
 * Both packet-path entry points (devargs parser and POE init) go through
 * hns5_poe_check_cpu_group(), so the two checks cannot drift apart.
 */
#define HNS5_POE_GRP_KERNEL_SSH          1U
#define HNS5_POE_SEC_RESERVED_GRP        12U
#define HNS5_POE_CPU_GRP_FORBIDDEN_FIRST 24U
#define HNS5_POE_CPU_GRP_FORBIDDEN_LAST  31U

int hns5_eth_poe_init(struct rte_eth_dev *dev);

/**
 * @brief Reject a POE group the packet path must not bind, logging the reason
 *        so every entry point applies the same policy
 * @return 0 if usable, -EINVAL otherwise
 */
int hns5_poe_check_cpu_group(struct rte_eth_dev *dev, uint32_t group);

/**
 * @brief Init poe HW groups and their queues, and bind vcpu during RX queue setup
 * @attention Not concurrent-safe but reentrant; no duplicate init internally
*/
int hns5_eth_rx_queue_setup_poe(struct rte_eth_dev *dev, uint32_t group, uint16_t depth);

/**
 * @brief Read and discard task messages queued for (vcpu, group)
 *
 * Only consumes messages; it deliberately does not touch the group state, since
 * the SEC engine of crypto/hns5 has its submit/completion groups bound to a
 * VCPU that the packet path may share. Used to get rid of messages left in the
 * queue by a previous process before this one enables its RX queues: their
 * buffers belong to the previous run's BMU pool, so they can only be dropped.
 *
 * @return number of discarded messages (>= 0), or negative errno
 */
int hns5_poe_drain_msgs(struct rte_eth_dev *dev, uint32_t vcpu, uint32_t group,
    unsigned int *last_rc);

#endif /* _HNS5_POE_H_ */
