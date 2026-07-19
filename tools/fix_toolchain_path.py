"""修正 pioarduino 在 Windows 下安装 RISC-V 工具链后的嵌套目录。"""

from os.path import isdir, join

Import("env")

packages_dir = env.subst("$PROJECT_PACKAGES_DIR")
nested_bin = join(
    packages_dir,
    "toolchain-riscv32-esp",
    "riscv32-esp-elf",
    "bin",
)

if isdir(nested_bin):
    env.PrependENVPath("PATH", nested_bin)
