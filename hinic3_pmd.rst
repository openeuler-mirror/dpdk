HINIC3 poll mode driver
=====================

This is a demo. As the DPDK framework lacks a mechanism for querying 
PMD private load parameters, documentation must be released alongside 
the open-source code to inform users of the supported private load parameters and their usage.

Driver options
^^^^^^^^^^^^^^

- ``rx_empty_threshold`` parameter [int]
Set the threshold for consecutive empty receive polls. When the number of 
empty polls reaches this value within a short interval, the driver will 
skip the current receive cycle to reduce CPU overhead.
- ``tx_free_loop`` parameter [int]
Set the maximum number of retry attempts for reclaiming TX descriptors. 
If the required space is not available after this many attempts, the driver 
will stop transmitting the current batch to avoid CPU starvation.