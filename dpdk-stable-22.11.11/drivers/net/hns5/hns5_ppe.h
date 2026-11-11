#ifndef _HNS5_PPE_H_
#define _HNS5_PPE_H_

#include <rte_ethdev.h>

#include "hidrv_ppe_pf_api.h"
#include "ndrv_ppe_pf_api.h"
#include "ndrv_ppe_vf_api.h"

#include "hns5_eth.h"

#define PPE_SUPPORT_FRAME_MAX (9600)    // incl. CRC
#define PPE_TNL_MAX_NUM (32)
#define HNS5_L3T_IPV6 1
#define HNS5_L3T_IPV4 2

#define HNS5_L4T_TCP  1
#define HNS5_L4T_UDP  2
#define HNS5_L4T_SCTP 3

extern enum hidrv_ppe_endian_type g_ppe_endian_mode;

int hns5_eth_ppe_init(struct rte_eth_dev *dev);
int hns5_eth_ppe_init_lsw(struct rte_eth_dev *dev);

/**
 * @brief Configure port isolation (all-open by default)
 */
int hns5_ppe_lsw_fltr_iso_set(struct rte_eth_dev *dev);

/**
 * @brief Configure unicast loopback enable
 * @param dev NIC device
 * @param enable 1-Enable loopback 0-Disable loopback
 */
int hns5_ppe_lsw_set_ucast_loop(struct rte_eth_dev *dev, unsigned int enable);
/**
 * @brief Configure fw_tnl; vqi=base_vqi+qos, vqi selects the destination queue
*/
int hns5_ppe_fwl_tnl_config(struct rte_eth_dev *dev, const struct rte_fw_tnl_conf *fw_tnl, uint32_t pool_id);

/**
 * @brief Configure CPU port VLAN Insert and ingress flow table
 * @param dev NIC device
 * @param vlan_insert_en VLAN Insert enable flag: 1-enable, 0-restore
 * @return int 0 on success, other on failure
 */
int hns5_ppe_vlan_insert_config(struct rte_eth_dev *dev, uint8_t vlan_insert_en);

/**
 * @brief Configure multicast mode (enable: receive multicast, disable: drop multicast)
 * @param dev NIC device
 * @param enable 1-enable multicast mode (add ingress flow matching DMAC I/G bit, mcast frames sent to CPU),
 *               0-disable multicast mode (delete the flow, mcast frames dropped by default path)
 * @return int 0 on success, other on failure
 */
int hns5_ppe_lsw_mcast_mode_config(struct rte_eth_dev *dev, uint8_t enable);

/**
 * @brief Refresh the PPE/MAM diagnostics exposed through xstats
 *
 * Reads the PPE "no buffer"/low-QoS drop counters and the per-vlp
 * "vpool pre-request buffer" enable bits. The second one is what decides how
 * many buffers the hardware holds outside the pool.
 */
void hns5_ppe_diag_refresh(struct rte_eth_dev *dev);

/**
 * @brief Dump the MAM sub-module DFX registers to the log
 *
 * Diagnostic only: the BSP prints text into a caller supplied buffer, so this
 * cannot be exposed as numeric xstats.
 */
void hns5_ppe_log_mam_dfx(struct rte_eth_dev *dev, const char *tag);

#endif /* _HNS5_PPE_H_ */
