# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2025 Huawei Technologies Co., Ltd

sources = files(
	'src/hinic3_capture/hinic3_capture_save.c',
	'src/hinic3_capture/hinic3_capture_hooks.c',
	'src/hinic3_capture/hinic3_capture_utils.c',
	'src/hinic3_capture/hinic3_capture_filter.c',
	'src/hinic3_capture/hinic3_capture_main.c',
	'src/hinic3_capture/hinic3_capture_command.c',
	'src/hinic3_capture/hinic3_capture_core.c',
	'src/hinic3_ports/comm/hinic3_port_util.c',
	'src/hinic3_ports/comm/hinic3_eth_util.c',
	'src/hinic3_ports/hinic3_queue/hinic3_queue.c',
	'src/hinic3_ports/hinic3_queue/hinic3_standard_queue.c',
	'src/hinic3_ports/hinic3_queue/hinic3_virtual_queue.c',
	'src/hinic3_ports/hinic3_vf/hinic3_vf_mgmt_baremetal.c',
	'src/hinic3_ports/hinic3_vf/hinic3_vf_controller.c',
	'src/hinic3_ports/hinic3_vf/hinic3_vf_statistics.c',
	'src/hinic3_ports/hinic3_vf/hinic3_vf_mgmt_virtual.c',
	'src/hinic3_ports/hinic3_vf/hinic3_vf_mgmt.c',
	'src/hinic3_ports/hinic3_vdev/hinic3_vdev.c',
	'src/hinic3_ports/hinic3_bond/hinic3_bond_controller.c',
	'src/hinic3_ports/hinic3_bond/hinic3_bond_detect.c',
	'src/hinic3_ports/hinic3_bond/hinic3_bond_statistics.c',
	'src/hinic3_qos/multilevel_qos/hinic3_mtr_profile.c',
	'src/hinic3_qos/multilevel_qos/hinic3_mtr_policy.c',
	'src/hinic3_qos/multilevel_qos/hinic3_mtr.c',
	'src/hinic3_qos/comm/hinic3_vf_port_qos_public.c',
	'src/hinic3_qos/meter/hinic3_qos.c',
	'src/hinic3_qos/flow_qos/hinic3_flow_qos.c',
	'src/hinic3_ufid_map/hinic3_ufid_hmap.c',
	'src/hinic3_ufid_map/hinic3_mpool_rte_flow.c',
	'src/hinic3_ufid_map/hinic3_ufid_map_rte_flow.c',
	'src/hinic3_init/hinic3_init.c',
	'src/hinic3_init/hinic3_init_arg.c',
	'src/hinic3_init/hinic3_agent.c',
	'src/hinic3_init/hinic3_flow_agent.c',
	'src/hinic3_init/hinic3_parse_agent_config.c',
	'src/hinic3_driver_adapter/hinic3_driver_public.c',
	'src/hinic3_driver_adapter/hinic3_iface_flow_api_record.c',
	'src/hinic3_driver_adapter/hinic3_iface_flow.c',
	'src/hinic3_driver_adapter/hinic3_iface_port.c',
	'src/hinic3_driver_adapter/hinic3_iface_global.c',
	'src/hinic3_driver_adapter/hinic3_iface_port_util.c',
	'src/hinic3_driver_adapter/hinic3_iface_global_api_record.c',
	'src/hinic3_driver_adapter/hinic3_iface_port_api_record.c',
	'src/hinic3_cmds/comm/hinic3_agent_cmd_format.c',
	'src/hinic3_cmds/comm/hinic3_agent_cmd_format_flexda.c',
	'src/hinic3_cmds/comm/hinic3_cmd_forward.c',
	'src/hinic3_cmds/comm/hinic3_dfx_port_bond_slave.c',
	'src/hinic3_cmds/comm/hinic3_dfx_port.c',
	'src/hinic3_cmds/comm/hinic3_agent_cmd.c',
	'src/hinic3_cmds/comm/hinic3_cmd_exec.c',
	'src/hinic3_cmds/comm/hinic3_dfx_port_upcall_queue.c',
	'src/hinic3_cmds/comm/hinic3_agent_flow_cmd_dump_file.c',
	'src/hinic3_cmds/comm/hinic3_dfx_multi_qos.c',
	'src/hinic3_cmds/comm/hinic3_agent_flow_cmd.c',
	'src/hinic3_cmds/comm/hinic3_agent_flow_cmd_api.c',
	'src/hinic3_cmds/comm/hinic3_trace_flow.c',
	'src/hinic3_cmds/comm/hinic3_cmd_meminfo.c',
	'src/hinic3_cmds/comm/hinic3_agent_flow_cmd_dump.c',
	'src/hinic3_cmds/comm/hinic3_agent_cmd_time.c',
	'src/hinic3_cmds/comm/hinic3_cmd_error_stats.c',
	'src/hinic3_cmds/comm/hinic3_dfx_port_flush_dump.c',
	'src/hinic3_cmds/comm/hinic3_agent_flow_cmd_speed.c',
	'src/hinic3_cmds/server/hinic3_command.c',
	'src/common/hinic3_log/hinic3_log.c',
	'src/common/hinic3_hash/hinic3_hash.c',
	'src/common/hinic3_mutex/hinic3_mutex.c',
	'src/common/hinic3_hmap/hinic3_hmap.c',
	'src/common/hinic3_ds/hinic3_ds.c',
	'src/common/hinic3_uuid/hinic3_uuid.c',
	'src/common/hinic3_string_util/hinic3_string_format.c',
	'src/common/hinic3_string_util/hinic3_string_util.c',
	'src/common/hinic3_packets/hinic3_packets.c',
	'src/common/hinic3_mpool_mgmt/hinic3_mpool.c',
	'src/common/hinic3_utils/hinic3_meminfo.c',
	'src/common/hinic3_utils/hinic3_check_thread_health_state.c',
	'src/common/hinic3_utils/hinic3_set_userdata.c',
	'src/common/hinic3_utils/hinic3_timeval.c',
	'src/common/hinic3_utils/hinic3_file_util.c',
	'src/common/hinic3_utils/hinic3_flow_agent_public.c',
	'src/common/hinic3_utils/hinic3_hugepage_meminfo.c',
	'src/common/hinic3_utils/hinic3_error_stats.c',
	'src/common/hinic3_utils/hinic3_util.c',
	'src/common/hinic3_thread/hinic3_thread.c',
	'src/common/hinic3_smap/hinic3_shash.c',
	'src/common/hinic3_smap/hinic3_smap.c',
	'src/hinic3_flows/comm/hinic3_offload_action_public.c',
	'src/hinic3_flows/comm/hinic3_age_delete_flow_public.c',
	'src/hinic3_flows/comm/hinic3_flow_dump_public.c',
	'src/hinic3_flows/comm/hinic3_offload_flow_public.c',
	'src/hinic3_flows/comm/hinic3_offload_flow_port.c',
	'src/hinic3_flows/hinic3_flow_mirror/hinic3_flow_session.c',
	'src/hinic3_flows/hinic3_flow_mirror/hinic3_flow_mirror.c',
	'src/hinic3_flows/hinic3_flow_offload/hinic3_offload_flow.c',
	'src/hinic3_flows/hinic3_flow_offload/hinic3_dump_flow.c',
	'src/hinic3_flows/hinic3_flow_offload/hinic3_flow_log_origin.c',
	'src/hinic3_flows/hinic3_flow_offload/hinic3_offload_action.c',
	'src/hinic3_flows/hinic3_flow_dump/hinic3_flow_api.c',
	'src/hinic3_flows/hinic3_flow_dump/hinic3_flow_dump.c',
	'src/hinic3_flows/hinic3_flow_delete/hinic3_age_delete_flow.c',
    'src/hinic3_flows/hinic3_flow_delete/hinic3_ufid_del_flow.c',
	'src/hinic3_flows/hinic3_flow_format/hinic3_flow_format.c',
	'src/hinic3_flows/hinic3_flow_format/hinic3_rte_flow_format.c',
	'src/hinic3_flows/hinic3_emc_dump/hinic3_flow_dump_action.c',
	'src/hinic3_flows/hinic3_emc_dump/hinic3_flow_dump_item_key.c',
	'src/hinic3_flows/hinic3_emc_dump/hinic3_flow_emc_dump.c',
	'src/hinic3_flows/hinic3_emc_dump/hinic3_flow_dump_item.c',
	'src/hinic3_flows/hinic3_vxlan/hinic3_vxlan_dump.c',
	'src/hinic3_flows/hinic3_vxlan/hinic3_vxlan.c',
	'src/hinic3_flows/hinic3_flow_query/hinic3_flow_agent_sync_stats.c',
	'src/hinic3_flows/hinic3_flow_query/hinic3_query_flow.c',
	'src/hinic3_flows/hinic3_flow_query/hinic3_key_query.c',
	'src/hinic3_flows/hinic3_mega_offload/hinic3_mega_offload.c',
	'src/hinic3_flows/hinic3_mega_offload/hinic3_mega_key.c',
	'src/hinic3_flows/hinic3_mega_offload/hinic3_mega_item.c',
	'src/hinic3_flows/hinic3_mega_dump/hinic3_mega_dump.c',
	'src/hinic3_flows/hinic3_mega_dump/hinic3_mega_dump_item.c',
	'src/hinic3_flows/hinic3_mega_dump/hinic3_mega_dump_format.c',
	'src/hinic3_flows/hinic3_flexda/hinic3_flexda_flow_public.c',
)

includes += include_directories(
    '../../../lib/distributor/',
    '../../../build',
    '../../../config',
	'include',
    'src/',
    'src/hinic3_capture',
    'src/hinic3_ports',
    'src/hinic3_ports/comm',
    'src/hinic3_ports/hinic3_queue',
    'src/hinic3_ports/hinic3_vf',
    'src/hinic3_ports/hinic3_vdev',
    'src/hinic3_ports/hinic3_bond',
    'src/hinic3_qos',
    'src/hinic3_qos/multilevel_qos',
    'src/hinic3_qos/comm',
	'src/hinic3_qos/flow_qos',
    'src/hinic3_qos/meter',
    'src/hinic3_ufid_map',
    'src/hinic3_init',
    'src/hinic3_driver_adapter',
    'src/hinic3_driver_adapter/hiovs',
    'src/hinic3_cmds',
    'src/hinic3_cmds/client',
    'src/hinic3_cmds/comm',
    'src/hinic3_cmds/server',
    'src/common',
    'src/common/hinic3_log',
    'src/common/hinic3_hash',
    'src/common/hinic3_mutex',
    'src/common/hinic3_hmap',
    'src/common/hinic3_ds',
    'src/common/hinic3_uuid',
    'src/common/hinic3_string_util',
    'src/common/hinic3_packets',
    'src/common/hinic3_mpool_mgmt',
    'src/common/hinic3_utils',
    'src/common/hinic3_thread',
    'src/common/hinic3_smap',
    'src/hinic3_flows',
    'src/hinic3_flows/comm',
    'src/hinic3_flows/hinic3_flow_mirror',
    'src/hinic3_flows/hinic3_flow_offload',
    'src/hinic3_flows/hinic3_flow_dump',
    'src/hinic3_flows/hinic3_flow_delete',
    'src/hinic3_flows/hinic3_flow_format',
    'src/hinic3_flows/hinic3_emc_dump',
    'src/hinic3_flows/hinic3_vxlan',
    'src/hinic3_flows/hinic3_flow_query',
	'src/hinic3_flows/hinic3_mega_offload',
	'src/hinic3_flows/hinic3_mega_dump',
    'src/hinic3_flows/hinic3_flexda',
)
deps += ['cfgfile', 'eal', 'kvargs', 'mempool', 'pci', 'bus_pci', 'bus_vdev', 'ring', 'mbuf', 'timer', 'ethdev']

# eal is standard dependency once built
if dpdk_conf.has('RTE_LIB_EAL')
    deps += ['eal']
endif

shared_deps = []
static_deps = []
foreach d:deps
    if not is_variable('shared_rte_' + d)
        error('Missing internal dependency "@0@" for @name@ [@2@]'
                .format(d, name, 'lib/' + name))
    endif
    shared_deps += [get_variable('shared_rte_' + d)]
    static_deps += [get_variable('static_rte_' + d)]
endforeach

executable('dpak-ovs-ctl',
    'src/hinic3_cmds/client/hinic3_command_client.c',
	include_directories: includes,
    install_rpath: '/usr/bin',
    install: true
)

install_headers('include/rte_pmd_hinic3.h')
install_headers('src/hinic3_driver_adapter/hiovs/hiovs_api.h')
install_headers('src/hinic3_init/hinic3_init_arg.h')
install_headers('src/hinic3_init/hinic3_agent.h')
install_headers('src/hinic3_init/hinic3_flow_agent.h')
install_headers('src/hinic3_init/hinic3_init.h')
install_headers('src/hinic3_init/hinic3_parse_agent_config.h')
install_headers('src/common/hinic3_utils/hinic3_drv.h')
install_headers('src/common/hinic3_utils/hinic3_packets_types.h')
install_headers('src/common/hinic3_utils/hinic3_eth_packets.h')
install_headers('src/common/hinic3_utils/hinic3_types.h')

install_headers('src/common/hinic3_utils/hinic3_mutex.h')
install_headers('src/common/hinic3_log/hinic3_log.h')
install_headers('src/common/hinic3_utils/hinic3_flow_agent_enum.h')
install_headers('src/common/hinic3_utils/hinic3_check_thread_health_state.h')
install_headers('src/common/hinic3_utils/hinic3_timeval.h')


install_headers('src/common/hinic3_utils/hinic3_meminfo.h')
install_headers('src/common/hinic3_utils/hinic3_hugepage_meminfo.h')
install_headers('src/common/hinic3_utils/hinic3_list.h')
install_headers('src/common/hinic3_utils/hinic3_util.h')
install_headers('src/common/hinic3_uuid/hinic3_uuid.h')
install_headers('src/common/hinic3_string_util/hinic3_string_util.h')
install_headers('src/common/hinic3_string_util/hinic3_string_format.h')
install_headers('src/common/hinic3_packets/hinic3_packets.h')
install_headers('src/common/hinic3_uuid/hinic3_uuid.h')

install_headers('src/common/hinic3_ds/hinic3_ds.h')
install_headers('src/common/hinic3_smap/hinic3_shash.h')
install_headers('src/common/hinic3_utils/hinic3_ui_string.h')
install_headers('src/common/hinic3_utils/hinic3_file_util.h')
install_headers('src/common/hinic3_utils/hinic3_unaligned.h')

# driver
install_headers('src/hinic3_driver_adapter/hinic3_iface_port.h')
install_headers('src/hinic3_driver_adapter/hinic3_driver_public.h')
install_headers('src/hinic3_driver_adapter/hinic3_iface_flow.h')
install_headers('src/hinic3_driver_adapter/hinic3_iface_global.h')
install_headers('src/hinic3_driver_adapter/hinic3_iface_flow_api_record.h')
install_headers('src/hinic3_driver_adapter/hinic3_iface_global_api_record.h')
install_headers('src/hinic3_driver_adapter/hinic3_iface_port_api_record.h')
install_headers('src/hinic3_driver_adapter/hiovs/hiovs_acl_api.h')

# port
install_headers('src/hinic3_ports/hinic3_vf/hinic3_vf_mgmt.h')
install_headers('src/common/hinic3_utils/hinic3_map.h')
install_headers('src/common/hinic3_smap/hinic3_smap.h')
install_headers('src/common/hinic3_hmap/hinic3_hmap.h')
install_headers('src/common/hinic3_utils/hinic3_provider.h')
install_headers('src/common/hinic3_utils/hinic3_message.h')
install_headers('src/common/hinic3_utils/hinic3_message_sub.h')
install_headers('src/common/hinic3_utils/hinic3_flow_agent_public.h')
install_headers('src/hinic3_flows/comm/hinic3_packet_key_public.h')
install_headers('src/common/hinic3_thread/hinic3_thread.h')
install_headers('src/hinic3_cmds/comm/hinic3_trace_flow.h')
install_headers('src/common/hinic3_utils/hinic3_nlattr.h')
install_headers('src/common/hinic3_utils/hinic3_set_userdata.h')

#capture
install_headers('src/hinic3_capture/hinic3_capture_command.h')
install_headers('src/hinic3_capture/hinic3_capture_save.h')
install_headers('src/hinic3_capture/hinic3_capture_hooks.h')
install_headers('src/hinic3_capture/hinic3_capture_utils.h')
install_headers('src/hinic3_capture/hinic3_capture_filter.h')
install_headers('src/hinic3_capture/hinic3_capture_core.h')
install_headers('src/hinic3_capture/hinic3_capture_main.h')

# commands
install_headers('src/hinic3_cmds/server/hinic3_command.h')
install_headers('src/hinic3_cmds/comm/hinic3_agent_cmd.h')
# components
install_headers('src/common/hinic3_utils/hinic3_error_stats.h')
install_headers('src/hinic3_flows/hinic3_flow_mirror/hinic3_flow_session.h')
install_headers('src/hinic3_flows/hinic3_flow_dump/hinic3_flow_dump.h')
install_headers('src/hinic3_capture/hinic3_capture_main.h')
install_headers('src/hinic3_ports/hinic3_queue/hinic3_queue.h')
install_headers('src/hinic3_flows/comm/hinic3_offload_flow_port.h')
install_headers('src/hinic3_ufid_map/hinic3_mpool_rte_flow.h')
install_headers('src/hinic3_ufid_map/hinic3_ufid_hmap.h')
install_headers('src/common/hinic3_mpool_mgmt/hinic3_mpool.h')
install_headers('src/hinic3_flows/comm/hinic3_offload_flow_public.h')
install_headers('src/hinic3_ports/comm/hinic3_port_util.h')
install_headers('src/common/hinic3_utils/hinic3_tlv_key.h')
install_headers('src/hinic3_ports/comm/hinic3_eth_util.h')
install_headers('src/hinic3_ports/comm/hinic3_port_util.h')
install_headers('src/hinic3_ports/hinic3_vdev/hinic3_vdev.h')
install_headers('src/hinic3_ports/hinic3_bond/hinic3_bond_controller.h')
install_headers('src/hinic3_ports/hinic3_bond/hinic3_bond_detect.h')
install_headers('src/hinic3_ports/hinic3_bond/hinic3_bond_statistics.h')
install_headers('src/hinic3_ports/hinic3_vf/hinic3_vf_controller.h')
install_headers('src/hinic3_ports/hinic3_vf/hinic3_vf_mgmt_baremetal.h')
install_headers('src/hinic3_ports/hinic3_vf/hinic3_vf_mgmt_virtual.h')
install_headers('src/hinic3_ports/hinic3_vf/hinic3_vf_statistics.h')
install_headers('src/hinic3_ports/hinic3_queue/hinic3_queue.h')
install_headers('src/hinic3_ports/hinic3_queue/hinic3_standard_queue.h')
install_headers('src/hinic3_ports/hinic3_queue/hinic3_virtual_queue.h')
install_headers('src/common/hinic3_hash/hinic3_hash.h')
install_headers('src/hinic3_flows/hinic3_flow_delete/hinic3_age_delete_flow.h')
install_headers('src/hinic3_flows/comm/hinic3_age_delete_flow_public.h')
install_headers('src/hinic3_flows/comm/hinic3_flow_agent_sync_stats_public.h')
install_headers('src/hinic3_flows/hinic3_flow_query/hinic3_query_flow.h')
install_headers('src/hinic3_flows/hinic3_flow_offload/hinic3_dump_flow.h')
install_headers('src/hinic3_flows/hinic3_flow_offload/hinic3_offload_flow.h')
install_headers('src/hinic3_flows/comm/hinic3_offload_action_public.h')
install_headers('src/hinic3_cmds/comm/hinic3_agent_cmd_format.h')
install_headers('src/hinic3_cmds/comm/hinic3_agent_cmd_format_flexda.h')
install_headers('src/hinic3_cmds/comm/hinic3_dfx_port.h')
install_headers('src/hinic3_cmds/comm/hinic3_option.h')
install_headers('src/hinic3_flows/comm/hinic3_flow_dump_public.h')
install_headers('src/hinic3_flows/hinic3_mega_offload/hinic3_mega_key.h')
install_headers('src/hinic3_flows/hinic3_mega_offload/hinic3_mega_offload.h')
install_headers('src/hinic3_flows/hinic3_vxlan/hinic3_vxlan.h')
install_headers('src/hinic3_flows/hinic3_vxlan/hinic3_vxlan_dump.h')
install_headers('src/hinic3_ufid_map/hinic3_ufid_map_rte_flow.h')
install_headers('src/hinic3_flows/hinic3_flow_offload/hinic3_offload_action.h')
install_headers('src/hinic3_driver_adapter/hiovs/hiovs_flexda_api.h')
install_headers('src/hinic3_driver_adapter/hiovs/hiovs_flexda_api_extra.h')

#ovs_flow
install_headers('src/hinic3_flows/hinic3_flow_format/hinic3_flow_format.h')
install_headers('src/hinic3_flows/hinic3_flow_format/hinic3_rte_flow_format.h')
install_headers('src/hinic3_flows/hinic3_emc_dump/hinic3_flow_dump_action.h')
install_headers('src/hinic3_flows/hinic3_emc_dump/hinic3_flow_dump_item.h')
install_headers('src/hinic3_flows/hinic3_emc_dump/hinic3_flow_dump_item_key.h')

#qos
install_headers('src/hinic3_qos/meter/hinic3_qos.h')
install_headers('src/hinic3_qos/multilevel_qos/hinic3_mtr.h')
install_headers('src/hinic3_qos/multilevel_qos/hinic3_mtr_policy.h')
install_headers('src/hinic3_qos/multilevel_qos/hinic3_mtr_profile.h')
install_headers('src/hinic3_qos/comm/hinic3_vf_port_qos_public.h')
install_headers('src/hinic3_qos/flow_qos/hinic3_flow_qos.h')

install_headers('src/hinic3_flows/hinic3_flexda/hinic3_flexda_flow_public.h')

hinic3_args = get_option('c_args')
if hinic3_args.contains('-DHINIC3_METAL')
	install_data('cfg/ovs_dpu_metal/agent_config.ini', install_dir: join_paths(get_option('prefix'),'etc','dpak','net'))
else
	install_data('cfg/ovs_dpu_virt/agent_config.ini', install_dir: join_paths(get_option('prefix'),'etc','dpak','net'))
endif

install_data('scripts/dpak_ovs_completion.sh', install_dir: join_paths(get_option('prefix'),'etc','bash_completion.d'))
install_data('scripts/network_info_collect.sh', install_dir: join_paths(get_option('prefix'),'bin'))
