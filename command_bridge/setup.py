from setuptools import find_packages, setup

package_name = 'command_bridge'

setup(
    name=package_name,
    version='0.0.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
    ],
    install_requires=['setuptools'],
    tests_require=['pytest'],
    zip_safe=True,
    maintainer='Roland Arsenault',
    maintainer_email='roland@ccom.unh.edu',
    description='Command bridge for project11',
    license='BSD-2-Clause',
    entry_points={
        'console_scripts': [
          'command_bridge_receiver = command_bridge.command_bridge_receiver:main',
          'command_bridge_sender = command_bridge.command_bridge_sender:main',
        ],
    },
)
