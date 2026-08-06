#!/bin/bash

# 版权所有 (c) 华为技术有限公司 2024-2024
# 生成日期 : 2024年7月25日
# 功能描述 : dpak-ovs 网络卸载工具命令自动补全功能实现

config_file="/etc/dpak/net/agent_config.ini"

TOP_LEVEL_OPTS=(
    hwoff/show-forward-mode
    hwoff/set-forward-mode
    hwoff/show-global-api
    hwoff/dump-hwoff-flows
    hwoff/dump-upcall-queue-info
    hwoff/dump-bond-slave-info
    hwoff/show-port-api
    hwoff/dump-fuzzy-flows
    hwoff/show-log-list
    hwoff/show-security-src-mac-info
    hwoff/set-log-level
    hwoff/log-limit-ctl
    hwoff/show-port-qos
    hwoff/show-offload-flow-num
    hwoff/dump-ports
    hwoff/dump-ports-queue-info
    hwoff/show-mpool-stats
    hwoff/dump-trace
    hwoff/dump-ufid-map-hw
    hwoff/show-function-flavor
    hwoff/show-meminfo
    hwoff/show-hugepage-meminfo
    hwoff/dump-qos
    hwoff/enable-capture-probe
    hwoff/flush-ports
    hwoff/show-port-security-filter
    hwoff/disable-capture-probe
    hwoff/show-qos-speed
    list-commands
    hwoff/capture-probe
    hwoff/show-error-stats
    hwoff/show-function-stats
    hwoff/trace-flow
    hwoff/show-thread-stats
    hwoff/flow-escape-mode
    hwoff/packet-detect-mode
    hwoff/dump-ufid-map-sw
    hwoff/dump-qos-loss
    hwoff/ufid-map-error-stats
    hwoff/exec-cmd
    hwoff/flow-offload-speed-stat
    hwoff/show-flow-api
    hwoff/show-hmap-flow-num
    hwoff/show-security-eth-type-info
    hwoff/query-offloaded-flow
    hwoff/del-flow-by-ufid
    hwoff/dump-meter
    hwoff/dump-flow-qos-loss
    -h
)

RUN_TIME_MODE_OPTS=(
    hwoff/dump-port-qos-loss
    hwoff/show-meter
    hwoff/show-policy
    hwoff/show-profile
    hwoff/show-port-qos-speed
    hwoff/dump-net-qos
    hwoff/dump-net-qos-loss
    hwoff/dump-qos-stats
    hwoff/dump-port-qos-stats
    hwoff/dump-net-qos-stats
    hwoff/show-sample-session
    hwoff/show-flow-qos
    hwoff/dump-flow-qos
    hwoff/dump-flow-qos-stats
    hwoff/dump-flow-qos-loss
)

_get_top_level_options() {
    local cur opts
    cur="${COMP_WORDS[COMP_CWORD]}"
    COMPREPLY=( $(compgen -W "${TOP_LEVEL_OPTS[*]}" -- ${cur}) )
}

_get_run_time_mode_options() {
    local cur opts
    cur="${COMP_WORDS[COMP_CWORD]}"
    COMPREPLY=( $(compgen -W "${TOP_LEVEL_OPTS[*]} ${RUN_TIME_MODE_OPTS[*]}" -- ${cur}) )
}

_complete_show_global_api() {
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    prev2="${COMP_WORDS[COMP_CWORD-2]}"

    local api_opts=(
            hovs_global_cfg_set
            hovs_global_cfg_get
            # hovs_global_device_feature_get
            hovs_global_statistics_get
            hovs_global_statistics_flush
            hovs_open_log
            hovs_set_log_level
            hovs_mml_lib
            hovs_global_pcie_list_query
        )
    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "api clear all -h" -- ${cur}) )
    elif [[ ${COMP_CWORD} -eq 3 && ${prev} == "api" ]]; then
        COMPREPLY=( $(compgen -W "${api_opts[*]}" -- ${cur}) )
    elif [[ ${COMP_CWORD} -eq 4 && ${prev2} == "api" && " ${api_opts[*]} " == *" ${prev} "* ]]; then
        COMPREPLY=( $(compgen -W "clear" -- ${cur}) )
    fi
}

_complete_dump_hinic3_flows() {
    local cur
    cur="${COMP_WORDS[COMP_CWORD]}"
    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "ufid check stop -h -f -n -b" -- ${cur}) )
    fi
}

_complete_dump_upcall_queue_info() {
        local cur
        cur="${COMP_WORDS[COMP_CWORD]}"
        if [[ ${COMP_CWORD} -eq 2 ]]; then
            COMPREPLY=( $(compgen -W "-virtual -physical -port -all -h --help" -- ${cur}) )
        fi
}

_complete_show_port_api() {
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    prev2="${COMP_WORDS[COMP_CWORD-2]}"

    local api_opts=(
        hovs_port_mgmt_add
        hovs_port_mgmt_del
        hovs_port_mgmt_get
        hovs_port_mgmt_set
        hovs_port_mgmt_setup_upcall_queue
        hovs_port_mgmt_release_upcall_queue
        hovs_bond_mgmt_create
        hovs_bond_mgmt_delete
        hovs_bond_mgmt_get
        hovs_bond_mgmt_set
        hovs_bond_mgmt_clear
        hovs_bond_slave_info_get
        hovs_etheraddr_get
        hovs_mtu_set
        hovs_carrier_get
        hovs_carrier_resets_get
        hovs_port_statistics_get
        hovs_port_statistics_flush
        hovs_features_get
        hovs_bum_get
        hovs_bum_set
        hovs_bum_remove
        hovs_bum_clear
        hovs_qos_ingress_limit_set
        hovs_qos_ingress_limit_get
        hovs_qos_egress_limit_set
        hovs_qos_egress_limit_get
        hovs_qos_drop_thresh_set
        hovs_qos_drop_thresh_get
        hovs_port_mgmt_get_capability
        hovs_port_mgmt_get_upcall_info
        hovs_qos_statistics_get_batch
        hovs_qos_statistics_get_all_batch
        hovs_qos_statistics_clear_batch
        hovs_hqos_statistics_get_all_batch
        hovs_hqos_statistics_clear_batch
        hovs_port_mgmt_add_dynamic
        hovs_qos_vm_limit_set
        hovs_qos_vm_limit_get
        hovs_qos_flow_limit_set
        hovs_qos_flow_limit_get
        hovs_qos_vport_limit_set
        hovs_qos_vport_limit_get
        hovs_qos_vm_srtcm_limit_set
        hovs_qos_vm_srtcm_limit_get
        hovs_qos_net_limit_set
        hovs_qos_net_limit_get
        hovs_bond_slave_statistics_get
        hovs_port_mgmt_set_usage_state
        hovs_port_mgmt_get_usage_state
        hovs_upcall_mtu_set
        hovs_hotplug_add
        hovs_hotplug_del
    )

    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "api clear all -h" -- ${cur}) )
    elif [[ ${COMP_CWORD} -eq 3 && ${prev} == "api" ]]; then
        COMPREPLY=( $(compgen -W "${api_opts[*]}" -- ${cur}) )
    elif [[ ${COMP_CWORD} -eq 4 && ${prev2} == "api" && " ${api_opts[*]} " == *" ${prev} "* ]]; then
        COMPREPLY=( $(compgen -W "clear" -- ${cur}) )
    fi
}

_complete_set_log_level() {
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    local module_name=(
        hwoff-agent
        hwoff-vport
        hwoff-bond
        hwoff-flow
        hwoff-bum
        hwoff-qos
        hwoff-packet
        hwoff-driver-vport
        hwoff-driver-bond
        hwoff-driver-flow
        hwoff-driver-bum
        hwoff-driver-qos
        hwoff-driver-chip
        hwoff-driver-packet
        hwoff-offload-policy
    )

    local level=(
        disabled
        emergency
        alert
        critical
        error
        warning
        notice
        info
        debug
    )

    local duration_suggestions=(
        infinite
        1 5 10 15 30 60 120 240 480 720 1440 7200 10080
    )

    # 检查是否已经包含 -h 或 --help
    for word in "${COMP_WORDS[@]}"; do
        if [[ "$word" == "-h" || "$word" == "--help" ]]; then
            COMPREPLY=()
            return
        fi
    done
    # 第2个参数：显示所有选项
    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "-m -l -t -h --help" -- ${cur}) )
    # 处理参数值补全
    elif [[ ${prev} == "-m" ]]; then
        COMPREPLY=( $(compgen -W "${module_name[*]}" -- ${cur}) )
        compopt -o nosort
    elif [[ ${prev} == "-l" ]]; then
        COMPREPLY=( $(compgen -W "${level[*]}" -- ${cur}) )
        compopt -o nosort
    elif [[ ${prev} == "-t" ]]; then
        # 智能数字补全
        if [[ -z "${cur}" ]]; then
            # 空输入时显示常用时间值
            COMPREPLY=( $(compgen -W "infinite 1 5 10 15 30 60 120 240 480 720 1440 7200 10080" -- "${cur}") )
            compopt -o nosort
        else
            local matched_numbers=()
            # 检查是否匹配 "infinite"
            if [[ "infinite" =~ ^${cur} ]]; then
                matched_numbers+=("infinite")
            fi
            # 生成匹配的数字
            for ((i=1; i<=10080; i++)); do
                if [[ "$i" =~ ^${cur} ]]; then
                    matched_numbers+=("$i")
                    # 限制最多显示10个结果
                    if [[ ${#matched_numbers[@]} -ge 10 ]]; then
                        break
                    fi
                fi
            done
            COMPREPLY=( "${matched_numbers[@]}" )
        fi
    # 处理参数名补全（检查哪些参数还没使用）
    else
        local used_flags=()
        local available_flags=()
        # 收集已使用的标志
        for ((i=1; i<COMP_CWORD; i++)); do
            if [[ "${COMP_WORDS[i]}" == "-m" || "${COMP_WORDS[i]}" == "-l" || "${COMP_WORDS[i]}" == "-t" ]]; then
                used_flags+=("${COMP_WORDS[i]}")
            fi
        done
        # 确定可用的标志
        if [[ ! " ${used_flags[@]} " =~ " -m " ]]; then
            available_flags+=("-m")
        fi
        if [[ ! " ${used_flags[@]} " =~ " -l " ]]; then
            available_flags+=("-l")
        fi
        if [[ ! " ${used_flags[@]} " =~ " -t " ]]; then
            available_flags+=("-t")
        fi
        # 如果所有必需参数都已提供，则不再补全
        if [[ ${#available_flags[@]} -eq 0 ]]; then
            COMPREPLY=()
        else
            COMPREPLY=( $(compgen -W "${available_flags[*]}" -- ${cur}) )
        fi
    fi
}

_complete_log_limit_ctl(){
    local cur matched_numbers
    cur="${COMP_WORDS[COMP_CWORD]}"
    matched_numbers=()
 
    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "--disable --enable --show -h --help" -- ${cur}) )
    elif [[ ${COMP_CWORD} -eq 3 && "${COMP_WORDS[2]}" == "--disable" ]]; then
        # 智能数字补全
        if [[ -z "${cur}" ]]; then
            # 空输入时显示常用时间值（分钟）
            COMPREPLY=( $(compgen -W "1 5 10 15 30 60 120 240 480 720 1440 7200 10080" -- "${cur}") )
            compopt -o nosort
        else
            # 有输入时生成匹配的数字
            for ((i=1; i<=10080; i++)); do
                if [[ "$i" =~ ^${cur} ]]; then
                    matched_numbers+=("$i")
                    # 限制最多显示10个结果
                    if [[ ${#matched_numbers[@]} -ge 10 ]]; then
                        break
                    fi
                fi
            done
            COMPREPLY=( "${matched_numbers[@]}" )
        fi
    else
        COMPREPLY=()
    fi
}

_complete_dump_ports() {
    local cur
    cur="${COMP_WORDS[COMP_CWORD]}"

    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "global -h" -- ${cur}) )
    fi
}

_complete_dump_trace() {
    local cur
    cur="${COMP_WORDS[COMP_CWORD]}"

    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "-f -h" -- ${cur}) )
    fi
}

_complete_flush_ports() {
    local cur
    cur="${COMP_WORDS[COMP_CWORD]}"

    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "global -h" -- ${cur}) )
    fi
}

_complete_flow_escape_mode() {
    local cur
    cur="${COMP_WORDS[COMP_CWORD]}"
    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "enable disable show -h" -- ${cur}) )
    fi
}

_complete_packet_detect_mode() {
        local cur
        cur="${COMP_WORDS[COMP_CWORD]}"
        if [[ ${COMP_CWORD} -eq 2 ]]; then
            COMPREPLY=( $(compgen -W "--enable --disable --show -h --help" -- ${cur}) )
        fi
}

_complete_flow_offload_speed_stat_opts()
{
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    local opts=(
        -t
        -i
    )

    for ((i = 1; i < COMP_CWORD; i++)); do
        word="${COMP_WORDS[i]}"
        opts=("${opts[@]/$word}")
    done

    COMPREPLY=( $(compgen -W "${opts[*]}" -- ${cur}) )
}
_complete_flow_offload_speed_stat() {
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "start restart show stop -h" -- ${cur}) )
    elif [[ ${COMP_WORDS[2]} == "start" ]]; then
        _complete_flow_offload_speed_stat_opts
    elif [[ ${COMP_WORDS[2]} == "restart" ]]; then
        _complete_flow_offload_speed_stat_opts
    fi
}

_complete_show_flow_api() {
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    prev2="${COMP_WORDS[COMP_CWORD-2]}"

    local api_opts=(
        hovs_flow_mgmt_put
        hovs_flow_mgmt_get_by_key
        hovs_flow_mgmt_get_by_ufid
        hovs_flow_mgmt_del_by_ufid
        hovs_flow_mgmt_del_by_batch
        hovs_flow_mgmt_flush
        hovs_flow_mgmt_dump_start
        hovs_flow_mgmt_dump_next
        hovs_flow_mgmt_dump_done
        hovs_flow_mgmt_get_maxflows
        hovs_flow_mgmt_get_capability
        hovs_flow_mgmt_set_forward_mode
        hovs_flow_mgmt_get_forward_mode
        hovs_statistics_flow_get_by_ufid
        hovs_statistics_flow_flush_by_ufid
        hinic3_rte_flow_create
        hinic3_rte_flow_query
        hinic3_rte_flow_delete
        hinic3_rte_flow_flush_all
        hinic3_rte_flow_destroy_by_batch
        hinic3_rte_flow_dump_start
        hinic3_rte_flow_dump_next
        hinic3_rte_flow_dump_end
        hovs_mega_flow_mgmt_put
        hovs_mega_flow_mgmt_del_by_ufid
        hovs_mega_flow_mgmt_flush
        hovs_mega_flow_mgmt_dump_start
        hovs_mega_flow_mgmt_dump_next
        hovs_mega_flow_mgmt_dump_done
        hovs_statistics_mega_flow_get_by_ufid
        hovs_mega_flow_set_l3_forward
        hovs_flow_mgmt_get_block_table_size
        hovs_flow_mgmt_update
        hovs_flow_mgmt_set_block_version
        hovs_flow_mgmt_get_block_version
    )

    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "api clear all -h" -- ${cur}) )
    elif [[ ${COMP_CWORD} -eq 3 && ${prev} == "api" ]]; then
        COMPREPLY=( $(compgen -W "${api_opts[*]}" -- ${cur}) )
    elif [[ ${COMP_CWORD} -eq 4 && ${prev2} == "api" && " ${api_opts[*]} " == *" ${prev} "* ]]; then
        COMPREPLY=( $(compgen -W "clear" -- ${cur}) )
    fi
}

_complete_exec_cmd_dump()
{
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    local opts="
        -h
        -t
        -x
    "
    local t_opts="
        0
        2
        3
        4
        16
        17
        18
        19
        20
        22
        23
        24
    "
    for ((i = 1; i < COMP_CWORD; i++)); do
        word="${COMP_WORDS[i]}"
        opts=("${opts[@]/$word}")
    done

    if [[ "${prev}" == "-t" ]]; then
        COMPREPLY=( $(compgen -W "${t_opts}" -- ${cur}) )
    else
        COMPREPLY=( $(compgen -W "${opts[*]}" -- ${cur}) )
    fi
}

_complete_exec_cmd_nic_queue()
{
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    local opts="
        -h
        -i
        -d
        -t
        -q
        -w
    "
    local d_opts="
        0
        1
    "
    local t_opts="
        0
        1
        2
    "
    for ((i = 1; i < COMP_CWORD; i++)); do
        word="${COMP_WORDS[i]}"
        opts=("${opts[@]/$word}")
    done

    if [[ "${prev}" == "-d" ]]; then
        COMPREPLY=( $(compgen -W "${d_opts}" -- ${cur}) )
    elif [[ "${prev}" == "-t" ]]; then
        COMPREPLY=( $(compgen -W "${t_opts}" -- ${cur}) )
    else
        COMPREPLY=( $(compgen -W "${opts[*]}" -- ${cur}) )
    fi
}

_complete_exec_cmd_hpd()
{
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    local opts="
        -h
        -v
        -m
        -t
        -i
        -x
    "
    local m_opts="
        0
        1
    "
    local t_opts="
        0
        1
        2
    "
    for ((i = 1; i < COMP_CWORD; i++)); do
        word="${COMP_WORDS[i]}"
        opts=("${opts[@]/$word}")
    done

    if [[ "${prev}" == "-m" ]]; then
        COMPREPLY=( $(compgen -W "${m_opts}" -- ${cur}) )
    elif [[ "${prev}" == "-t" ]]; then
        COMPREPLY=( $(compgen -W "${t_opts}" -- ${cur}) )
    else
        COMPREPLY=( $(compgen -W "${opts[*]}" -- ${cur}) )
    fi
}

_complete_exec_cmd() {
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "-h -v dump xstats nic_queue hpd" -- ${cur}) )
    elif [[ ${COMP_WORDS[2]} == "dump" ]]; then
       _complete_exec_cmd_dump
    elif [[ ${COMP_CWORD} -eq 3 && ${prev} == "xstats" ]]; then
        COMPREPLY=( $(compgen -W "-h -i" -- ${cur}) )
    elif [[ ${COMP_WORDS[2]} == "nic_queue" ]]; then
        _complete_exec_cmd_nic_queue
    elif [[ ${COMP_WORDS[2]} == "hpd" ]]; then
        _complete_exec_cmd_hpd
    fi
}

_complete_capture_probe_start()
{
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    local opts="
        -w
        -t
        -sip
        -dip
        -host
        -smac
        -dmac
        -eth_type
        -ip_proto
        -vxlan_inner
        -vlan
        -sport
        -dport
        -vxlan_vni
        -P
        -c
        -n
        -o
        -thread
    "
    local ip_proto_opts="
       TCP
       UDP
       ICMP
       SCTP
       ICMPV6
    "
    local p_opts="
        in
        out
        inout
    "
    for ((i = 1; i < COMP_CWORD; i++)); do
        word="${COMP_WORDS[i]}"
        opts=("${opts[@]/$word}")
    done

 if [[ "${prev}" == "-ip_proto" ]]; then
        COMPREPLY=( $(compgen -W "${ip_proto_opts}" -- ${cur}) )
    elif [[ "${prev}" == "-P" ]]; then
        COMPREPLY=( $(compgen -W "${p_opts}" -- ${cur}) )
    elif [[ "${prev}" == "-n" ]]; then
        COMPREPLY=( $(compgen -W "3 5 8 10" -- ${cur}) )
    elif [[ "${prev}" == "-t" ]]; then
        COMPREPLY=( $(compgen -W "10 30 60" -- ${cur}) )
    elif [[ "${prev}" == "-c" ]]; then
        COMPREPLY=( $(compgen -W "50000 300000 1000000" -- ${cur}) )
    elif [[ "${prev}" == "-o" ]]; then
        compopt -o nospace
        COMPREPLY=( $(compgen -d -S '/' -- ${cur}) )
        if [[ ${#COMPREPLY[@]} -eq 0 ]]; then
            compopt +o nospace
            COMPREPLY=( "" )
        fi
    else
        COMPREPLY=( $(compgen -W "${opts[*]}" -- ${cur}) )
    fi
}

_complete_enable_capture_probe() {
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    # 可用选项列表（短选项和长选项）
    local main_options="-c --cpu"
    local alone_options="-q --query -h --help"
    local cpu_options="high low"

    # 动态获取帮助文本，检查是否支持 -p 参数
    local help_output
    help_output=$(dpak-ovs-ctl hwoff/enable-capture-probe --help 2>/dev/null)

    # 检查是否支持 -p 参数
    local probe_modes=""
    local has_p_option=0
    if echo "$help_output" | grep -E '(^|\s)-p(\s|$)' > /dev/null; then
        has_p_option=1
        # 从 ENUM#<...> 中提取模式列表
        probe_modes=$(echo "$help_output" | grep "ENUM#<" | awk -F'ENUM#<' '{print $2}' | awk -F'>' '{print $1}' | tr ',' ' ')
    fi

    # 检查是否已经使用了独立选项(-q/--query或-h/--help)
    local has_standalone_option=0
    for word in "${COMP_WORDS[@]:1:COMP_CWORD}"; do
        if [[ "$word" == "-q" || "$word" == "--query" ||
              "$word" == "-h" || "$word" == "--help" ]]; then
            has_standalone_option=1
            break
        fi
    done

    case ${COMP_CWORD} in
        2)
            if [[ $has_p_option -eq 1 ]]; then
                COMPREPLY=( $(compgen -W "$main_options -p --pcap $alone_options" -- ${cur}) )
            else
                COMPREPLY=( $(compgen -W "$main_options $alone_options" -- ${cur}) )
            fi
            ;;
        3)
            case ${prev} in
                -c|--cpu)
                    COMPREPLY=( $(compgen -W "$cpu_options" -- ${cur}) )
                    ;;
                -p|--pcap)
                    if [[ -n "$probe_modes" ]]; then
                        COMPREPLY=( $(compgen -W "$probe_modes" -- ${cur}) )
                    fi
                    ;;
                -q|--query|-h|--help)
                    # 独立选项不需要进一步补全
                    COMPREPLY=()
                    ;;
                *)
                    # 默认情况，显示所有选项
                    if [[ $has_p_option -eq 1 ]]; then
                        COMPREPLY=( $(compgen -W "$main_options -p --pcap $alone_options" -- ${cur}) )
                    else
                        COMPREPLY=( $(compgen -W "$main_options $alone_options" -- ${cur}) )
                    fi
                    ;;
            esac
            ;;
        *)
            if [[ $has_standalone_option -eq 1 ]]; then
                # 如果已经使用了独立选项，不再补全
                COMPREPLY=()
            else
                # 检查已使用的主选项
                local used_main_options=""
                for word in "${COMP_WORDS[@]:1:COMP_CWORD-1}"; do
                    if [[ "$word" == "-c" || "$word" == "--cpu" ||
                          "$word" == "-p" || "$word" == "--pcap" ]]; then
                        used_main_options="$used_main_options $word"
                    fi
                done

                # 根据前一个参数决定补全内容
                case ${prev} in
                    -c|--cpu)
                        COMPREPLY=( $(compgen -W "$cpu_options" -- ${cur}) )
                        ;;
                    -p|--pcap)
                        if [[ -n "$probe_modes" ]]; then
                            COMPREPLY=( $(compgen -W "$probe_modes" -- ${cur}) )
                        fi
                        ;;
                    *)
                        # 如果不是跟在选项后面，提供可用的主选项
                        local available_options=""
                        if [[ ! $used_main_options =~ "-c" && ! $used_main_options =~ "--cpu" ]]; then
                            available_options="$available_options -c --cpu"
                        fi
                        if [[ $has_p_option -eq 1 && ! $used_main_options =~ "-p" && ! $used_main_options =~ "--pcap" ]]; then
                            available_options="$available_options -p --pcap"
                        fi

                        COMPREPLY=( $(compgen -W "$available_options" -- ${cur}) )
                        ;;
                esac
            fi
            ;;
    esac
}

_complete_capture_probe() {
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "start show stop help" -- ${cur}) )
    elif [[ ${COMP_WORDS[2]} == "start" ]]; then
        _complete_capture_probe_start
    elif [[ ${COMP_CWORD} -eq 3 && ${prev} == "stop" ]]; then
        COMPREPLY=( $(compgen -W "-pcap_id" -- ${cur}) )
    elif [[ ${COMP_CWORD} -eq 3 && ${prev} == "show" ]]; then
        COMPREPLY=( $(compgen -W "all -pcap_id" -- ${cur}) )
    fi
}

_complete_set_pkt_forward_mod() {
    local cur
    cur="${COMP_WORDS[COMP_CWORD]}"
    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "low_latency high_throughput" -- ${cur}) )
    fi
}

_complete_trace_flow() {
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "-proto -sip -dip -sport -dport" -- ${cur}) )
    elif [[ ${COMP_CWORD} -eq 3 && ${prev} == "-proto" ]]; then
        COMPREPLY=( $(compgen -W "tcp udp" -- ${cur}) )
    fi
}

_complete_dump_ufid_map_hw() {
    local cur
    cur="${COMP_WORDS[COMP_CWORD]}"

    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "-n -h all" -- ${cur}) )
    fi
}

_complete_set_offload_switch() {
    local cur
    cur="${COMP_WORDS[COMP_CWORD]}"

    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "enable disable -h" -- ${cur}) )
    fi
}

_complete_set_policy_info() {
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    local set_policy_opts="
        permission-enable
        user-permission-default
        permission-update-interval
        permission-credit
        delay-mode
        delay-duration
        delay-packet-pps
        delay-packet-nums
        garbage-clean-interval
        max-garbage-life
        user-idle-time
        limit-offload-flow-nums
        clean-window-enable
        flow-clean-pps
        window-update-interval
        -h
    "
    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "${set_policy_opts}" -- ${cur}) )
    elif [[ ${COMP_CWORD} -eq 3 && ${prev} == "permission-enable" ]]; then
        COMPREPLY=( $(compgen -W "off on" -- ${cur}) )
    elif [[ ${COMP_CWORD} -eq 3 && ${prev} == "delay-mode" ]]; then
        COMPREPLY=( $(compgen -W "delay-packet-count delay-packet-pps no-offload-delay" -- ${cur}) )
    elif [[ ${COMP_CWORD} -eq 3 && ${prev} == "clean-window-enable" ]]; then
        COMPREPLY=( $(compgen -W "off on" -- ${cur}) )
    fi
}

_complete_dump_ufid_map_sw() {
    local cur
    cur="${COMP_WORDS[COMP_CWORD]}"

    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "all -n -h" -- ${cur}) )
    fi
}

_complete_add_del_rapid_proto() {
    local cur
    cur="${COMP_WORDS[COMP_CWORD]}"

    if [[ ${COMP_CWORD} -eq 2 ]]; then
        COMPREPLY=( $(compgen -W "tcp udp icmp icmpv6 -h" -- ${cur}) )
    fi
}

_dpak_ovs_ctl() {
    local cur prev
    local run_time_mode
    run_time_mode=$(grep -E '^run_time_mode=' "$config_file" | awk -F= '{print $2+0}')
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[1]}"

    if [[ ${COMP_CWORD} -eq 1 ]]; then
        if [[ $run_time_mode -eq 1 ]]; then
            _get_run_time_mode_options
            return 0
        fi
        _get_top_level_options
        return 0
    fi

    case "${prev}" in
        hwoff/show-global-api)
            _complete_show_global_api
            return 0
            ;;
        hwoff/dump-hwoff-flows)
            _complete_dump_hinic3_flows
            return 0
            ;;
        hwoff/dump-upcall-queue-info)
            _complete_dump_upcall_queue_info
            return 0
            ;;
        hwoff/show-port-api)
            _complete_show_port_api
            return 0
            ;;
        hwoff/set-log-level)
            _complete_set_log_level
            return 0
            ;;
        hwoff/log-limit-ctl)
            _complete_log_limit_ctl
            return 0
            ;;
        hwoff/dump-ports)
           _complete_dump_ports
            return 0
            ;;
        hwoff/dump-trace)
           _complete_dump_trace
            return 0
            ;;
        hwoff/dump-ufid-map-hw)
           _complete_dump_ufid_map_hw
            return 0
            ;;
        hwoff/flush-ports)
           _complete_flush_ports
            return 0
            ;;
        hwoff/flow-escape-mode)
           _complete_flow_escape_mode
            return 0
            ;;
        hwoff/packet-detect-mode)
            _complete_packet_detect_mode
            return 0
            ;;
        hwoff/dump-ufid-map-sw)
           _complete_dump_ufid_map_sw
            return 0
            ;;
        hwoff/flow-offload-speed-stat)
           _complete_flow_offload_speed_stat
            return 0
            ;;
        hwoff/show-flow-api)
           _complete_show_flow_api
            return 0
            ;;
        hwoff/exec-cmd)
           _complete_exec_cmd
            return 0
            ;;
        hwoff/enable-capture-probe)
           _complete_enable_capture_probe
            return 0
            ;;
        hwoff/capture-probe)
           _complete_capture_probe
            return 0
            ;;
        hwoff/set-forward-mode)
           _complete_set_pkt_forward_mod
            return 0
            ;;
        hwoff/trace-flow)
           _complete_trace_flow
            return 0
            ;;
        *)
            return 0
            ;;
    esac
}

complete -F _dpak_ovs_ctl dpak-ovs-ctl
