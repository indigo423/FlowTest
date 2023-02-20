"""
Author(s):  Jakub Rimek <jakub.rimek@progress.com>

Copyright: (C) 2022 Flowmon Networks a.s.
SPDX-License-Identifier: BSD-3-Clause

Orchestration configuration entity - AuthenticationCfg"""
from dataclasses import dataclass
from os.path import exists
from typing import Optional

from dataclass_wizard import YAMLWizard


class AuthenticationCfgException(Exception):
    """Exception raised by the AuthenticationCfg class"""


@dataclass
class AuthenticationCfg(YAMLWizard):
    """AuthenticationCfg configuration entity.

    - name is mandatory
    - contains the key_path OR pair of the username and the password
    """

    name: str
    key_path: Optional[str] = None
    username: Optional[str] = None
    password: Optional[str] = None
    ssh_agent: bool = False

    def check(self) -> None:
        """Check the configuration validity."""
        if self.key_path and self.password:
            raise AuthenticationCfgException(
                "AuthenticationCfg config file can not contain both of the key_path and the password."
            )
        if self.ssh_agent and (self.key_path or self.password):
            raise AuthenticationCfgException("Key path or password cannot be set if the ssh agent is used.")
        if not (self.ssh_agent or self.key_path or self.password):
            raise AuthenticationCfgException(
                "At least one authentication (SSH agent, key_path or password) must be present."
            )

        if self.key_path:
            self._check_key_path()

    def _check_key_path(self) -> None:
        if not exists(self.key_path):
            raise AuthenticationCfgException(f"Key file {self.key_path} was not found.")
