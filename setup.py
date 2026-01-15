import setuptools.command.build
from setuptools import Command, setup
import wheel.bdist_wheel

import os
import os.path
import subprocess

class MakeCommand(Command):
    """Class of `setuptools`/`distutils` commands which invoke a `make` program.

    GNU Make (http://www.gnu.org/software/make) is currently assumed for providing `make`. The program is invoked in a manner where it changes the working directory to the build directory advertised for the command (utilizing `self.set_undefined_options` as hinted at by [the documentation](http://setuptools.pypa.io/en/latest/userguide/extension.html) which defers to `help(setuptools.command.build.SubCommand)`).

    The command is expected to produce build artefacts which will be added to the wheel.
    """
    def finalize_options(self) -> None:
        pass

    def initialize_options(self) -> None:
        pass

    def run(self, *args, **kwargs) -> None:
        subprocess.check_call(('make',))

class BuildCommand(setuptools.command.build.build):
    sub_commands = [ ('build_make', None) ] + setuptools.command.build.build.sub_commands # Makes the `build_make` command a sub-command of the `build` command, which has the effect of the former being invoked when the latter is invoked (which is invoked in turn when the wheel must be built, through the `bdist_wheel` command)

class NoABIBdistWheel(wheel.bdist_wheel.bdist_wheel):
    def get_tag(self):
        python, abi, plat = super().get_tag()
        return 'py3', 'none', plat

setup(cmdclass={ 'build': BuildCommand, 'build_make': MakeCommand, 'bdist_wheel': NoABIBdistWheel }, data_files=(('bin', ('mock-large-files-fuse',)),), has_ext_modules=lambda: True)
