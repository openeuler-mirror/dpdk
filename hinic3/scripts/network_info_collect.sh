#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
# Description: 巡检信息收集脚本
# Create: 2023-10-19

function get_info() {
    #dpak-ovs线程状态
    echo "------------------------------Thread stats------------------------------"
    dpak-ovs-ctl hwoff/show-thread-stats
    echo ""

    #当前设备的转发包模式
    echo "------------------------------Forward mode------------------------------"
    dpak-ovs-ctl hwoff/show-forward-mode
    echo ""

    #当前逃生模式状态
    echo "---------------------------Flow Escape mode-----------------------------"
    dpak-ovs-ctl hwoff/flow-escape-mode show
    echo ""

    #内存占用情况
    echo "------------------------------Memory info-------------------------------"
    cat /proc/$(pidof -s ovs-vswitchd)/status | grep VmRSS 2>/dev/null
    echo ""

    #内存池状态
    echo "------------------------------Mpool stats-------------------------------"
    dpak-ovs-ctl hwoff/show-mpool-stats
    echo ""

    #各模块内存使用情况统计
    echo "----------------------------Module Meminfo------------------------------"
    dpak-ovs-ctl hwoff/show-meminfo
    echo ""

    #各模块大页内存使用情况统计
    echo "---------------------------Hugepage Meminfo-----------------------------"
    dpak-ovs-ctl hwoff/show-hugepage-meminfo
    echo ""

    #端口个数
    echo "------------------------------Port info----------------------------------"
    dpak-ovs-ctl hwoff/show-function-flavor
    dpak-ovs-ctl hwoff/show-function-stats
    echo ""

    #全局端口状态
    echo "--------------------------------Global ports-----------------------------"
    dpak-ovs-ctl hwoff/dump-ports global
    echo ""

    #收发队列信息
    echo "-------------------------Dump unpcall queue info-------------------------"
    dpak-ovs-ctl hwoff/dump-upcall-queue-info
    echo ""

    #丢包统计信息
    echo "---------------------------Dump info------------------------------------"
    #获取端口大名称
    port_name_str=$(ovs-vsctl show | awk '/^ +Port/ { port=$2; getline; getline; \
        if ($1 == "type:") { if ($2 == "dpdk") { gsub(/^"|"$/, "", port); print port } } }')
    for i in $port_name_str
    do
        port_name=$i
        echo "Port $port_name dump info:"
        dpak-ovs-ctl hwoff/dump-ports $port_name
        echo ""
    done
    echo ""

    #回调线程个数
    echo "--------------------------Flow offload thread num-----------------------"
    cat /etc/dpak/net/agent_config.ini | grep offload_thread_num
    echo ""

    #当前抓包任务列表
    echo "-------------------------------Capture probe info-----------------------"
    dpak-ovs-ctl hwoff/capture-probe show all
    echo ""

    #收集网络卸载相关日志
    echo "-----------------------------Collecting log-----------------------------"
    if [ -d "$1/dpak-snic_log" ];then
        rm -rf $1/dpak-snic_log #收集日志存放位置
    fi
    mkdir -p $1/dpak-snic_log
    cp $(find /var/log/openvswitch/ -name 'ovs-vswitchd*') $1/dpak-snic_log 2>/dev/null
    logname=$1/dpak-dpunic-$(date +%Y_%m_%d_%H_%M_%S).tar.gz
    tar czvf $logname -C $1 dpak-snic_log --remove-files
    if [ ! -d "$1/logs" ];then
        mkdir -p $1/logs
    fi
    mv $logname $1/logs
    echo "Collecting log finished. The log is stored in \"$1/logs\""
    echo ""

    #收集异常分支统计信息
    echo "--------------------------Error stats info-------------------------------"
    dpak-ovs-ctl hwoff/show-error-stats -l all
    echo ""

    #已卸载流表个数信息
    echo "--------------------------Flow num-----------------------------------------"
    dpak-ovs-ctl hwoff/show-offload-flow-num #软件流表
    dpak-ovs-ctl hwoff/dump-hwoff-flows -n #硬件流表
    dpak-ovs-ctl hwoff/show-hmap-flow-num #哈希流表
    echo ""

    #dump硬件流表信息
    echo "--------------------------Dump hwoff flows----------------------------------"
    flows_filename=dump-flows-$(date +%Y_%m_%d_%H_%M_%S).log
    dpak-ovs-ctl hwoff/dump-hwoff-flows -f $flows_filename
    if [ -d "$1/dpak-dpunic_flows" ];then   
        rm -rf $1/dpak-dpunic_flows  #收集硬件流表存放位置
    fi    
    mkdir -p $1/dpak-dpunic_flows    
    mv "/var/log/dpak/dpak_ovs_data/$flows_filename" $1/dpak-dpunic_flows
    flows_logname=$1/dpak-dpunic_flows-$(date +%Y_%m_%d_%H_%M_%S).tar.gz
    tar czvf $flows_logname -C $1 dpak-dpunic_flows --remove-files   
    if [ ! -d "$1/flows" ];then        
        mkdir -p $1/flows    
    fi   
    mv $flows_logname $1/flows
    echo "Dump flows finished. The flows are stored in \"$1/flows\""
    echo ""

    #已卸载模糊流表信息
    echo "----------------------------Dump fuzzy flows---------------------------------"
    dpak-ovs-ctl hwoff/dump-fuzzy-flows
    echo ""

    #流镜像信息
    echo "----------------------------Dump sample session---------------------------------"
    dpak-ovs-ctl hwoff/show-sample-session
    echo ""

    #OVS驱动信息，组件相关
    echo "--------------------------------Exec cmd------------------------------------"
    echo "--------------------------------Bond cfg------------------------------------"
    dpak-ovs-ctl hwoff/exec-cmd dump -t 16
    echo ""
    echo "-------------------------------Global cfg-----------------------------------"
    dpak-ovs-ctl hwoff/exec-cmd dump -t 17
    echo ""
    echo "--------------------------------Bum cfg-------------------------------------"
    dpak-ovs-ctl hwoff/exec-cmd dump -t 18
    echo ""
    echo "-------------------------------Lib stats------------------------------------"
    dpak-ovs-ctl hwoff/exec-cmd dump -t 19
    echo ""
    echo "-----------------------------Upcall queue mgr-------------------------------"
    dpak-ovs-ctl hwoff/exec-cmd dump -t 20
    echo ""
    echo "--------------------------------Mempool-------------------------------------"
    dpak-ovs-ctl hwoff/exec-cmd dump -t 22
    echo ""
    echo "--------------------------------Mgr stats-----------------------------------"
    dpak-ovs-ctl hwoff/exec-cmd dump -t 23
    echo ""

    #所有硬件流表相关API信息
    echo "-------------------------------Flow API-------------------------------------"
    dpak-ovs-ctl hwoff/show-flow-api all
    echo ""

    #查询所有硬件全局操作相关API
    echo "------------------------------Global API------------------------------------"
    dpak-ovs-ctl hwoff/show-global-api all
    echo ""

    #当查询所有硬件端口相关API调用统计信息
    echo "-------------------------------Port API-------------------------------------"
    dpak-ovs-ctl hwoff/show-port-api all
    echo ""
}

function check_input() {
    if (( $# == 0 || $# > 1 ));then
        echo "Please input a parameter as the output file path."
        exit 1
    fi

    if [ ! -d "$1" ];then
        echo "The path does not exist, please input a valid path."
        exit 1
    fi
}

function main() {
    check_input $@

    info_file_path=$1
    file_name="network_info.txt"
    get_info $1 > $info_file_path/$file_name
}

main $@