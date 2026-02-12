"""Setup configuration for mission_manager package."""

from glob import glob
import os

from setuptools import find_packages, setup

package_name = 'mission_manager'

setup(
    name=package_name,
    version='0.0.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        (os.path.join('share', package_name, 'launch'),
         glob(os.path.join('launch', '*launch.[pxy][yma]*'))),
    ],
    install_requires=['setuptools'],
    tests_require=['pytest'],
    zip_safe=True,
    maintainer='Roland Arsenault',
    maintainer_email='roland@ccom.unh.edu',
    description='Mission manager for marine autonomy',
    license='BSD-2-Clause',
    entry_points={
        'console_scripts': [
            (
                'mission_manager = '
                'mission_manager.mission_manager:main'
            ),
            (
                'multibeam_coverage_adapter = '
                'mission_manager.multibeam_coverage_adapter:main'
            ),
        ],
    },
)
