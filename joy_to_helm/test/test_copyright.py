# Copyright 2026 University of New Hampshire
# SPDX-License-Identifier: BSD-3-Clause


from ament_copyright.main import main
import pytest


# Remove the `skip` decorator once the source file(s) have a copyright header
@pytest.mark.skip(reason='No copyright required for now')
@pytest.mark.copyright
@pytest.mark.linter
def test_copyright():
    rc = main(argv=['.', 'test'])
    assert rc == 0, 'Found errors'
