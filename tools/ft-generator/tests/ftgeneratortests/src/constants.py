"""
Author(s): Matej Hulák <hulak@cesnet.cz>

Copyright: (C) 2023 CESNET, z.s.p.o.
SPDX-License-Identifier: BSD-3-Clause

File defines project constants.
"""

L3_PROTOCOL_IPV4 = 4
L3_PROTOCOL_IPV6 = 6
L4_PROTOCOL_TCP = 6
L4_PROTOCOL_UDP = 17
L4_PROTOCOL_ICMP = 1
L4_PROTOCOL_ICMPV6 = 58
L4_PROTOCOL_NONE = 0

MAC_MASK_SIZE = 48
MTU_SIZE = 1500

ENCAPSULATION_ABS_TOLERANCE = 0.1
FRAGMENTATION_ABS_TOLERANCE = 0.2
REPORT_REL_TOLERANCE = 0.2
TIMING_REL_TOLERANCE = 0.2

GENERATOR_ERROR_RET_CODE = 1
