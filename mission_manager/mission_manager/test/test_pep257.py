# Copyright 2026 University of New Hampshire
# SPDX-License-Identifier: BSD-3-Clause


from ament_pep257.main import main
import pytest


@pytest.mark.linter
@pytest.mark.pep257
def test_pep257():
    rc = main(argv=['.', 'test', '--add-ignore', 'D213,D406,D407'])
    assert rc == 0, 'Found code style errors / warnings'
