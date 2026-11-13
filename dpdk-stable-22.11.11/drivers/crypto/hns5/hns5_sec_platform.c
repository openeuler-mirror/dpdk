/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Huawei Technologies Co., Ltd
 *
 * SEC PF/VF + POE setup, adapted from the vendor reference sec_platform.c.
 * Brings up POE submit/completion groups, binds the reserved vCPU1 to the
 * completion group, sets the SEC PF/VF into POE mode, and verifies the POE
 * routing. Ownership markers in the common BMU state keep DMA memory from being
 * reused after an uncertain teardown.
 */

#include <dlfcn.h>

#include "hidrv_urm_common.h"

#include "hns5_crypto_pmd_private.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define POE_DEFAULT_BP_THRESHOLD 0x3fffffU
#define POE_GROUP_LIMIT 64U
/*
 * ACC-side part of the POE group plan (see drivers/net/hns5/hns5_poe.h):
 * accelerator-bound virtual groups start at 32 and groups 50..63 must not
 * be used, so 32..49 is all the SEC engine may touch.
 */
#define POE_ACC_GRP_FIRST 32U
#define POE_ACC_GRP_LAST  50U /* exclusive: usable ACC window is [32, 50) */
#define POE_MASK_WORD_BITS 32U
#define POE_MASK_WORDS 2U
#define POE_VCPU_LIMIT 64U

struct poe_acc_endpoint {
	enum ndrv_mod_id module;
	unsigned int index;
	const char *name;
};

static const struct poe_acc_endpoint poe_acc_endpoints[] = {
	{ NDRV_SEC, 0U, "SEC0" },
	{ NDRV_SA, 0U, "SA0" },
	{ NDRV_TRING, 0U, "TRING0" },
	{ NDRV_SPPE_PKIO, 0U, "SPPE_PKIO0" },
	{ NDRV_SPPE_PKIO, 1U, "SPPE_PKIO1" },
};

#define CHECK(call)										\
	do {											\
		if ((call) != 0) {								\
			HNS5_LOG(ERR, "%s failed\n", #call);					\
			return -EIO;								\
		}										\
	} while (0)

static bool
poe_mask_is_empty(const unsigned int *mask, unsigned int words)
{
	unsigned int i;

	for (i = 0; i < words; ++i)
		if (mask[i] != 0U)
			return false;
	return true;
}

static int
check_group(struct hns5_sec_runtime *p, unsigned int group)
{
	struct ndrv_poe_pgrp_map_info map = { 0 };
	struct ndrv_poe_vf_grp_stats stats = { 0 };
	unsigned int enabled;

	CHECK(ndrv_poe_pf_get_pgrp_map(p->poe_pf_handle, group, &map));
	if (map.vfid != 0U || map.vgrp != group) {
		HNS5_LOG(ERR, "group %u map mismatch: vfid=%u vgrp=%u\n",
			 group, map.vfid, map.vgrp);
		return -1;
	}
	CHECK(ndrv_poe_vf_get_grp_out_enable(p->poe_vf_handle, group, &enabled));
	CHECK(ndrv_poe_vf_get_grp_stats(p->poe_vf_handle, group, &stats));
	if (stats.cur_pkt_cnt != 0U) {
		/* Stale packets from a previous abnormal exit. */
		HNS5_LOG(WARNING, "group %u has %u stale packets, ignoring\n",
			 group, stats.cur_pkt_cnt);
	}
	return 0;
}

static int
check_submit_group_unbound(struct hns5_sec_runtime *p,
			   const struct ndrv_poe_pf_grp_range *range)
{
	unsigned int mask[POE_MASK_WORDS] = { 0 };
	unsigned int words = (range->acc_grp_num + POE_MASK_WORD_BITS - 1U) /
			     POE_MASK_WORD_BITS;
	unsigned int local = p->submit_group - range->acc_grp_start;
	size_t i;

	/* Both operands are hardware-provided; an unsigned subtraction that
	 * wrapped would index mask[] out of range.
	 */
	if (local >= range->acc_grp_num) {
		HNS5_LOG(ERR, "submit group %u outside ACC range [%u, %u)\n",
			 p->submit_group, range->acc_grp_start,
			 range->acc_grp_start + range->acc_grp_num);
		return -EIO;
	}
	if (words == 0U || words > POE_MASK_WORDS)
		return -EIO;
	for (i = 0; i < sizeof(poe_acc_endpoints) / sizeof(poe_acc_endpoints[0]); ++i) {
		const struct poe_acc_endpoint *endpoint = &poe_acc_endpoints[i];
		unsigned int rc;

		memset(mask, 0, sizeof(mask));
		rc = ndrv_poe_vf_get_acc_bind_grp(p->poe_vf_handle, endpoint->module,
						  endpoint->index, mask, words);
		if (rc != 0U)
			return -1;
		if ((mask[local / POE_MASK_WORD_BITS] &
		     (1U << (local % POE_MASK_WORD_BITS))) != 0U) {
			/* Stale binding from a previous run. The submit group
			 * will be reconfigured in setup_sec(). */
			HNS5_LOG(WARNING, "submit group %u has stale acc binding "
				  "(endpoint %zu), will reconfigure\n",
				  p->submit_group, i);
		}
	}
	return 0;
}

static int
select_reserved_vcpu(struct hns5_sec_runtime *p,
		      const struct ndrv_poe_pf_grp_range *range)
{
	bool vcpu_free[POE_VCPU_LIMIT] = { false };
	unsigned int want = p->requested_vcpu ? p->requested_vcpu : HNS5_SEC_ENGINE_VCPU;
	struct ndrv_poe_lcpu_map_info cpu = { 0 };
	unsigned int mask[POE_MASK_WORDS] = { 0 };
	unsigned int words = (range->cpu_grp_num + POE_MASK_WORD_BITS - 1U) /
			     POE_MASK_WORD_BITS;
	unsigned int local = p->completion_group - range->cpu_grp_start;
	unsigned int vcpu_count = ndrv_poe_get_lcpu_num();
	unsigned int i;

	/* local indexes mask[] below; grp_num is its only real bound. */
	if (local >= range->cpu_grp_num) {
		HNS5_LOG(ERR, "completion group %u outside CPU range [%u, %u)\n",
			 p->completion_group, range->cpu_grp_start,
			 range->cpu_grp_start + range->cpu_grp_num);
		return -EIO;
	}
	if (words == 0U || words > POE_MASK_WORDS || vcpu_count == 0U ||
	    vcpu_count > POE_VCPU_LIMIT || want >= vcpu_count) {
		HNS5_LOG(ERR, "vcpu check failed: words=%u vcpu_count=%u engine_vcpu=%u\n",
			 words, vcpu_count, want);
		return -EIO;
	}
	for (i = 0; i < vcpu_count; ++i) {
		unsigned int rc;

		memset(mask, 0, sizeof(mask));
		rc = ndrv_poe_vf_get_vcpu_bind_grp(p->poe_vf_handle, i, mask, words);
		if (rc != 0U) {
			HNS5_LOG(ERR, "vcpu %u bind query failed: rc=%u\n", i, rc);
			return -1;
		}
		if ((mask[local / POE_MASK_WORD_BITS] &
		     (1U << (local % POE_MASK_WORD_BITS))) != 0U) {
			/* Stale binding from a previous abnormal exit (Ctrl+C/crash).
			 * Actual clearing is done in setup_poe via
			 * hidrv_poe_set_cpu_bind_group_mask(empty). Here we
			 * just clear the local bit so vcpu_free check passes. */
			HNS5_LOG(WARNING, "vcpu %u has stale binding to completion "
				  "group %u, will clear in setup\n",
				  i, p->completion_group);
			mask[local / POE_MASK_WORD_BITS] &=
				~(1U << (local % POE_MASK_WORD_BITS));
		}
		vcpu_free[i] = poe_mask_is_empty(mask, words);
	}
	for (i = 0; i < vcpu_count; ++i) {
		CHECK(ndrv_poe_pf_get_lcpu_map(p->poe_pf_handle, i, &cpu));
		if (cpu.vfid != 0U || cpu.vcpu != want)
			continue;
		if (cpu.vcpu >= vcpu_count) {
			HNS5_LOG(ERR, "mapped vcpu %u out of range (%u)\n",
				 cpu.vcpu, vcpu_count);
			return -1;
		}
		if (!vcpu_free[cpu.vcpu]) {
			HNS5_LOG(ERR, "engine vcpu %u is not free\n", cpu.vcpu);
			return -EIO;
		}
		p->vcpu = cpu.vcpu;
		return 0;
	}
	HNS5_LOG(ERR, "engine vcpu %u not found in lcpu map\n", want);
	return -EIO;
}

int
hns5_sec_platform_inspect(struct hns5_sec_runtime *p)
{
	struct ndrv_poe_pf_grp_range range = { 0 };
	int submit_status, completion_status, cpu_status, acc_status;

	p->poe_pf = hidrv_poe_get_pf_devid(0, 0);
	p->poe_vf = hidrv_poe_get_vf_devid(0, 0, 0);
	p->sec_pf = hidrv_sec_get_pf_devid(0, 0);
	p->sec_vf = hidrv_sec_get_vf_devid(0, 0, 0, HIDRV_SEC_VF_MODE_POE);
	HNS5_LOG(DEBUG, "poe_pf=%d poe_vf=%d sec_pf=%d sec_vf=%d\n",
		 p->poe_pf, p->poe_vf, p->sec_pf, p->sec_vf);
	if (p->poe_pf < 0 || p->poe_vf < 0 || p->sec_pf < 0 || p->sec_vf < 0)
		return -EIO;

	p->poe_pf_handle = hidrv_poe_pf_get_ndrv_handle(p->poe_pf);
	p->poe_vf_handle = hidrv_poe_vf_get_ndrv_handle(p->poe_vf);
	if (p->poe_pf_handle == NULL || p->poe_vf_handle == NULL)
		return -EIO;

	CHECK(ndrv_poe_pf_get_grp_range(p->poe_pf_handle, &range));
	p->range = range;
	HNS5_LOG(NOTICE, "poE grp range: cpu[start=%u num=%u] acc[start=%u num=%u]\n",
		 range.cpu_grp_start, range.cpu_grp_num,
		 range.acc_grp_start, range.acc_grp_num);

	/*
	 * Multi-instance feasibility probe (diagnostic only).
	 *
	 * Two crypto_hns5 vdevs in one process both take SEC vf0, completion
	 * group 12 and vcpu 1, and the second instance's setup rebinds them out
	 * from under the first ("stale binding ... clearing"), corrupting the
	 * shared engine once both drive traffic. Whether that can be fixed by
	 * giving each instance its own VF + groups depends on how many SEC VFs
	 * this board exposes. POE mode's PF conf carries no vf_num (only the QM
	 * conf does), so enumerate what actually resolves instead of trusting a
	 * field. get_vf_devid is a (die,pf,vf)->devid lookup; the loop stops at
	 * the first failure and does not touch the real sec_vf chosen above.
	 */
	{
		int vf_count = 0;
		int vf;

		for (vf = 0; vf < 16; vf++) {
			if (hidrv_sec_get_vf_devid(0, 0, vf, HIDRV_SEC_VF_MODE_POE) < 0)
				break;
			vf_count++;
		}
		HNS5_LOG(NOTICE,
			 "SEC VF probe: %d VF(s) available. Each instance needs its own "
			 "completion(CPU)+submit(ACC) group and vcpu (cpu %u..%u, acc %u..%u)\n",
			 vf_count, range.cpu_grp_start,
			 range.cpu_grp_start + range.cpu_grp_num - 1,
			 range.acc_grp_start,
			 range.acc_grp_start + range.acc_grp_num - 1);
	}
	if (!range.cpu_grp_num || !range.acc_grp_num ||
	    range.cpu_grp_start >= POE_GROUP_LIMIT ||
	    range.acc_grp_start >= POE_GROUP_LIMIT ||
	    range.cpu_grp_num > POE_GROUP_LIMIT - range.cpu_grp_start ||
	    range.acc_grp_num > POE_GROUP_LIMIT - range.acc_grp_start ||
	    (range.cpu_grp_start < range.acc_grp_start + range.acc_grp_num &&
	     range.acc_grp_start < range.cpu_grp_start + range.cpu_grp_num)) {
		HNS5_LOG(ERR, "POE group range invalid\n");
		return -EIO;
	}
	if (HNS5_SEC_ENGINE_COMPLETION_GRP < range.cpu_grp_start ||
	    HNS5_SEC_ENGINE_COMPLETION_GRP >= range.cpu_grp_start + range.cpu_grp_num) {
		HNS5_LOG(ERR, "completion group %u not in CPU range [%u, %u)\n",
			 HNS5_SEC_ENGINE_COMPLETION_GRP,
			 range.cpu_grp_start,
			 range.cpu_grp_start + range.cpu_grp_num);
		return -EIO;
	}
	if (range.acc_grp_start < POE_ACC_GRP_FIRST ||
	    range.acc_grp_start + range.acc_grp_num > POE_ACC_GRP_LAST) {
		HNS5_LOG(ERR, "ACC group window [%u, %u) outside the usable "
			 "[%u, %u)\n", range.acc_grp_start,
			 range.acc_grp_start + range.acc_grp_num,
			 POE_ACC_GRP_FIRST, POE_ACC_GRP_LAST);
		return -EIO;
	}
	p->completion_group = HNS5_SEC_ENGINE_COMPLETION_GRP;
	p->submit_group = range.acc_grp_start;

	/* Report the groups actually used: the CPU range must not overlap the
	 * packet path's groups, and groups 24..31 are forbidden for the packet
	 * path on this platform, so it helps to see what the BSP handed out. */
	HNS5_LOG(NOTICE, "group plan: completion=%u submit=%u (cpu range [%u,%u), "
		 "acc range [%u,%u))\n", p->completion_group, p->submit_group,
		 range.cpu_grp_start, range.cpu_grp_start + range.cpu_grp_num,
		 range.acc_grp_start, range.acc_grp_start + range.acc_grp_num);
	p->completion_group_local = p->completion_group - range.cpu_grp_start;
	p->submit_group_local = p->submit_group - range.acc_grp_start;
	/* Both *_local values index group_mask[] later (setup and readback);
	 * check them here so the invariant lives next to the subtraction.
	 */
	if (p->completion_group_local >= range.cpu_grp_num ||
	    p->submit_group_local >= range.acc_grp_num) {
		HNS5_LOG(ERR, "group ids out of range after planning: "
			 "completion=%u submit=%u\n",
			 p->completion_group, p->submit_group);
		return -EIO;
	}
	submit_status = check_group(p, p->submit_group);
	completion_status = check_group(p, p->completion_group);
	HNS5_LOG(DEBUG, "check_group: submit=%d completion=%d\n",
		 submit_status, completion_status);
	if (submit_status != 0 || completion_status != 0) {
		HNS5_LOG(ERR, "group check failed: submit_grp=%u(%d) completion_grp=%u(%d)\n",
			 p->submit_group, submit_status,
			 p->completion_group, completion_status);
		return -1;
	}
	cpu_status = select_reserved_vcpu(p, &range);
	acc_status = check_submit_group_unbound(p, &range);
	HNS5_LOG(DEBUG, "vcpu_select=%d submit_unbound=%d\n",
		 cpu_status, acc_status);
	if (cpu_status != 0 || acc_status != 0) {
		HNS5_LOG(ERR, "vcpu or group binding check failed: vcpu=%d unbound=%d\n",
			 cpu_status, acc_status);
		return -1;
	}
	return 0;
}

static int
setup_poe(struct hns5_sec_runtime *p)
{
	struct hidrv_poe_grp_cfg group = {
		.order_en = p->poe_order_en,
		.preload_en = p->poe_preload_en,
		.flow_en = p->poe_flow_en,
		.int_th = p->poe_int_th,
		.bp_ppe_th = p->poe_bp_th ? p->poe_bp_th
					  : POE_DEFAULT_BP_THRESHOLD
	};
	struct hidrv_poe_group_bind_mask mask = { { 0 } };
	struct hidrv_poe_group_bind_mask old = { { 0 } };
	unsigned int enabled, flow, bp;
	size_t i;

	CHECK(hidrv_poe_get_cpu_bind_group_mask(p->poe_vf, p->vcpu, &old));
	for (i = 0; i < sizeof(old.group_mask) / sizeof(old.group_mask[0]); ++i)
		if (old.group_mask[i]) {
			/* Stale binding from a previous run. Clear it by
			 * setting an empty mask, then re-bind below. */
			struct hidrv_poe_group_bind_mask empty = { { 0 } };

			HNS5_LOG(WARNING, "vcpu %u has stale group binding "
				  "(mask[%zu]=0x%lx), clearing\n",
				  p->vcpu, i,
				  (unsigned long)old.group_mask[i]);
			CHECK(hidrv_poe_set_cpu_bind_group_mask(p->poe_vf,
								  p->vcpu, &empty));
			break;
		}
	p->complete_open = true;
	CHECK(hidrv_poe_open_group(p->poe_vf, p->completion_group, &group));
	p->submit_open = true;
	CHECK(hidrv_poe_open_group(p->poe_vf, p->submit_group, &group));

	CHECK(ndrv_poe_vf_get_grp_out_enable(p->poe_vf_handle,
			p->completion_group, &enabled));
	CHECK(ndrv_poe_vf_get_grp_overflow_enable(p->poe_vf_handle,
			p->completion_group, &flow));
	CHECK(ndrv_poe_vf_get_grp_bp_th(p->poe_vf_handle,
			p->completion_group, &bp));
	if (enabled != 1U || flow != group.flow_en || bp != group.bp_ppe_th) {
		HNS5_LOG(ERR, "completion group config mismatch: en=%u flow=%u "
			 "(want %u) bp=%u (want %u)\n", enabled, flow,
			 group.flow_en, bp, group.bp_ppe_th);
		return -EIO;
	}
	HNS5_LOG(NOTICE, "group cfg: order=%u preload=%u flow=%u int_th=%u "
		 "bp_ppe_th=0x%x\n", group.order_en, group.preload_en,
		 group.flow_en, group.int_th, group.bp_ppe_th);
	CHECK(ndrv_poe_vf_get_grp_out_enable(p->poe_vf_handle,
			p->submit_group, &enabled));
	CHECK(ndrv_poe_vf_get_grp_overflow_enable(p->poe_vf_handle,
			p->submit_group, &flow));
	CHECK(ndrv_poe_vf_get_grp_bp_th(p->poe_vf_handle, p->submit_group, &bp));
	if (enabled != 1U || flow != group.flow_en || bp != group.bp_ppe_th) {
		HNS5_LOG(ERR, "submit group config mismatch: en=%u flow=%u (want %u) "
			 "bp=%u (want %u)\n", enabled, flow, group.flow_en, bp,
			 group.bp_ppe_th);
		return -EIO;
	}

	mask.group_mask[p->completion_group_local / 64U] = UINT64_C(1) <<
		(p->completion_group_local % 64U);
	p->cpu_bound = true;
	CHECK(hidrv_poe_set_cpu_bind_group_mask(p->poe_vf, p->vcpu, &mask));
	return 0;
}

/* Upper bound for one drain pass; a stale backlog is small, the limit only
 * protects against an endless queue. */
#define HNS5_SEC_STALE_MSG_MAX 4096U

/*
 * Discard completion messages left in the completion group by a previous run.
 *
 * hns5_sec_reap_completion() validates that the buffer address carried by the
 * message equals the address of the slot being reaped. Leftover messages carry
 * the physical address of the *previous* run's hugepage pool, so they can never
 * match: the reap returns -EIO, the dequeue path stops without freeing the SEC
 * buffer, and after 448 such slots the pool is exhausted, turning every packet
 * into a crypto error. That is why the second start after a process exit used
 * to fail while the first start after boot worked.
 *
 * Only this driver's own completion group is touched, so this cannot interfere
 * with the packet path of net/hns5.
 */
/*
 * A previous abnormal run (crash, or a refused submit left in the POE queue)
 * can leave packets inside the submit group. They are queued ahead of our first
 * task and may reference buffers that no longer exist, which makes the engine
 * stall - the device then appears to hang with no further output.
 *
 * hidrv_poe_clear_group() discards those packets. If it does not help, fall back
 * to a POE VF reset, which wipes the group configuration as well: the caller
 * then has to configure the groups again.
 *
 * Returns 0 when the queues are clean, 1 when a VF reset was performed (groups
 * must be reconfigured), -1 when stale packets could not be removed.
 */
static int
purge_stale_poe_packets(struct hns5_sec_runtime *p)
{
	struct ndrv_poe_vf_grp_stats stats;
	unsigned int groups[2];
	unsigned int i;
	unsigned int cleared = 0;
	unsigned int left = 0;

	groups[0] = p->completion_group;
	groups[1] = p->submit_group;

	for (i = 0; i < 2; i++) {
		memset(&stats, 0, sizeof(stats));
		if (ndrv_poe_vf_get_grp_stats(p->poe_vf_handle, groups[i],
					      &stats) != 0U || stats.cur_pkt_cnt == 0U)
			continue;
		HNS5_LOG(WARNING, "group %u holds %u stale packet(s) from a "
			 "previous run (the engine would stall behind them)",
			 groups[i], stats.cur_pkt_cnt);
		if (hidrv_poe_clear_group((uint32_t)p->poe_vf, p->vcpu,
					  groups[i]) != 0) {
			HNS5_LOG(WARNING, "clear_group(%u) failed", groups[i]);
			left += stats.cur_pkt_cnt;
		} else {
			cleared += stats.cur_pkt_cnt;
		}
	}

	if (cleared != 0)
		HNS5_LOG(NOTICE, "cleared %u stale packet(s) from the POE groups",
			 cleared);

	/* Re-check: clear_group may have left some behind. */
	left = 0;
	for (i = 0; i < 2; i++) {
		memset(&stats, 0, sizeof(stats));
		if (ndrv_poe_vf_get_grp_stats(p->poe_vf_handle, groups[i],
					      &stats) == 0U)
			left += stats.cur_pkt_cnt;
	}
	if (left == 0)
		return 0;

	/*
	 * The engine itself is stuck on an earlier descriptor. Only a SEC VF
	 * soft reset can drain it (it also returns the buffers through a BMU
	 * callback), but that needs the SEC VF native handle, which the shipped
	 * BSP does not expose. Try anyway in case a future BSP exports it.
	 */
	{
		typedef unsigned int (*soft_reset_fn)(void *handle, void *usr_data);
		typedef void *(*get_vf_handle_fn)(unsigned int dev_id);
		soft_reset_fn reset = (soft_reset_fn)(uintptr_t)dlsym(
			RTLD_DEFAULT, "ndrv_sec_vf_soft_reset");
		get_vf_handle_fn get_vf = (get_vf_handle_fn)(uintptr_t)dlsym(
			RTLD_DEFAULT, "hidrv_sec_vf_get_ndrv_handle");
		void *vf_handle = NULL;

		if (get_vf != NULL)
			vf_handle = get_vf((unsigned int)p->sec_vf);
		if (reset == NULL)
			HNS5_LOG(WARNING, "ndrv_sec_vf_soft_reset is not exported "
				 "by the BSP: a stuck SEC engine can only be "
				 "cleared by rebooting");
		else if (vf_handle == NULL)
			HNS5_LOG(WARNING, "SEC VF native handle is not available "
				 "(hidrv_sec_vf_get_ndrv_handle missing): cannot "
				 "soft reset a stuck engine");
		else if (reset(vf_handle, NULL) == 0U) {
			HNS5_LOG(NOTICE, "SEC VF soft reset done");
			return 1;
		} else
			HNS5_LOG(WARNING, "ndrv_sec_vf_soft_reset failed");
	}

	HNS5_LOG(WARNING, "%u stale packet(s) still queued; resetting the POE VF",
		 left);
	if (ndrv_poe_vf_reset(p->poe_vf_handle) != 0U) {
		HNS5_LOG(ERR, "%u stale packet(s) are stuck in the submit group and "
			 "the SEC engine cannot be reset from the driver: the engine "
			 "is hung. Reboot the board; afterwards this driver refuses "
			 "to run on a stuck engine instead of hanging", left);
		return -1;
	}
	HNS5_LOG(NOTICE, "POE VF reset done: groups will be configured again");
	return 1;
}

static void
drain_stale_completions(struct hns5_sec_runtime *p)
{
	union ndrv_poe_msg msg = { 0 };
	unsigned int queue;
	unsigned int n;
	unsigned int total = 0;

	for (queue = 0; queue < 2U; queue++) {
		for (n = 0; n < HNS5_SEC_STALE_MSG_MAX; n++) {
			if (ndrv_poe_vf_get_que_msg(p->poe_vf_handle, p->vcpu,
						    p->completion_group, queue, &msg))
				break;
			total++;
		}
		if (n == HNS5_SEC_STALE_MSG_MAX)
			HNS5_LOG(WARNING, "completion group %u queue %u still busy after %u drains\n",
				 p->completion_group, queue, n);
	}

	if (total != 0)
		HNS5_LOG(WARNING, "discarded %u stale completion msg(s) from group %u left by a previous run\n",
			 total, p->completion_group);
}

/*
 * The two engine options that are supported on this silicon
 * (ndrv_sec_vf_set_skip_length, ndrv_sec_vf_set_queue_get_poe_enable) live in
 * the native layer and are NOT exported by the shipped BSP library, and the
 * SEC VF native handle is not exposed either (only mag/poe/ppe have
 * hidrv_*_get_ndrv_handle). They are therefore applied only when the BSP
 * exports the symbols: resolved with dlsym() at runtime, so no link-time
 * dependency is added and the default behaviour is unchanged.
 */
static void
hns5_sec_apply_vf_opts(struct hns5_sec_runtime *p)
{
	typedef void *(*get_handle_fn)(int dev_id);
	typedef unsigned int (*set_u32_fn)(void *handle, unsigned int value);
	get_handle_fn get_handle;
	set_u32_fn set_skip;
	set_u32_fn set_getpoe;
	void *vf_handle;
	unsigned int rc;

	if (p->req_skip_len == 0 && p->req_get_poe == 0)
		return;

	/*
	 * hidrv has public handle helpers for exactly this purpose:
	 *   handle hidrv_sec_open_vf(unsigned int dev_id);          (handle_api.h)
	 *   handle hidrv_get_vf_handle(enum hidrv_urm_id, dev_id);
	 * Resolve them with dlsym so that a BSP without them simply skips the
	 * options instead of failing to load the application.
	 */
	{
		typedef handle (*open_vf_fn)(unsigned int dev_id);
		typedef handle (*get_vf_fn)(int acc_id, unsigned int dev_id);
		open_vf_fn open_vf;
		get_vf_fn get_vf;

		open_vf = (open_vf_fn)dlsym(RTLD_DEFAULT, "hidrv_sec_open_vf");
		if (open_vf != NULL) {
			HNS5_LOG(NOTICE, "sec opts: using hidrv_sec_open_vf(%d)\n",
				 p->sec_vf);
			vf_handle = open_vf((unsigned int)p->sec_vf);
			goto have_handle;
		}
		get_vf = (get_vf_fn)dlsym(RTLD_DEFAULT, "hidrv_get_vf_handle");
		if (get_vf != NULL) {
			HNS5_LOG(NOTICE, "sec opts: using hidrv_get_vf_handle(URM_SEC=%d, %d)\n",
				 (int)URM_SEC, p->sec_vf);
			vf_handle = get_vf((int)URM_SEC, (unsigned int)p->sec_vf);
			goto have_handle;
		}
		get_handle = (get_handle_fn)dlsym(RTLD_DEFAULT,
						  "hidrv_sec_vf_get_ndrv_handle");
		if (get_handle == NULL)
			get_handle = (get_handle_fn)dlsym(RTLD_DEFAULT,
							  "hidrv_sec_vf_open_handle");
		if (get_handle == NULL) {
			HNS5_LOG(WARNING, "sec opts: no SEC VF handle helper found "
				 "(hidrv_sec_open_vf / hidrv_get_vf_handle / "
				 "hidrv_sec_vf_get_ndrv_handle); options ignored\n");
			return;
		}
		vf_handle = get_handle(p->sec_vf);
	}

have_handle:
	if (vf_handle == NULL) {
		HNS5_LOG(WARNING, "sec opts: no native SEC VF handle for "
			 "dev_id %d\n", p->sec_vf);
		return;
	}

	if (p->req_skip_len != 0) {
		set_skip = (set_u32_fn)dlsym(RTLD_DEFAULT,
					     "ndrv_sec_vf_set_skip_length");
		if (set_skip == NULL) {
			HNS5_LOG(WARNING, "sec opts: ndrv_sec_vf_set_skip_length "
				 "not exported; skipping\n");
		} else {
			rc = set_skip(vf_handle, p->req_skip_len);
			if (rc != 0)
				HNS5_LOG(WARNING, "sec skip_length=%u failed rc=%u\n",
					 p->req_skip_len, rc);
			else
				HNS5_LOG(NOTICE, "sec skip_length=%u applied\n",
					 p->req_skip_len);
		}
	}
	if (p->req_get_poe != 0) {
		set_getpoe = (set_u32_fn)dlsym(RTLD_DEFAULT,
					       "ndrv_sec_vf_set_queue_get_poe_enable");
		if (set_getpoe == NULL) {
			HNS5_LOG(WARNING, "sec opts: "
				 "ndrv_sec_vf_set_queue_get_poe_enable not "
				 "exported; skipping\n");
		} else {
			rc = set_getpoe(vf_handle, 1U);
			if (rc != 0)
				HNS5_LOG(WARNING, "sec get_poe=1 failed rc=%u\n", rc);
			else
				HNS5_LOG(NOTICE, "sec get_poe=1 applied\n");
		}
	}
}

static int
setup_sec(struct hns5_sec_runtime *p)
{
	struct hidrv_sec_pf_conf pf = { 0 };
	struct hidrv_sec_vf_conf vf = { 0 };
	struct hidrv_poe_group_bind_mask cpu_mask = { { 0 } };
	struct hidrv_poe_group_bind_mask sec_mask = { { 0 } };
	uint64_t expected;
	size_t i;
	int rc;

	CHECK(ndrv_poe_vf_cvt_que_to_vqi(p->poe_vf_handle, p->submit_group, 0,
			&p->submit_vqi));
	CHECK(ndrv_poe_vf_cvt_que_to_vqi(p->poe_vf_handle, p->completion_group, 0,
			&p->completion_vqi));
	CHECK(ndrv_poe_vf_cvt_que_to_vqi(p->poe_vf_handle, p->completion_group, 1,
			&p->error_vqi));
	pf.mode = HIDRV_SEC_PF_MODE_POE;
	pf.sec_bd_endian = HIDRV_SEC_ENDIAN_32_LITTLE;

	rc = hidrv_sec_pf_setup((uint32_t)p->sec_pf, &pf, NULL);
	if (rc != 0) {
		hns5_bmu_poison(p->common, "SEC PF init failed");
		return -1;
	}
	p->sec_pf_started = true;
	vf.mode = HIDRV_SEC_VF_MODE_POE;
	vf.poe_conf.get_packet_group = p->submit_group;
	vf.poe_conf.get_packet_vqi = p->submit_vqi;
	vf.poe_conf.add_packet_group = p->completion_group;
	vf.poe_conf.add_packet_vqi = p->completion_vqi;
	vf.poe_conf.add_err_packet_vqi = p->error_vqi;

	rc = hidrv_sec_vf_setup((uint32_t)p->sec_vf, &vf, NULL);
	if (rc != 0) {
		hns5_bmu_poison(p->common, "SEC VF init failed");
		return -1;
	}
	p->sec_vf_started = true;
	hns5_sec_apply_vf_opts(p);

	CHECK(hidrv_poe_get_cpu_bind_group_mask(p->poe_vf, p->vcpu, &cpu_mask));
	CHECK(hidrv_poe_get_acc_bind_group_mask(p->poe_vf, HIDRV_POE_ACC_SEC, 0,
			&sec_mask));
	for (i = 0; i < sizeof(cpu_mask.group_mask) / sizeof(cpu_mask.group_mask[0]); ++i) {
		expected = (i == p->completion_group_local / 64U)
			   ? UINT64_C(1) << (p->completion_group_local % 64U)
			   : 0U;
		if (cpu_mask.group_mask[i] != expected) {
			HNS5_LOG(ERR, "cpu mask mismatch: mask[%zu]=0x%lx expected=0x%lx\n",
				 i, (unsigned long)cpu_mask.group_mask[i],
				 (unsigned long)expected);
			return -EIO;
		}
	}
	{
		unsigned int bound = 0, gi;

		for (gi = 0; gi < sizeof(sec_mask.group_mask) /
			     sizeof(sec_mask.group_mask[0]); gi++)
			bound += (unsigned int)__builtin_popcountll(
				(unsigned long long)sec_mask.group_mask[gi]);
		HNS5_LOG(NOTICE, "POE: SEC is bound to %u acc group(s)%s", bound,
			 bound > 1U ? " (multi-group fetch)" : "");
	}

	if ((sec_mask.group_mask[p->submit_group_local / 64U] &
	     (UINT64_C(1) << (p->submit_group_local % 64U))) == 0U) {
		HNS5_LOG(ERR, "SEC not bound to submit group %u (local=%u)\n",
			 p->submit_group, p->submit_group_local);
		return -EIO;
	}
	return 0;
}

int
hns5_sec_platform_start(struct hns5_sec_runtime *r)
{
	const struct hidrv_mbuf_params layout = { 128, 128, 128 };

	if (r->common == NULL)
		return -EINVAL;
	hidrv_mbuf_set_config(&layout);

	/*
	 * No SEC unsetup here on purpose: hidrv_sec_pf_unsetup() leaves the PF in
	 * a state that a later hidrv_sec_pf_setup() cannot recover from (the next
	 * setup fails with -EIO). The engine is instead torn down at process exit
	 * by hns5_crypto_remove() -> hns5_crypto_teardown(), so every run starts
	 * from a clean engine like the first run after boot does.
	 */

	if (setup_poe(r) || setup_sec(r)) {
		hns5_bmu_poison(r->common, "SEC/POE partial initialization");
		return -EIO;
	}
	HNS5_LOG(NOTICE, "channel 0: vcpu=%u submit=%u(vqi %u) completion=%u"
		 "(vqi %u/%u)", r->vcpu, r->submit_group, r->submit_vqi,
		 r->completion_group, r->completion_vqi, r->error_vqi);

	/* Groups are open now: drop packets a previous run left queued, they
	 * would stall the engine ahead of our first task. */
	if (r->poe_vf_handle != NULL) {
		int pr = purge_stale_poe_packets(r);

		if (pr < 0) {
			/* Engine hung: fail loudly instead of letting the
			 * application block forever on completions. */
			HNS5_LOG(ERR, "SEC engine is stuck; refusing to start "
				 "(reboot the board to clear it)");
			hns5_bmu_poison(r->common, "SEC engine stuck");
			return -EIO;
		}
		if (pr > 0) {
			/* The VF reset wiped the group configuration. */
			if (setup_poe(r) != 0 || setup_sec(r) != 0) {
				hns5_bmu_poison(r->common,
						    "SEC/POE re-init after reset");
				return -EIO;
			}
		}
	}
	/* Groups are configured and the VCPU is bound now, so the completion
	 * queues are readable: drop whatever the previous process left there
	 * before the first operation of this process is submitted. */
	drain_stale_completions(r);
	return 0;
}

/* Find a vcpu that the lcpu map assigns to this VF. */
static int
find_channel_vcpu(struct hns5_sec_runtime *p, unsigned int want, unsigned int *out)
{
	struct ndrv_poe_lcpu_map_info cpu = { 0 };
	unsigned int mask[POE_MASK_WORDS] = { 0 };
	unsigned int words = (p->range.cpu_grp_num + POE_MASK_WORD_BITS - 1U) /
			     POE_MASK_WORD_BITS;
	unsigned int vcpu_count = ndrv_poe_get_lcpu_num();
	unsigned int local = p->completion_group - p->range.cpu_grp_start;
	unsigned int i;

	if (local >= p->range.cpu_grp_num ||
	    words == 0U || words > POE_MASK_WORDS || want >= vcpu_count)
		return -EINVAL;

	for (i = 0; i < vcpu_count; ++i) {
		CHECK(ndrv_poe_pf_get_lcpu_map(p->poe_pf_handle, i, &cpu));
		if (cpu.vfid == 0U && cpu.vcpu == want)
			break;
	}
	if (i == vcpu_count) {
		HNS5_LOG(ERR, "channel %u: vcpu %u is not mapped to this VF\n",
			 p->chan_index, want);
		return -EIO;
	}

	memset(mask, 0, sizeof(mask));
	if (ndrv_poe_vf_get_vcpu_bind_grp(p->poe_vf_handle, want, mask, words) != 0U) {
		HNS5_LOG(ERR, "channel %u: vcpu %u bind query failed\n",
			 p->chan_index, want);
		return -EIO;
	}
	if ((mask[local / POE_MASK_WORD_BITS] &
	     (1U << (local % POE_MASK_WORD_BITS))) != 0U)
		HNS5_LOG(WARNING, "channel %u: vcpu %u has stale binding to "
			 "completion group %u, will clear in setup\n",
			 p->chan_index, want, p->completion_group);

	*out = want;
	return 0;
}


/*
 * Debug-only micro benchmark (devarg debug_memcpy=1).
 *
 * Compares three 1 KiB copies:
 *   warm-read     same source lines repeatedly (cache resident)
 *   warm-write    same destination lines repeatedly
 *   distinct-read every iteration reads a different segment (fresh lines) -
 *                 the situation of reading the SEC buffer right after the
 *                 engine DMA'd into it
 * This tells whether the dequeue copy is expensive because of cache state or
 * because the memory itself is slow.
 */
void
hns5_sec_bench_memcpy(struct hns5_sec_runtime *r)
{
	enum { ITERS = 2000, BYTES = 1024, SEGS = 32 };
	static uint8_t src[BYTES] __attribute__((aligned(64)));
	static uint8_t dst[BYTES] __attribute__((aligned(64)));
	static volatile uint64_t sink;
	void *pool[SEGS];
	uint64_t pa;
	uint64_t hz = rte_get_tsc_hz();
	uint64_t t0, warm_read, warm_write, distinct_read;
	unsigned int n = 0, i;
	void *seg;

	if (r == NULL || r->common == NULL || !r->debug_memcpy)
		return;

	seg = hns5_bmu_alloc(r->common, &pa);
	if (seg == NULL) {
		HNS5_LOG(WARNING, "memcpy bench: no segment\n");
		return;
	}
	memset(src, 0x5a, sizeof(src));
	memset(dst, 0, sizeof(dst));

	/* Consume a byte of dst so the copies cannot be optimised away. */
	t0 = hns5_tsc();
	for (i = 0; i < ITERS; i++) {
		rte_memcpy(dst, (uint8_t *)seg + HNS5_SEC_DATA_OFFSET +
			   HNS5_SEC_PREFIX_BYTES, BYTES);
		sink += dst[i & (BYTES - 1)];
	}
	warm_read = hns5_tsc() - t0;

	t0 = hns5_tsc();
	for (i = 0; i < ITERS; i++) {
		src[i & (BYTES - 1)] = (uint8_t)i;
		rte_memcpy((uint8_t *)seg + HNS5_SEC_DATA_OFFSET +
			   HNS5_SEC_PREFIX_BYTES, src, BYTES);
	}
	sink += *(volatile uint8_t *)((uint8_t *)seg + HNS5_SEC_DATA_OFFSET +
				      HNS5_SEC_PREFIX_BYTES);
	warm_write = hns5_tsc() - t0;

	while (n < SEGS) {
		void *p = hns5_bmu_alloc(r->common, &pa);

		if (p == NULL)
			break;
		pool[n++] = p;
	}
	t0 = hns5_tsc();
	for (i = 0; i < ITERS && n != 0; i++) {
		rte_memcpy(dst, (uint8_t *)pool[i % n] + HNS5_SEC_DATA_OFFSET +
			   HNS5_SEC_PREFIX_BYTES, BYTES);
		sink += dst[i & (BYTES - 1)];
	}
	distinct_read = hns5_tsc() - t0;
	while (n != 0)
		(void)hns5_bmu_free(r->common, pool[--n]);

	(void)hns5_bmu_free(r->common, seg);

	HNS5_LOG(NOTICE, "memcpy bench (1 KiB, %u iters): warm-read=%.0f ns "
		 "warm-write=%.0f ns distinct-region-read=%.0f ns "
		 "(%u segments, sink=%" PRIu64 ")\n",
		 (unsigned int)ITERS,
		 hz ? (double)(warm_read / ITERS) * 1e9 / (double)hz : 0.0,
		 hz ? (double)(warm_write / ITERS) * 1e9 / (double)hz : 0.0,
		 hz ? (double)(distinct_read / ITERS) * 1e9 / (double)hz : 0.0,
		 SEGS, sink);
}

int
hns5_sec_channels_start(struct hns5_sec_runtime *primary,
			struct hns5_sec_runtime *extra, unsigned int count)
{
	unsigned int i;
	int rc;

	if (primary == NULL || count <= 1U)
		return 0;

	for (i = 1U; i < count; ++i) {
		struct hns5_sec_runtime *c = &extra[i - 1U];
		unsigned int vcpu = 0;

		/* Share every handle and pool with the primary channel. */
		*c = *primary;
		c->chan_index = i;
		c->submit_open = false;
		c->complete_open = false;
		c->cpu_bound = false;
		/* The SEC PF/VF belong to the primary channel: the copy must not
		 * tear them down when this channel closes. */
		c->sec_pf_started = false;
		c->sec_vf_started = false;

		/*
		 * POE routes every SEC completion to one group per VF, so all queue
		 * pairs share the submit group and the completion group; they differ
		 * only in the vcpu used to push tasks (one push queue per vcpu).
		 * Only the primary channel binds a vcpu to the completion group,
		 * because a single reader drains that group.
		 */
		c->submit_group = primary->submit_group;
		c->completion_group = primary->completion_group;
		c->submit_group_local = primary->submit_group_local;
		c->completion_group_local = primary->completion_group_local;
		c->submit_vqi = primary->submit_vqi;
		c->completion_vqi = primary->completion_vqi;
		c->error_vqi = primary->error_vqi;

		rc = find_channel_vcpu(c, primary->vcpu + i, &vcpu);
		if (rc != 0)
			return rc;
		c->vcpu = vcpu;

		CHECK(ndrv_poe_vf_cvt_que_to_vqi(c->poe_vf_handle, c->submit_group, 0,
				&c->submit_vqi));
		CHECK(ndrv_poe_vf_cvt_que_to_vqi(c->poe_vf_handle, c->completion_group, 0,
				&c->completion_vqi));
		CHECK(ndrv_poe_vf_cvt_que_to_vqi(c->poe_vf_handle, c->completion_group, 1,
				&c->error_vqi));

		/* The POE groups belong to the primary channel and were drained
		 * there: nothing to open or drain per extra channel. */

		HNS5_LOG(NOTICE, "channel %u: vcpu=%u submit=%u(vqi %u) completion=%u"
			 "(vqi %u/%u)\n", i, c->vcpu, c->submit_group, c->submit_vqi,
			 c->completion_group, c->completion_vqi, c->error_vqi);
	}
	return 0;
}

/*
 * Counters that the data path updates are per queue pair (see struct hns5_qp),
 * so tearing down a channel means asking every queue pair it serves.
 */
static bool
hns5_sec_channel_busy(struct hns5_sec_runtime *r)
{
	struct hns5_dev_private *priv = r->priv;
	uint16_t i;

	if (priv == NULL || priv->qps == NULL)
		return false;
	for (i = 0; i < priv->nb_used_qps; i++) {
		struct hns5_qp *qp = priv->qps[i];

		if (qp != NULL && qp->sec == r && qp->in_use)
			return true;
	}
	return false;
}

static void
hns5_sec_qp_counters_dump(struct hns5_sec_runtime *r)
{
	struct hns5_dev_private *priv = r->priv;
	uint16_t i;

	if (priv == NULL || priv->qps == NULL)
		return;
	for (i = 0; i < priv->nb_used_qps; i++) {
		struct hns5_qp *qp = priv->qps[i];

		if (qp == NULL || qp->sec != r)
			continue;
		if (qp->cache_hits == 0 && qp->cap_hits == 0)
			continue;
		HNS5_LOG(NOTICE, "qp %u (channel %u): cache hits=%" PRIu64
			 ", in-flight cap reached=%" PRIu64 " time(s)\n",
			 i, r->chan_index, qp->cache_hits, qp->cap_hits);
	}
}

int
hns5_sec_platform_close(struct hns5_sec_runtime *r)
{
	struct hidrv_poe_group_bind_mask empty = { { 0 } };
	int rc = 0;

	if (r == NULL)
		return 0;

	if (hns5_sec_channel_busy(r))
		return -EBUSY;

	if (hns5_sec_stats_en && (r->send_calls != 0 || r->alloc_calls != 0)) {
		uint64_t hz = rte_get_tsc_hz();

		HNS5_LOG(NOTICE, "channel %u: submit calls=%" PRIu64
			 " avg=%" PRIu64 " cyc (%.0f ns) max=%" PRIu64 " cyc | "
			 "bmu alloc calls=%" PRIu64 " avg=%" PRIu64 " cyc (%.0f ns) "
			 "max=%" PRIu64 " cyc\n", r->chan_index,
			 r->send_calls,
			 r->send_calls ? r->send_cycles / r->send_calls : 0,
			 (hz && r->send_calls) ?
			    (double)(r->send_cycles / r->send_calls) * 1e9 / (double)hz : 0.0,
			 r->send_max_cycles,
			 r->alloc_calls,
			 r->alloc_calls ? r->alloc_cycles / r->alloc_calls : 0,
			 (hz && r->alloc_calls) ?
			    (double)(r->alloc_cycles / r->alloc_calls) * 1e9 / (double)hz : 0.0,
			 r->alloc_max_cycles);
	}
	if (r->zc_refused != 0 || r->auth_mismatch != 0 || r->send_fail != 0) {
		HNS5_LOG(NOTICE, "channel %u: suppressed-log totals: zc_in refused=%" PRIu64
			 ", auth mismatch=%" PRIu64 ", submit failed=%" PRIu64 "\n",
			 r->chan_index, r->zc_refused, r->auth_mismatch, r->send_fail);
	}
	hns5_sec_qp_counters_dump(r);
	if (r->smooth_backlog != 0 || r->smooth_spins != 0)
		HNS5_LOG(NOTICE, "channel %u: smooth: backlogged=%" PRIu64
			 ", dequeue spins that found nothing=%" PRIu64 "\n",
			 r->chan_index, r->smooth_backlog, r->smooth_spins);
	if (r->cache_misses != 0 || r->refill_fail != 0 || r->free_fail != 0)
		HNS5_LOG(NOTICE, "channel %u: cache refill misses=%" PRIu64
			 " ok=%" PRIu64 " fail=%" PRIu64 " allocs=%" PRIu64
			 " free_to_pool=%" PRIu64 " free_fail=%" PRIu64 "\n",
			 r->chan_index, r->cache_misses, r->refill_ok,
			 r->refill_fail, r->alloc_calls, r->free_to_pool,
			 r->free_fail);
	if (r->zc_used != 0 || r->zc_fallback != 0 || r->zc_pool_num != 0) {
		HNS5_LOG(NOTICE, "channel %u: zero-copy mbuf-input ops=%" PRIu64
			 ", fallback-to-copy=%" PRIu64 ", app pools registered=%u\n",
			 r->chan_index, r->zc_used, r->zc_fallback, r->zc_pool_num);
	}
	if (r->enq_ops != 0 || r->deq_ops != 0) {
		uint64_t hz = rte_get_tsc_hz();
		uint64_t enq = r->enq_ops ? r->enq_cycles / r->enq_ops : 0;
		uint64_t deq = r->deq_ops ? r->deq_cycles / r->deq_ops : 0;

		HNS5_LOG(NOTICE, "channel %u: driver enqueue=%" PRIu64 " cyc "
			 "(%.0f ns)/op over %" PRIu64 " ops, dequeue=%" PRIu64
			 " cyc (%.0f ns)/op over %" PRIu64 " ops\n",
			 r->chan_index, enq,
			 hz ? (double)enq * 1e9 / (double)hz : 0.0, r->enq_ops,
			 deq, hz ? (double)deq * 1e9 / (double)hz : 0.0, r->deq_ops);
	}
	if (r->ext_calls != 0) {
		uint64_t hz = rte_get_tsc_hz();
		uint64_t c = r->ext_calls;

		HNS5_LOG(NOTICE, "channel %u: extract internals per op: "
			 "parse=%.0f ns digest=%.0f ns copy=%.0f ns\n",
			 r->chan_index,
			 hz ? (double)(r->ext_parse_cycles / c) * 1e9 / (double)hz : 0.0,
			 hz ? (double)(r->ext_digest_cycles / c) * 1e9 / (double)hz : 0.0,
			 hz ? (double)(r->ext_copy_cycles / c) * 1e9 / (double)hz : 0.0);
	}
	if (r->ref_calls != 0 || r->ext_calls != 0) {
		uint64_t hz = rte_get_tsc_hz();

		HNS5_LOG(NOTICE, "channel %u: poe_refresh calls=%" PRIu64
			 " avg=%" PRIu64 " cyc (%.0f ns) max=%" PRIu64 " | "
			 "extract calls=%" PRIu64 " avg=%" PRIu64 " cyc (%.0f ns)\n",
			 r->chan_index, r->ref_calls,
			 r->ref_calls ? r->ref_cycles / r->ref_calls : 0,
			 (hz && r->ref_calls) ?
			    (double)(r->ref_cycles / r->ref_calls) * 1e9 / (double)hz : 0.0,
			 r->ref_max_cycles,
			 r->ext_calls,
			 r->ext_calls ? r->ext_cycles / r->ext_calls : 0,
			 (hz && r->ext_calls) ?
			    (double)(r->ext_cycles / r->ext_calls) * 1e9 / (double)hz : 0.0);
	}
	hns5_sec_bench_memcpy(r);

	if (r->reap_calls != 0) {
		uint64_t hz = rte_get_tsc_hz();

		HNS5_LOG(NOTICE, "channel %u: POE read calls=%" PRIu64 ", "
			 "avg=%" PRIu64 " cycles (%.0f ns), max=%" PRIu64
			 " cycles (%.0f ns)\n",
			 r->chan_index, r->reap_calls,
			 r->reap_cycles / r->reap_calls,
			 hz ? (double)(r->reap_cycles / r->reap_calls) * 1e9 / (double)hz : 0.0,
			 r->reap_max_cycles,
			 hz ? (double)r->reap_max_cycles * 1e9 / (double)hz : 0.0);
	}

	if (r->sec_vf_started && hidrv_sec_vf_unsetup((uint32_t)r->sec_vf))
		rc = -EIO;
	if (r->sec_pf_started && hidrv_sec_pf_unsetup((uint32_t)r->sec_pf))
		rc = -EIO;
	if (r->cpu_bound &&
	    hidrv_poe_set_cpu_bind_group_mask(r->poe_vf, r->vcpu, &empty))
		rc = -EIO;
	if (r->submit_open && hidrv_poe_close_group(r->poe_vf, r->submit_group))
		rc = -EIO;
	if (r->complete_open && hidrv_poe_close_group(r->poe_vf, r->completion_group))
		rc = -EIO;
	if (rc)
		hns5_bmu_poison(r->common, "SEC close incomplete");
	r->sec_vf_started = false;
	r->sec_pf_started = false;
	r->cpu_bound = false;
	r->submit_open = false;
	r->complete_open = false;
	return rc;
}
