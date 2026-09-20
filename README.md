# AN758x U-Boot

U-Boot for Airoha AN7581/AN7583 with a bilingual Web recovery interface.

面向 Airoha AN7581/AN7583 的 U-Boot，集成中英文 Web 恢复页面。

Hold Reset about one second after power-on to enter Web recovery.

上电约一秒后按住 Reset，即可进入 Web 恢复页面。

## Supported devices / 支持机型

| Target / 构建目标 | Device / 设备 | SoC | Storage / 存储 |
| --- | --- | --- | --- |
| `hg5382a` | FiberHome HG5382A | AN7581 | Parallel NAND |
| `hg5585f-ct` | FiberHome HG5585F CT | AN7581 | Parallel NAND |
| `hg5585f-cu` | FiberHome HG5585F CU | AN7581 | Parallel NAND |
| `xg2010g` | Gemtek XG2010G | AN7581 | SPI NAND |
| `zn504xg-d` | ZNXT ZN504XG-D | AN7581 | SPI NAND |
| `zn515xg-d` | ZNXT ZN515XG-D | AN7581 | SPI NAND |
| `ung00a` | UnionMan UNG00A | AN7581 | SPI NAND |
| `xg-040g-md` | Nokia XG-040G-MD | AN7581 | SPI NAND |
| `xg-040g-tf` | Nokia XG-040G-TF | AN7581 | SPI NAND |
| `xg-040g-mf` | Nokia XG-040G-MF | AN7583 | SPI NAND |

## Build / 构建

```sh
export CROSS_COMPILE=/path/to/aarch64-linux-musl-
export ARM32_CROSS_COMPILE=/path/to/arm-none-eabi-
export MBEDTLS_DIR=/path/to/mbedtls-3.4.1
export BUILD_JOBS=$(nproc)

./scripts/build-an758x.sh hg5585f-ct
```
Artifacts are written to `output/<target>/` / 产物位于 `output/<target>/`

## Installation / 刷入

1. Back up every MTD partition and UBI volume.<br>
   备份全部 MTD 分区和 UBI 卷。
2. Write one BL2 image at the beginning of NAND:<br>
   在 NAND 开头写入以下一种 BL2 镜像：

   - Write `*-firstblock.bin` from NAND offset `0x0`.<br>
     从 NAND `0x0` 偏移写入 `*-firstblock.bin`。
   - Leave the first `0x800` bytes erased and write `*-preloader.bin` from
     NAND offset `0x800`.<br>
     保持前 `0x800` 字节为擦除态，从 NAND `0x800` 偏移写入
     `*-preloader.bin`。
3. Boot the device. When BL2 requests a FIP, press `x` and send
   `*-bl31-u-boot.fip` through XMODEM.<br>
   启动设备；BL2 请求 FIP 时按 `x`，通过 XMODEM 发送
   `*-bl31-u-boot.fip`，临时进入 U-Boot。
4. Connect Ethernet and open `http://192.168.0.1/`.<br>
   连接网线并打开 `http://192.168.0.1/`。
5. For the first installation, select **Rebuild UBI / 重建 UBI**. This erases
   every volume in the UBI partition.<br>
   首次安装选择 **重建 UBI**，该操作会清空 UBI 分区中的全部卷。
6. Write `*-bl31-u-boot.fip`, restore the board-data volumes, then upload the
   sysupgrade image.<br>
   写入 `*-bl31-u-boot.fip`，恢复板级数据卷，再上传 sysupgrade 镜像。
7. Select **Boot system / 启动系统**.<br>
   选择 **启动系统**。

Subsequent upgrades use **Install system / 刷写系统** directly.

后续升级直接使用 **刷写系统**。

## TF-A sources / TF-A 来源

- [Ansuel/atf-airoha](https://github.com/Ansuel/atf-airoha)
- [mkshevetskiy/atf-airoha](https://github.com/mkshevetskiy/atf-airoha)
- [Yuzhii0718/atf-airoha](https://github.com/Yuzhii0718/atf-airoha)
