# hi1102/ — legacy stack, NOT ported

Mirror of vendor `drivers/connectivity/hisi/` (`_PRE_PRODUCT_VERSION=1102`),
the driver stack the retail kernel actually selects (`CONFIG_CONNECTIVITY_
HI1102=y`).  Kept as source-only reference.

**This tree does not build on Linux 7.2.5**: it still uses APIs removed
from mainline (raw `net_device.wireless_handlers`, old wireless ext, the
pre-cfg80211 ioctl paths).  A try-build fails immediately in
`wifi/wal/wal_linux_*.c`.

It is reachable in the build only via
`CONFIG_HISI_CONNECTIVITY_HI1102_LEGACY` (default n, mutually exclusive
with the ported stack) — enable it solely to work on porting.

The board's Hi1102 chip is driven by the ported stack in `../hi1102a/`,
which loads the same retail firmware (`/vendor/firmware/*`,
`Hi1102 V100R001C03B07SP02`) at runtime; see `../Kconfig` and the board
DTS node `hi110x` (`hi110x,subchip_type = "hi1102"`).
