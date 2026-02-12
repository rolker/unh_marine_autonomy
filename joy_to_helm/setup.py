from glob import glob
import os

from setuptools import find_packages, setup

package_name = 'joy_to_helm'

setup(
    name=package_name,
    version='0.0.0',
    packages=find_packages(exclude=['test']),
    install_requires=['setuptools'],
    tests_require=['pytest'],
    zip_safe=True,
    maintainer='Roland Arsenault',
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        (os.path.join('share', package_name, 'launch'),
         glob(os.path.join('launch', '*launch.[pxy][yma]*'))),
    ],
    entry_points={
        'console_scripts': [
            'joy_to_helm = joy_to_helm.joy_to_helm:main',
        ],
    },
)
