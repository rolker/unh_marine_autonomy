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

"""The four ways a store root is decided, and their precedence."""

from pathlib import Path

from marine_world_store.store_root import (
    ENV_VAR, resolve_store_root, resolve_store_root_verbose, StoreRootError,
)
import pytest


@pytest.fixture
def home(tmp_path):
    """Give the test a home of its own, so none reads the real one."""
    return {'HOME': str(tmp_path / 'home')}


def test_default_is_used_when_nothing_else_says(home):
    """Nothing given: the one documented default, expanded."""
    root = resolve_store_root_verbose(env=home, config_path=None)
    assert root.source == 'default'
    assert root.path == Path(home['HOME']) / 'data' / 'world'


def test_environment_beats_the_default(home, tmp_path):
    """The env var overrides the default."""
    home[ENV_VAR] = str(tmp_path / 'from-env')
    root = resolve_store_root_verbose(env=home)
    assert root.source == f'${ENV_VAR}'
    assert root.path == tmp_path / 'from-env'


def test_argument_beats_the_environment(home, tmp_path):
    """An explicit --store-root wins over the env var."""
    home[ENV_VAR] = str(tmp_path / 'from-env')
    root = resolve_store_root_verbose(str(tmp_path / 'from-arg'), env=home)
    assert root.source == 'argument'
    assert root.path == tmp_path / 'from-arg'


def test_config_file_beats_the_default(home, tmp_path):
    """store_root: in the config file is consulted before the default."""
    config = tmp_path / 'config.yaml'
    config.write_text(f'store_root: {tmp_path / "from-config"}\n')
    root = resolve_store_root_verbose(env=home, config_path=config)
    assert root.source.startswith('config ')
    assert root.path == tmp_path / 'from-config'


def test_environment_beats_the_config_file(home, tmp_path):
    """Precedence holds all the way down."""
    config = tmp_path / 'config.yaml'
    config.write_text(f'store_root: {tmp_path / "from-config"}\n')
    home[ENV_VAR] = str(tmp_path / 'from-env')
    root = resolve_store_root_verbose(env=home, config_path=config)
    assert root.path == tmp_path / 'from-env'


def test_absent_config_file_falls_through(home, tmp_path):
    """A missing config file is normal, not an error."""
    root = resolve_store_root_verbose(
        env=home, config_path=tmp_path / 'nope.yaml')
    assert root.source == 'default'


def test_config_file_without_the_key_falls_through(home, tmp_path):
    """So is one that says something else."""
    config = tmp_path / 'config.yaml'
    config.write_text('unrelated: 3\n')
    assert resolve_store_root_verbose(
        env=home, config_path=config).source == 'default'


def test_malformed_config_is_an_error_not_a_silent_default(home, tmp_path):
    """
    A config that cannot be read must never quietly become the default.

    Falling back would write a survey into the wrong tree because of a stray
    character, and nothing would say so.
    """
    config = tmp_path / 'config.yaml'
    config.write_text('store_root: [not, a, path\n')
    with pytest.raises(StoreRootError):
        resolve_store_root_verbose(env=home, config_path=config)


def test_config_with_an_empty_root_is_an_error(home, tmp_path):
    """An empty store_root: is a mistake, not a request for the default."""
    config = tmp_path / 'config.yaml'
    config.write_text('store_root: "   "\n')
    with pytest.raises(StoreRootError):
        resolve_store_root_verbose(env=home, config_path=config)


def test_empty_argument_is_an_error(home):
    """An empty --store-root is a bug in the caller, never the default."""
    with pytest.raises(StoreRootError):
        resolve_store_root_verbose('', env=home)


def test_empty_environment_variable_is_ignored(home):
    """An exported-but-empty env var means unset, as the shell means it."""
    home[ENV_VAR] = '   '
    assert resolve_store_root_verbose(env=home).source == 'default'


def test_tilde_user_form_is_refused(home):
    """``~someone/store`` would silently resolve to the wrong directory."""
    with pytest.raises(StoreRootError):
        resolve_store_root_verbose('~someone/store', env=home)


def test_result_is_absolute(home, monkeypatch, tmp_path):
    """A relative root would follow the working directory around."""
    monkeypatch.chdir(tmp_path)
    assert resolve_store_root_verbose('relative/store', env=home).path \
        .is_absolute()


def test_store_root_is_path_like(home):
    """A StoreRoot can be used wherever a path is accepted."""
    root = resolve_store_root_verbose(env=home)
    assert Path(root) == root.path


def test_plain_helper_returns_the_path(home):
    """The common form returns just the path."""
    assert resolve_store_root(env=home) == \
        resolve_store_root_verbose(env=home).path


def test_missing_home_is_reported_not_guessed(tmp_path):
    """With no HOME there is no ``~`` to expand; say so rather than guess."""
    with pytest.raises(StoreRootError):
        resolve_store_root_verbose(env={})
