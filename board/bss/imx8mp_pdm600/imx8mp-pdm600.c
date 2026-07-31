// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 BSS Hochspannungstechnik GmbH
 */

#include <asm/arch/sys_proto.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/mach-imx/boot_mode.h>
#include <asm-generic/gpio.h>
#include <env.h>
#include <init.h>
#include <fdt_support.h>
#include <jffs2/load_kernel.h>
#include <miiphy.h>
#include <mtd_node.h>
#include <mmc.h>

#include "../../phytec/common/imx8m_som_detection.h"

DECLARE_GLOBAL_DATA_PTR;

#define EEPROM_ADDR 0x51
#define EEPROM_ADDR_FALLBACK 0x59

#define BOARD_REV_GPIO_COUNT 5

/*
 * Board/carrier-card revisions, read from the pca9555 GPIO expanders in
 * board_late_init(). Cached here so ft_board_setup() does not have to
 * re-probe the I2C bus. Default to 0 (matches the compiled-in dts
 * fallback) if the expanders could not be read.
 */
static u32 base_hw_rev;
static u32 io_card_hw_rev;
static u32 power_card_hw_rev;

/**
 * read_revision_gpios() - Read a board/carrier-card revision code
 * @gpio_list_name: name of the "*-gpios" property on the root node
 *
 * Requests the 5 GPIOs listed in @gpio_list_name (root node property) and
 * combines their values into an integer (gpio 0 = bit 0, ... gpio 4 = bit 4).
 *
 * Return: revision code (0-31), or 0 if the GPIOs could not be requested.
 */
static u32 read_revision_gpios(const char *gpio_list_name)
{
	struct gpio_desc gpios[BOARD_REV_GPIO_COUNT];
	ofnode root = ofnode_path("/");
	int ret, count;

	if (!ofnode_valid(root))
		return 0;

	count = gpio_request_list_by_name_nodev(root, gpio_list_name, gpios,
						ARRAY_SIZE(gpios), GPIOD_IS_IN);
	if (count <= 0) {
		printf("%s: failed to request %s (%d)\n", __func__,
		       gpio_list_name, count);
		return 0;
	}

	if (count != ARRAY_SIZE(gpios)) {
		printf("%s: only got %d/%zu gpios for %s, assuming revision 0\n",
		       __func__, count, ARRAY_SIZE(gpios), gpio_list_name);
		gpio_free_list_nodev(gpios, count);
		return 0;
	}

	ret = dm_gpio_get_values_as_int(gpios, count);
	gpio_free_list_nodev(gpios, count);
	if (ret < 0) {
		printf("%s: failed to read %s (%d)\n", __func__, gpio_list_name,
		       ret);
		return 0;
	}

	return ret;
}

static void setup_board_revisions(void)
{
	base_hw_rev = read_revision_gpios("base-revision-gpios");
	io_card_hw_rev = read_revision_gpios("io-card-revision-gpios");
	power_card_hw_rev = read_revision_gpios("power-card-revision-gpios");

	/*
	 * Expose the revisions as env vars, so the bootscript can select
	 * matching device-tree overlays.
	 */
	env_set_hex("base_hw_rev", base_hw_rev);
	env_set_hex("io_card_hw_rev", io_card_hw_rev);
	env_set_hex("power_card_hw_rev", power_card_hw_rev);
}

int ft_board_setup(void *blob, struct bd_info *bd)
{
	u8 spi = phytec_get_imx8m_spi(NULL);

	fdt_setprop_u32(blob, 0, "base_hardware_revision", base_hw_rev);
	fdt_setprop_u32(blob, 0, "io_card_hardware_revision", io_card_hw_rev);
	fdt_setprop_u32(blob, 0, "power_card_hardware_revision",
			power_card_hw_rev);

	/* Do nothing more if no SPI is populated */
	if (!spi)
		return 0;

	static const struct node_info nodes[] = {
		{
			"jedec,spi-nor",
			MTD_DEV_TYPE_NOR,
		},
	};

	fdt_fixup_mtdparts(blob, nodes, ARRAY_SIZE(nodes));

	return 0;
}

static int setup_fec(void)
{
	struct iomuxc_gpr_base_regs *gpr =
		(struct iomuxc_gpr_base_regs *)IOMUXC_GPR_BASE_ADDR;

	/* Use 125M anatop REF_CLK1 for ENET1, not from external */
	clrsetbits_le32(&gpr->gpr[1], 0x2000, 0);

	return 0;
}

int board_init(void)
{
	int ret = phytec_eeprom_data_setup_fallback(NULL, 0, EEPROM_ADDR,
						    EEPROM_ADDR_FALLBACK);
	if (ret)
		printf("%s: EEPROM data init failed\n", __func__);

	setup_fec();

	return 0;
}

int board_mmc_get_env_dev(int devno)
{
	return devno;
}

static void setup_boot_device(void)
{
	struct mmc *mmc;
	u8 boot_part;

	switch (get_boot_device()) {
	case SD2_BOOT:
		env_set_ulong("mmcdev", 1);

		/*
		 * Booting from the SD card, so drop any eMMC boot0/boot1
		 * override that might still be present in a saved
		 * environment and fall back to u-boot's default env.
		 */
		env_set("emmc_boot_part", env_get_default("emmc_boot_part"));
		env_set("mmcroot", env_get_default("mmcroot"));

		break;
	case MMC3_BOOT:
		env_set_ulong("mmcdev", 2);

		mmc = find_mmc_device(2);
		if (mmc) {
			mmc_init(mmc);

			boot_part = (mmc->part_config >> 3) & 0x7;

			if (boot_part == 1) {
				env_set("emmc_boot_part", "boot0");
				env_set("mmcroot", "2");
			} else if (boot_part == 2) {
				env_set("emmc_boot_part", "boot1");
				env_set("mmcroot", "3");
			} else {
				/* Boot partition not enabled / user area: keep defaults */
				env_set("emmc_boot_part",
					env_get_default("emmc_boot_part"));
				env_set("mmcroot", env_get_default("mmcroot"));
			}
		}

		break;
	case USB_BOOT:
		printf("Detect USB boot. Will enter fastboot mode!\n");
		env_set_ulong("dofastboot", 1);
		break;
	default:
		break;
	}
}

int board_late_init(void)
{
	u8 spi = phytec_get_imx8m_spi(NULL);

	if (spi != 0 && spi != PHYTEC_EEPROM_INVAL)
		env_set("spiprobe", "sf probe");

	setup_board_revisions();

	setup_boot_device();

	return 0;
}

int board_phys_sdram_size(phys_size_t *size)
{
	if (!size)
		return -EINVAL;

	*size = get_ram_size((void *)PHYS_SDRAM,
			     PHYS_SDRAM_SIZE + PHYS_SDRAM_2_SIZE);

	return 0;
}
