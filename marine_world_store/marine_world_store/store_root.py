# Copyright 2026 University of New Hampshire
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#
#    * Neither the name of the University of New Hampshire nor the names of its
#      contributors may be used to endorse or promote products derived from
#      this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

"""
Resolve the world store's root directory.

The store root is a **parameter** (design draft section 2): it is never a
literal in code, because the prototype, the tests and a replica all build under
a different root, and a hard-coded path is exactly what stops them.

The one documented default lives in :data:`_DEFAULT_ROOT` below and nowhere
else. ``test/test_no_literal_store_root.py`` greps the repository for the
literal and fails on any other occurrence, so "never hard-coded" is mechanical
rather than aspirational.

Precedence, highest first:

1. an explicit argument (a tool's ``--store-root``),
2. the ``WORLD_STORE_ROOT`` environment variable,
3. ``store_root:`` in ``~/.config/marine_world_store/config.yaml``,
4. :data:`_DEFAULT_ROOT`.
"""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
from typing import Mapping, Optional

#: The single documented default. THE ONLY PLACE THIS LITERAL MAY APPEAR.
_DEFAULT_ROOT = '~/data/world'

#: Environment variable that overrides the default.
ENV_VAR = 'WORLD_STORE_ROOT'

#: Config file consulted when neither an argument nor the env var is given.
CONFIG_RELATIVE_PATH = Path('marine_world_store') / 'config.yaml'

#: Key read from that file.
CONFIG_KEY = 'store_root'


class StoreRootError(RuntimeError):
    """The store root could not be resolved from the inputs given."""


@dataclass(frozen=True)
class StoreRoot:
    """
    A resolved root and where it came from.

    ``source`` is provenance, not decoration: a tool that writes into the
    store should be able to say which of the four mechanisms decided where it
    wrote, and a test asserts on it rather than on a path that differs per
    host.
    """

    path: Path
    source: str

    def __fspath__(self) -> str:
        """Let a :class:`StoreRoot` be used wherever a path is accepted."""
        return str(self.path)


def _config_path(env: Mapping[str, str]) -> Optional[Path]:
    """
    Where the optional config file lives, honouring ``XDG_CONFIG_HOME``.

    ``None`` when the environment names no home at all -- there is then no
    config file to consult, which is a fall-through, not an error.
    """
    xdg = env.get('XDG_CONFIG_HOME', '').strip()
    if xdg:
        base = Path(xdg)
    else:
        home = env.get('HOME', '').strip()
        if not home:
            return None
        base = Path(home) / '.config'
    return base / CONFIG_RELATIVE_PATH


def _from_config(path: Path) -> Optional[str]:
    """
    Read ``store_root:`` from ``path``.

    A missing file is normal and falls through. A file that exists but cannot
    be parsed is an error: silently falling back to the default would write a
    survey into the wrong tree because of a stray tab.
    """
    if not path.is_file():
        return None
    import yaml  # local: keeps the module importable where PyYAML is absent
    try:
        document = yaml.safe_load(path.read_text())
    except yaml.YAMLError as exc:
        raise StoreRootError(f'{path}: not valid YAML: {exc}') from exc
    if document is None:
        return None
    if not isinstance(document, dict):
        raise StoreRootError(
            f'{path}: expected a mapping with a {CONFIG_KEY!r} key, '
            f'got {type(document).__name__}')
    value = document.get(CONFIG_KEY)
    if value is None:
        return None
    if not isinstance(value, str) or not value.strip():
        raise StoreRootError(
            f'{path}: {CONFIG_KEY!r} must be a non-empty string')
    return value


def resolve_store_root_verbose(
    cli_arg: Optional[str] = None,
    *,
    env: Optional[Mapping[str, str]] = None,
    config_path: Optional[Path] = None,
) -> StoreRoot:
    """
    Resolve the store root and report which mechanism decided it.

    :param cli_arg: the value of a tool's ``--store-root`` flag, or ``None``.
    :param env: environment mapping; defaults to ``os.environ`` (injected in
        tests so no test depends on the developer's own environment).
    :param config_path: override for the config file location.
    :returns: the resolved :class:`StoreRoot`, expanded but **not** created.
    :raises StoreRootError: on an empty ``cli_arg`` or an unreadable config.
    """
    env = os.environ if env is None else env

    if cli_arg is not None:
        if not cli_arg.strip():
            raise StoreRootError(
                'an empty --store-root is not a store root; omit the flag to '
                'fall back to the environment, the config file or the default')
        return StoreRoot(_expand(cli_arg, env), 'argument')

    from_env = env.get(ENV_VAR, '').strip()
    if from_env:
        return StoreRoot(_expand(from_env, env), f'${ENV_VAR}')

    path = _config_path(env) if config_path is None else config_path
    if path is not None:
        from_config = _from_config(path)
        if from_config is not None:
            return StoreRoot(_expand(from_config, env), f'config {path}')

    return StoreRoot(_expand(_DEFAULT_ROOT, env), 'default')


def resolve_store_root(
    cli_arg: Optional[str] = None,
    *,
    env: Optional[Mapping[str, str]] = None,
    config_path: Optional[Path] = None,
) -> Path:
    """
    Resolve the store root.

    The common form of :func:`resolve_store_root_verbose`; see it for the
    precedence rules and the arguments.
    """
    return resolve_store_root_verbose(
        cli_arg, env=env, config_path=config_path).path


def _expand(value: str, env: Mapping[str, str]) -> Path:
    """
    Expand ``~`` against ``env``'s HOME and return an absolute path.

    ``Path.expanduser`` reads the process environment directly, which would
    make an injected ``env`` a half-truth in tests.
    """
    text = value
    if text.startswith('~') and not text.startswith(('~/', '~\\')) \
            and text != '~':
        raise StoreRootError(
            f'{value!r}: the ~user form is not supported; name the path or '
            'use ~/ for this user')
    if text.startswith('~'):
        home = env.get('HOME')
        if not home:
            raise StoreRootError(
                f'cannot expand {value!r}: HOME is not set. Pass an absolute '
                f'--store-root or set ${ENV_VAR} to one.')
        text = home + text[1:]
    return Path(text).absolute()
