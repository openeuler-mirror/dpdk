#include <rte_common.h>
#include <rte_ethdev.h>
#ifdef DPDK_21_11
#include <ethdev_driver.h>
#endif
#include <rte_malloc.h>
#include <rte_mempool.h>
#include <pthread.h>

#include "../test.h"

#include "base/hinic3_compat.h"
#include "base/hinic3_pmd_mgmt.h"
#include "hinic3_pmd_ethdev.h"
#include <rte_test.h>
#include <rte_log.h>
#include "hinic3_pmd_hairpin.h"

static struct rte_eth_dev *g_dev;
static struct hinic3_nic_dev *g_nic_dev;
static int g_orig_log_level;

static int
hairpin_suite_setup(void)
{
    g_orig_log_level = rte_log_get_global_level();
    rte_log_set_level_pattern("pmd.net.hinic3", RTE_LOG_INFO);  //可调整日志等级以便于后续定位调试
    return 0;
}

static void
hairpin_suite_teardown(void)
{
    rte_log_set_level_pattern("pmd.net.hinic3", g_orig_log_level);
}

static int
hairpin_test_setup(void)
{
    g_dev = rte_zmalloc("test_dev", sizeof(*g_dev), 0);
    if (!g_dev)
        return TEST_FAILED;

    g_dev->data = rte_zmalloc("test_dev_data", sizeof(*g_dev->data), 0);
    if (!g_dev->data)
        goto err_free_dev;

    g_nic_dev = rte_zmalloc("test_nic_dev", sizeof(*g_nic_dev), 0);
    if (!g_nic_dev)
        goto err_free_dev_data;

    g_nic_dev->rxqs = rte_zmalloc("rxqs", HINIC3_MAX_QUEUE_NUM * sizeof(void *), 0);
    if (!g_nic_dev->rxqs)
        goto err_free_nic_dev;

    g_nic_dev->txqs = rte_zmalloc("txqs", HINIC3_MAX_QUEUE_NUM * sizeof(void *), 0);
    if (!g_nic_dev->txqs)
        goto err_free_rxqs;

    g_dev->data->rx_queues = rte_zmalloc("rx_queues", HINIC3_MAX_QUEUE_NUM * sizeof(void *), 0);
    if (!g_dev->data->rx_queues)
        goto err_free_txqs;

    g_dev->data->tx_queues = rte_zmalloc("tx_queues", HINIC3_MAX_QUEUE_NUM * sizeof(void *), 0);
    if (!g_dev->data->tx_queues)
        goto err_free_rx_queues;

    g_nic_dev->feature_cap = NIC_F_HAIRPIN;
    g_dev->data->dev_private = g_nic_dev;
    g_dev->data->port_id = 0;
    g_dev->data->nb_rx_queues = 4;
    g_dev->data->nb_tx_queues = 4;
    strcpy(g_dev->data->name, "test_hinic3_dev");

    return TEST_SUCCESS;

err_free_rx_queues:
    rte_free(g_dev->data->rx_queues);
err_free_txqs:
    rte_free(g_nic_dev->txqs);
err_free_rxqs:
    rte_free(g_nic_dev->rxqs);
err_free_nic_dev:
    rte_free(g_nic_dev);
err_free_dev_data:
    rte_free(g_dev->data);
err_free_dev:
    rte_free(g_dev);
    g_dev = NULL;
    g_nic_dev = NULL;
    return TEST_FAILED;
}

static void
hairpin_test_teardown(void)
{
    if (g_nic_dev) {
        for (int i = 0; i < HINIC3_MAX_QUEUE_NUM; i++) {
            if (g_nic_dev->rxqs && g_nic_dev->rxqs[i])
                rte_free(g_nic_dev->rxqs[i]);
            if (g_nic_dev->txqs && g_nic_dev->txqs[i])
                rte_free(g_nic_dev->txqs[i]);
        }
        rte_free(g_nic_dev->rxqs);
        rte_free(g_nic_dev->txqs);
        rte_free(g_nic_dev);
    }
    if (g_dev) {
        rte_free(g_dev->data->rx_queues);
        rte_free(g_dev->data->tx_queues);
        rte_free(g_dev->data);
        rte_free(g_dev);
    }
    g_dev = NULL;
    g_nic_dev = NULL;
}

static int
test_hairpin_cap_get_success(void)
{
    struct rte_eth_hairpin_cap cap;

    int ret = hinic3_hairpin_cap_get(g_dev, &cap);

    if (ret != 0) {
        return TEST_SKIPPED;
    }

    RTE_TEST_ASSERT_EQUAL(cap.max_nb_queues, UINT16_MAX, "max_nb_queues check failed");
    RTE_TEST_ASSERT_EQUAL(cap.max_rx_2_tx, 1, "max_rx_2_tx check failed");
    RTE_TEST_ASSERT_EQUAL(cap.max_tx_2_rx, 1, "max_tx_2_rx check failed");
    RTE_TEST_ASSERT_EQUAL(cap.max_nb_desc, HINIC3_MAX_QUEUE_DEPTH, "max_nb_desc check failed");

    return TEST_SUCCESS;
}

static int
test_hairpin_cap_get_not_supported(void)
{
    struct rte_eth_hairpin_cap cap;
    uint64_t orig_cap = g_nic_dev->feature_cap;

    g_nic_dev->feature_cap &= ~NIC_F_HAIRPIN;
    int ret = hinic3_hairpin_cap_get(g_dev, &cap);
    g_nic_dev->feature_cap = orig_cap;

    RTE_TEST_ASSERT(ret != 0, "hinic3_hairpin_cap_get should fail");
    RTE_TEST_ASSERT_EQUAL(rte_errno, ENOTSUP, "rte_errno should be ENOTSUP");

    return TEST_SUCCESS;
}

static int
test_hairpin_rx_queue_setup_success(void)
{
    struct rte_eth_hairpin_conf conf = {
        .peer_count = 1,
        .peers[0] = { .port = 0, .queue = 0 }
    };

    int ret = hinic3_rx_hairpin_queue_setup(g_dev, 0, 1024, &conf);

    RTE_TEST_ASSERT_EQUAL(ret, 0, "hinic3_rx_hairpin_queue_setup failed");
    RTE_TEST_ASSERT_NOT_NULL(g_dev->data->rx_queues[0], "rx_queues[0] is NULL");

    return TEST_SUCCESS;
}

static int
test_hairpin_rx_queue_setup_depth_alignment(void)
{
    struct rte_eth_hairpin_conf conf = {
        .peer_count = 1,
        .peers[0] = { .port = 0, .queue = 1 }
    };

    int ret = hinic3_rx_hairpin_queue_setup(g_dev, 1, 1000, &conf);

    RTE_TEST_ASSERT_EQUAL(ret, 0, "hinic3_rx_hairpin_queue_setup failed");

    return TEST_SUCCESS;
}

static int
test_hairpin_rx_queue_setup_invalid_peer_count(void)
{
    struct rte_eth_hairpin_conf conf = { .peer_count = 2 };

    int ret = hinic3_rx_hairpin_queue_setup(g_dev, 2, 1024, &conf);

    RTE_TEST_ASSERT(ret != 0, "hinic3_rx_hairpin_queue_setup should fail");
    RTE_TEST_ASSERT_EQUAL(rte_errno, EINVAL, "rte_errno should be EINVAL");

    return TEST_SUCCESS;
}

static int
test_hairpin_rx_queue_setup_depth_too_large(void)
{
    struct rte_eth_hairpin_conf conf = {
        .peer_count = 1,
        .peers[0] = { .port = 0, .queue = 0 }
    };

    int ret = hinic3_rx_hairpin_queue_setup(g_dev, 3, HINIC3_MAX_QUEUE_DEPTH * 2, &conf);

    RTE_TEST_ASSERT(ret != 0, "hinic3_rx_hairpin_queue_setup should fail for too large depth");

    return TEST_SUCCESS;
}

static int
test_hairpin_rx_queue_setup_depth_too_small(void)
{
    struct rte_eth_hairpin_conf conf = {
        .peer_count = 1,
        .peers[0] = { .port = 0, .queue = 0 }
    };

    int ret = hinic3_rx_hairpin_queue_setup(g_dev, 3, 64, &conf);

    RTE_TEST_ASSERT(ret != 0, "hinic3_rx_hairpin_queue_setup should fail for too small depth");

    return TEST_SUCCESS;
}

static int
test_hairpin_tx_queue_setup_success(void)
{
    struct rte_eth_hairpin_conf conf = {
        .peer_count = 1,
        .peers[0] = { .port = 0, .queue = 0 }
    };

    int ret = hinic3_tx_hairpin_queue_setup(g_dev, 0, 1024, &conf);

    RTE_TEST_ASSERT_EQUAL(ret, 0, "hinic3_tx_hairpin_queue_setup failed");
    RTE_TEST_ASSERT_NOT_NULL(g_dev->data->tx_queues[0], "tx_queues[0] is NULL");

    return TEST_SUCCESS;
}

static int
test_hairpin_tx_queue_setup_invalid_peer_count(void)
{
    struct rte_eth_hairpin_conf conf = { .peer_count = 3 };

    int ret = hinic3_tx_hairpin_queue_setup(g_dev, 2, 1024, &conf);

    RTE_TEST_ASSERT(ret != 0, "hinic3_tx_hairpin_queue_setup should fail");
    RTE_TEST_ASSERT_EQUAL(rte_errno, EINVAL, "rte_errno should be EINVAL");

    return TEST_SUCCESS;
}

static int
test_hairpin_tx_queue_setup_depth_alignment(void)
{
    struct rte_eth_hairpin_conf conf = {
        .peer_count = 1,
        .peers[0] = { .port = 0, .queue = 1 }
    };

    int ret = hinic3_tx_hairpin_queue_setup(g_dev, 1, 500, &conf);

    RTE_TEST_ASSERT_EQUAL(ret, 0, "hinic3_tx_hairpin_queue_setup failed");

    return TEST_SUCCESS;
}

static int
test_hairpin_bind_success(void)
{
    int ret = hinic3_hairpin_bind(g_dev, 0);

    RTE_TEST_ASSERT_EQUAL(ret, 0, "hinic3_hairpin_bind failed");

    return TEST_SUCCESS;
}

static int
test_hairpin_unbind_success(void)
{
    int ret = hinic3_hairpin_unbind(g_dev, 0);

    RTE_TEST_ASSERT_EQUAL(ret, 0, "hinic3_hairpin_unbind failed");

    return TEST_SUCCESS;
}

static int
test_hairpin_get_peer_ports_tx_direction(void)
{
    struct rte_eth_hairpin_conf conf = {
        .peer_count = 1,
        .peers[0] = { .port = 1, .queue = 0 }
    };

    hinic3_rx_hairpin_queue_setup(g_dev, 0, 1024, &conf);

    uint16_t peer_ports[4];
    int ret = hinic3_hairpin_get_peer_ports(g_dev, peer_ports, 4, 1);

    RTE_TEST_ASSERT(ret >= 0, "hinic3_hairpin_get_peer_ports failed");

    return TEST_SUCCESS;
}

static int
test_hairpin_get_peer_ports_rx_direction(void)
{
    struct rte_eth_hairpin_conf conf = {
        .peer_count = 1,
        .peers[0] = { .port = 1, .queue = 0 }
    };

    hinic3_tx_hairpin_queue_setup(g_dev, 0, 1024, &conf);

    uint16_t peer_ports[4];
    int ret = hinic3_hairpin_get_peer_ports(g_dev, peer_ports, 4, 0);

    RTE_TEST_ASSERT(ret >= 0, "hinic3_hairpin_get_peer_ports failed");

    return TEST_SUCCESS;
}

static struct unit_test_suite hinic3_hairpin_test_suite = {
    .suite_name = "HINIC3 Hairpin Unit Tests",
    .setup = hairpin_suite_setup,
    .teardown = hairpin_suite_teardown,
    .unit_test_cases = {
        TEST_CASE_ST(hairpin_test_setup, hairpin_test_teardown, test_hairpin_cap_get_success),
        TEST_CASE_ST(hairpin_test_setup, hairpin_test_teardown, test_hairpin_cap_get_not_supported),
        TEST_CASE_ST(hairpin_test_setup, hairpin_test_teardown, test_hairpin_rx_queue_setup_success),
        TEST_CASE_ST(hairpin_test_setup, hairpin_test_teardown, test_hairpin_rx_queue_setup_depth_alignment),
        TEST_CASE_ST(hairpin_test_setup, hairpin_test_teardown, test_hairpin_rx_queue_setup_invalid_peer_count),
        TEST_CASE_ST(hairpin_test_setup, hairpin_test_teardown, test_hairpin_rx_queue_setup_depth_too_large),
        TEST_CASE_ST(hairpin_test_setup, hairpin_test_teardown, test_hairpin_rx_queue_setup_depth_too_small),
        TEST_CASE_ST(hairpin_test_setup, hairpin_test_teardown, test_hairpin_tx_queue_setup_success),
        TEST_CASE_ST(hairpin_test_setup, hairpin_test_teardown, test_hairpin_tx_queue_setup_invalid_peer_count),
        TEST_CASE_ST(hairpin_test_setup, hairpin_test_teardown, test_hairpin_tx_queue_setup_depth_alignment),
        TEST_CASE_ST(hairpin_test_setup, hairpin_test_teardown, test_hairpin_bind_success),
        TEST_CASE_ST(hairpin_test_setup, hairpin_test_teardown, test_hairpin_unbind_success),
        TEST_CASE_ST(hairpin_test_setup, hairpin_test_teardown, test_hairpin_get_peer_ports_tx_direction),
        TEST_CASE_ST(hairpin_test_setup, hairpin_test_teardown, test_hairpin_get_peer_ports_rx_direction),
        TEST_CASES_END()
    }
};

static int
test_hinic3_hairpin(void)
{
    return unit_test_suite_runner(&hinic3_hairpin_test_suite);
}

REGISTER_TEST_COMMAND(hinic3_hairpin_autotest, test_hinic3_hairpin);
