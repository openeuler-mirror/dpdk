..  SPDX-License-Identifier: BSD-3-Clause
    Copyright(c) 2026 Huawei Technologies Co., Ltd

HNS5 Crypto Poll Mode Driver
============================

The HNS5 crypto poll mode driver provides symmetric crypto offload for the SEC
(Symmetric Encrypt Engine) accelerator integrated in the board SoC.

Unlike a pure QM lookaside path, the SEC engine is driven in POE (Packet Order
Engine) mode through the board BSP library ``libhidrv_api``: the SEC
PF/VF are set up together with POE submit/completion groups, and DMA buffers
are allocated from a file-backed 32 MiB HugeTLB pool managed by the BMU. A
fresh 128-byte SEC descriptor is built per request from the DPDK sym xform
chain; the engine performs the key schedule on the origin key, so no
persistent HW session is needed (the DPDK session is pure software state).

The hardware access path is adapted from the vendor OpenSSL engine reference.
It is **board-specific**: ``libhidrv_api``, the SEC/BMU/POE ``/dev`` devices
and a 32 MiB hugetlbfs mount are only present on the S920L18 board image.
On a host without the BSP the driver is skipped at build time.

Features
--------

HNS5 crypto PMD has support for:

Cipher algorithms:

* ``RTE_CRYPTO_CIPHER_SM4_CBC``
* ``RTE_CRYPTO_CIPHER_AES_CBC``

Hash algorithms:

* ``RTE_CRYPTO_AUTH_SM3``
* ``RTE_CRYPTO_AUTH_SM3_HMAC``
* ``RTE_CRYPTO_AUTH_SHA256``
* ``RTE_CRYPTO_AUTH_SHA256_HMAC``

Chained cipher-then-auth operations are supported.

.. note::

   All algorithms listed above are registered in the driver capability
   table and accepted at session creation; the driver does not intercept
   them. The acceptance scope of this release covers ``SM4-CBC`` and
   ``SM3-HMAC`` only; the remaining algorithms are provided by the SEC
   engine but have not been validated in this release.

   ``AES-ECB`` is implemented by the SEC engine but is **not offered**:
   equal plaintext blocks produce equal ciphertext blocks, so it leaks
   the structure of the data. It has no capability entry and a session
   request is refused with ``-ENOTSUP``.

Device and queue pair model
---------------------------

The PMD registers as a virtual device (``crypto_hns5``) since the SEC engine
is a SoC platform device rather than a discoverable PCI function. One SEC VF
is set up per device, with a single in-order queue pair. Each queue pair keeps
a fixed-depth in-flight ring: ``enqueue`` builds descriptors and submits them
through ``hidrv_send_sec``, and ``dequeue`` polls the POE completion queue
(in-order, so the ring head is reaped next) and copies results back into the
DPDK mbufs.

DMA memory and recovery
-----------------------

A 32 MiB HugeTLB pool is opened under a canonical owner lock. Its physical
address is read from ``/proc/self/pagemap`` (the SMMU is configured in
passthrough mode). Once hardware has been touched, the pool is retained unless
every teardown and reset step completes; a later process therefore never
reuses live DMA memory. After an uncertain teardown the engine is poisoned
and must be reset through the BSP flow before reuse.

Build and run
-------------

#. Ensure the board BSP provides ``libhidrv_api`` (e.g.
   ``/opt/extralib/libhidrv_api.so``) and the matching SDK headers.

#. Make the BSP SDK archive available. ``buildtools/hns5_compile.sh`` unpacks
   the vendor headers into ``drivers/net/hns5/dependency/{hidrv,native}``,
   which is the only place the meson files look. Pass the archive as an
   argument, or set ``SDK_ZIP``/``SDK_DIR`` if it is not next to the source
   tree.

#. Build DPDK. The driver builds automatically when ``libhidrv_api`` is found
   and is skipped otherwise:

   .. code-block:: console

      cd dpdk
      meson setup build
      ninja -C build

#. Prepare the 32 MiB hugetlbfs mount used by the SEC pool:

   .. code-block:: console

      mkdir -p /mnt/huge32m
      mount -t hugetlbfs none /mnt/huge32m -o pagesize=32M,mode=0700

#. Run a crypto application:

   .. code-block:: console

      sudo ./build/app/dpdk-test --vdev=crypto_hns5 --log-level=6
      RTE>>cryptodev_sm4_autotest

Runtime configuration options
-----------------------------

The device is created with ``--vdev=crypto_hns5[,key=value]*``. Every key is
optional and the effective configuration is printed back at probe time
(``effective config: ...``), so a value that did not apply is visible without
reading the source. A key the driver does not know is reported as a warning
and ignored; it used to be dropped silently, which made a typo look like "the
option had no effect".

Every value is read through one bounded parser: a non-integer, a trailing
garbage suffix (``12abc``), an empty value or a number outside the range
below is reported and leaves the documented default in place. Numbers are
accepted in decimal or hex (``base 0``), so ``0x10`` means 16.

Operational options
~~~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 24 18 58

   * - Option
     - Default
     - Description
   * - ``qps=<1-16>``
     - ``1``
     - Number of queue pairs. Each extra pair runs on its own SEC vcpu; all
       pairs share one SEC VF and one completion group, so completions are
       dispatched by buffer address (see ``reaper_qp``).
   * - ``vcpu=<0-63>``
     - ``1``
     - SEC engine vcpu for the first queue pair. ``0`` is taken as "not
       specified" and falls back to the driver default.
   * - ``pool_mb=<MiB>``
     - ``32``
     - Size of the HugeTLB pool that backs the SEC segments. Must be at least
       32 MiB, at most 8 GiB, and a multiple of 4 MiB; the driver keeps 4 MiB
       of it for the POE queues.
   * - ``cap=<number>``
     - derived
     - In-flight limit per queue pair. Derived from the pool (buffer count
       minus the per-queue segment cache, split over the queue pairs); lower
       it to make a saturated board fail fast instead of queueing. ``0`` means
       "use the derived value". Above 65470 it is rejected: the queue depth is
       16 bits and must still hold the cap plus one burst of slack, so a
       larger value could not be honoured.
   * - ``stats=<0|1>``
     - ``0``
     - Per-operation timing: six counter reads per operation plus shared
       counters. It changes the number being measured, so it stays off unless
       asked for.
   * - ``bsp_log=<default|debug|info|warn|err>``
     - unchanged
     - Log level of the board BSP library (``hidrv_log``). DPDK's
       ``--log-level`` does not reach it, and the BSP prints buffer
       addresses at debug level, so keep ``info`` or above in production.
       Applied before any BSP output, because this driver probes first.
   * - ``reap=<que|grp|msg>``
     - ``que``
     - Which POE message getter the completion path uses. Provided to compare
       their cost; the total is printed at close.
   * - ``reaper_qp=<0..qps-1|65535>``
     - ``0`` with several queue pairs
     - Queue pair that drains the shared completion group on everyone's
       behalf. ``65535`` means no dedicated reaper: each queue pair reaps
       when it dequeues. With a single queue pair the driver always reaps
       lazily, since there is no other pair to drain for. A value outside
       the queue-pair count falls back to lazy with an error message.
   * - ``reap_batch=<number>``
     - ``64``
     - Completions drained per hold of the completion-group lock
       (1..4096).
   * - ``reap_polls=<number>``
     - ``64``
     - Consecutive empty polls before a reaping pass gives up (1..4096).
       Every empty poll steals submission time, so a larger queue-pair count
       wants a smaller budget.
   * - ``reap_spin=<number>``
     - ``64``
     - Attempts to take the completion-group lock before the reaper moves on
       (1..4096).
   * - ``reap_fair=<0|1>``
     - ``0``
     - Stop reaping as soon as this thread's own request is flagged, instead
       of draining the whole batch. Only useful together with ``smooth``:
       with a backlog, one lagging thread holds up the rest. Draining the
       full batch is otherwise measurably faster.
   * - ``smooth=<0|1>``
     - ``0``
     - Park operations that are not ready in a per-queue backlog (4096
       entries) and hand them out on the next dequeue, so an application
       polling with a fixed burst sees fewer short returns.
   * - ``zero_copy_out=<0|1>``
     - ``1``
     - Let the engine write the result straight into the application mbuf,
       which removes the copy at dequeue. Requests that cannot be handled in
       place (out-of-place, multi-segment, unaligned start) fall back to the
       copy automatically. ``0`` restores the copy path for comparison.

Bring-up and experiment options
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

These exist to A/B test the BSP and the POE group configuration. None of them
is expected in normal operation, and the driver only forwards them - it does
not validate the value against a board configuration.

.. list-table::
   :header-rows: 1
   :widths: 24 18 58

   * - Option
     - Default
     - Description
   * - ``zero_copy=<0|1>``
     - ``0``
     - Legacy whole-request zero-copy switch. ``1`` is rejected in POE mode -
       completion is delivered into the buffer named by the descriptor, which
       has to belong to the BMU pool - and the copy path is kept.
   * - ``zero_copy_in=<0|1>``
     - ``0``
     - Point the descriptor input at the application mbuf and register that
       pool with the BMU on the first operation. The BSP currently refuses
       this (``rc=-33``), so the driver falls back to copying after the first
       refusal and stops retrying. Kept for BSP work and for a future QM path.
   * - ``zc_app_pool=<0|1>``
     - ``0``
     - Register the application pool with the BMU without requesting input
       zero-copy. Implied by ``zero_copy_in=1``.
   * - ``poe_preload=<0|1>``
     - ``0``
     - POE group option: preload one cache line into L2 when the CPU reads a
       completion message.
   * - ``poe_order=<0|1>``
     - ``0``
     - POE group ordering.
   * - ``poe_flow=<0|1>``
     - ``1``
     - POE overflow (drop) enable.
   * - ``poe_int_th=<number>``
     - ``0``
     - POE group interrupt watermark. Only the syntax is checked (a plain
       32-bit integer): the BSP header types the field as ``unsigned int``
       and gives no register width to validate against, so the driver does
       not invent an upper bound.
   * - ``poe_bp_th=<number>``
     - ``0``
     - POE backpressure-to-PPE threshold; accepts hex (``0x3fffff``), same
       syntax-only check as ``poe_int_th``.
   * - ``sec_skip_len=<0-15>``
     - ``0``
     - SEC VF ``skip_length``, 0..15 words. Applied through a ``dlsym`` lookup
       for the BSP symbol; if the BSP does not export it, the driver warns and
       moves on. A value outside 0..15 is reported and ignored - it is never
       passed to the BSP.
   * - ``sec_get_poe=<0|1>``
     - ``0``
     - SEC VF ``queue_get_poe_enable``, same lookup and same fallback as
       ``sec_skip_len``.
   * - ``debug_memcpy=<0|1>``
     - ``0``
     - Run a 1 KiB copy micro benchmark once at device close, to tell a
       cold-cache copy from slow memory. It copies its own buffers, so it
       prints no application data.

Limitations
-----------

* Board verification on S920L18 is pending; source-level compilation against
  DPDK 22.11 plus the vendor headers is the host-side boundary.
* Up to 16 queue pairs per device (``qps=``); they share one SEC VF,
  so the completion group is shared and dispatched by buffer address.
* A message must fit one SEC segment: the payload limit is
  ``HNS5_SEC_MAX_PAYLOAD`` (15424 bytes) inside a 16 KiB buffer.
  Longer messages are not yet chunked across multiple segments.
