HINIC3 poll mode driver
=====================
 
This is a demo. As the DPDK framework lacks a mechanism for querying 
PMD private load parameters, documentation must be released alongside 
the open-source code to inform users of the supported private load parameters and their usage.
 
Driver options
^^^^^^^^^^^^^^
 
- ``tx_pending_limit`` parameter [int]
Set the threshold for the number of coalesced packets in the Tx queue, unit is packets.
Note: The configured value must be a multiple of 8.
- ``tx_coalescing_time`` parameter [int]
Set the threshold for the number of coalesced time in the Tx queue, unit is us.
Note: The configured value must be a multiple of 5.
- ``rx_cqe_coalesce_num`` parameter [int]
Set the threshold for the number of coalesced packets in the Rx queue, unit is packets.
Note: The configured value must be a multiple of 8.
- ``rx_cqe_timer_loop`` parameter [int]
Set the threshold for the number of coalesced time in the Rx queue, unit is us.
Note: The configured value must be a multiple of 5.
- ``rx_cqe_compact_en`` parameter [int]
Set to 0 to disable RX CQE and packet coalescing delivery. This feature is enabled by default.