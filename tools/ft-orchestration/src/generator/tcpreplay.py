"""
Author(s): Dominik Tran <tran@cesnet.cz>

Copyright: (C) 2022 CESNET, z.s.p.o.
SPDX-License-Identifier: BSD-3-Clause

Module implements TcpReplay class representing tcpreplay tool.
"""

import logging
import re
import tempfile
from os import path
from typing import Optional

from src.generator.interface import (
    MbpsSpeed,
    MultiplierSpeed,
    PcapPlayer,
    PcapPlayerStats,
    PpsSpeed,
    ReplaySpeed,
    TopSpeed,
)
from src.generator.scapy_rewriter import RewriteRules, rewrite_pcap


class TcpReplay(PcapPlayer):
    """Class provides means to use tcpreplay tool.

    Parameters
    ----------
    host : Host
        Host class with established connection.
    add_vlan : int, optional
        If specified, vlan header with given tag will be added to replayed packets.
    mtu: int, optional
        Mtu size of interface on which traffic will be replayed.
    """

    # pylint: disable=super-init-not-called
    def __init__(self, host, add_vlan: Optional[int] = None, mtu: int = 2000):
        self._host = host
        self._vlan = add_vlan
        self._interface = None
        self._dst_mac = None
        self._mtu = mtu
        self._result = None

        self._bin = "tcpreplay"
        if host.run(f"command -v {self._bin}", check_rc=False).exited != 0:
            logging.getLogger().error("%s is missing on host %s", self._bin, host.get_host())
            raise RuntimeError(f"{self._bin} is missing")

    def add_interface(self, ifc_name: str, dst_mac: Optional[str] = None):
        """Add interface on which traffic will be replayed.

        Parameters
        ----------
        ifc_name : str
            String name of interface, e.g. os name or pci address.
        dst_mac : str, optional
            If specified, destination mac address will be edited in packets which are replayed on interface.

        Raises
        ------
        RuntimeError
            If more than one interface is added.
        """

        if self._interface:
            raise RuntimeError("Tcpreplay generator supports only one replaying interface.")
        self._interface = ifc_name
        self._dst_mac = dst_mac

    # pylint: disable=arguments-differ
    def start(
        self,
        pcap_path: str,
        speed: ReplaySpeed = MultiplierSpeed(1.0),
        loop_count: int = 1,
        asynchronous=False,
        check_rc=True,
        timeout=None,
    ):
        """Start tcpreplay with given command line options.

        tcpreplay is started with root permissions.

        Parameters
        ----------
        pcap_path : str
            Path to pcap file to replay. Path to PCAP file must be local path.
            Method will synchronize pcap file on remote machine.
        speed : ReplaySpeed, optional
            Argument to modify packets replay speed.
        loop_count : int, optional
            Packets from pcap will be replayed loop_count times.
            Zero or negative value means infinite loop and must be started asynchronous.
        asynchronous : bool, optional
            If True, start tcpreplay in asynchronous (non-blocking) mode.
        check_rc : bool, optional
            If True, raise ``invoke.exceptions.UnexpectedExit`` when
            return code is nonzero. If False, do not raise any exception.
        timeout : float, optional
            Raise ``CommandTimedOut`` if command doesn't finish within
            specified timeout (in seconds).

        Returns
        -------
        invoke.runners.Result or invoke.runners.Promise
            Execution result. If ``asynchronous``, Promise is returned.

        Raises
        ------
        RuntimeError
            If tcpreplay or tcpreplay-edit is missing or output interface was not specified.
        """

        if not self._interface:
            logging.getLogger().error("no output interface was specified")
            raise RuntimeError("no output interface was specified")

        # negative values mean infinite loop
        loop_count = max(loop_count, 0)

        if loop_count == 0 and not asynchronous:
            logging.getLogger().error("tcpreplay can not be started synchronous in infinite loop")
            raise RuntimeError("tcpreplay can not be started synchronous in infinite loop")

        rewrite_rules = None
        if self._dst_mac or self._vlan is not None:
            rewrite_rules = RewriteRules()
            if self._dst_mac:
                rewrite_rules.edit_dst_mac = self._dst_mac
            if self._vlan is not None:
                rewrite_rules.add_vlan = self._vlan

        cmd_options = []
        cmd_options += [self._get_speed_arg(speed)]
        cmd_options += [f"--loop={loop_count}"]
        cmd_options += ["--preload-pcap"]  # always preload pcap file
        cmd_options += [f"--intf1={self._interface}"]

        self._host.run(f"sudo ip link set dev {self._interface} up")
        self._host.run(f"sudo ip link set dev {self._interface} mtu {self._mtu}")

        with tempfile.TemporaryDirectory() as temp_dir:
            if rewrite_rules:
                pcap_path = rewrite_pcap(pcap_path, rewrite_rules, path.join(temp_dir, path.basename(pcap_path)))

            self._result = self._host.run(
                f"sudo {self._bin} {' '.join(cmd_options)} {pcap_path}", asynchronous, check_rc, timeout=timeout
            )

        return self._result

    def stats(self) -> PcapPlayerStats:
        """Get stats based on result from ``start`` method.

        This method will block if tcpreplay was started in asynchronous mode.

        Returns
        -------
        PcapPlayerStats
            Class containing statistics of sent packets and bytes.
        """

        result = self._result
        if not hasattr(result, "stdout"):
            result = self._host.wait_until_finished(result)

        pkts = int(re.findall(r"(\d+) packets", result.stdout)[0])
        bts = int(re.findall(r"(\d+) bytes", result.stdout)[0])
        return PcapPlayerStats(pkts, bts)

    def stop(self):
        """Stop current execution of tcpreplay.

        This method is valid only if tcpreplay was started in
        asynchronous mode. Otherwise it won't have any effect.
        """

        self._host.stop(self._result)

    @staticmethod
    def _get_speed_arg(speed: ReplaySpeed) -> str:
        """Prepare tcpreplay argument according to given replay speed modifier.

        Parameters
        ----------
        speed : ReplaySpeed
            Replay speed modifier.

        Returns
        -------
        str
            Tcpreplay argument.

        Raises
        ------
        TypeError
            If speed parameter has unsupported type.
        """

        if isinstance(speed, MultiplierSpeed):
            return f"--multiplier={speed.multiplier}"
        if isinstance(speed, MbpsSpeed):
            return f"--mbps={speed.mbps}"
        if isinstance(speed, PpsSpeed):
            return f"--pps={speed.pps}"
        if isinstance(speed, TopSpeed):
            return "--topspeed"
        raise TypeError("Unsupported speed type.")
