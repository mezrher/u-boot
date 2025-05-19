/*
 * Copyright 2019-2021 Digi International Inc
 * Copyright 2018 NXP
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <malloc.h>
#include <errno.h>
#include <asm/io.h>
#include <micrel.h>
#include <miiphy.h>
#include <netdev.h>
#include <asm/mach-imx/iomux-v3.h>
#include <asm-generic/gpio.h>
#include <fsl_esdhc_imx.h>
#include <mmc.h>
#ifdef CONFIG_IMX8MM
#include <asm/arch/imx8mm_pins.h>
#elif defined CONFIG_IMX8MN
#include <asm/arch/imx8mn_pins.h>
#endif
#include <asm/arch/sys_proto.h>
#include <asm/mach-imx/boot_mode.h>
#include <asm/mach-imx/gpio.h>
#include <asm/mach-imx/mxc_i2c.h>
#include <asm/arch/clock.h>
#include <spl.h>
#include <asm/mach-imx/dma.h>
#include <power/pmic.h>
#include "../../freescale/common/tcpc.h"
#include <usb.h>
#include <linux/ctype.h>




#include "../ccimx8/ccimx8.h"
#include "../common/carrier_board.h"
#include "../common/helper.h"
#include "../common/mca_registers.h"
#include "../common/mca.h"
#include "../common/tamper.h"
#include "../common/trustfence.h"

DECLARE_GLOBAL_DATA_PTR;

unsigned int board_version = CARRIERBOARD_VERSION_UNDEFINED;
unsigned int board_id = CARRIERBOARD_ID_UNDEFINED;

#define UART_PAD_CTRL	(PAD_CTL_DSE6 | PAD_CTL_FSEL1)
#define WDOG_PAD_CTRL	(PAD_CTL_DSE6 | PAD_CTL_ODE | PAD_CTL_PUE | PAD_CTL_PE)

static iomux_v3_cfg_t const uart_pads[] = {
#ifdef CONFIG_IMX8MM
	IMX8MM_PAD_SAI2_RXC_UART1_RX | MUX_PAD_CTRL(UART_PAD_CTRL),
	IMX8MM_PAD_SAI2_RXFS_UART1_TX | MUX_PAD_CTRL(UART_PAD_CTRL),
#elif defined CONFIG_IMX8MN
	IMX8MN_PAD_SAI2_RXC__UART1_DCE_RX | MUX_PAD_CTRL(UART_PAD_CTRL),
	IMX8MN_PAD_SAI2_RXFS__UART1_DCE_TX | MUX_PAD_CTRL(UART_PAD_CTRL),
#endif
};

static iomux_v3_cfg_t const wdog_pads[] = {
#ifdef CONFIG_IMX8MM
	IMX8MM_PAD_GPIO1_IO02_WDOG1_WDOG_B  | MUX_PAD_CTRL(WDOG_PAD_CTRL),
#elif defined CONFIG_IMX8MN
	IMX8MN_PAD_GPIO1_IO02__WDOG1_WDOG_B  | MUX_PAD_CTRL(WDOG_PAD_CTRL),
#endif
};

#if defined(CONFIG_CONSOLE_ENABLE_GPIO) && !defined(CONFIG_SPL_BUILD)
#define GPI_PAD_CTRL	(PAD_CTL_DSE6 | PAD_CTL_FSEL2 | PAD_CTL_PE)

static iomux_v3_cfg_t const ext_gpios_pads[] = {
#ifdef CONFIG_IMX8MM
	IMX8MM_PAD_GPIO1_IO10_GPIO1_IO10 | MUX_PAD_CTRL(GPI_PAD_CTRL),
	IMX8MM_PAD_GPIO1_IO11_GPIO1_IO11 | MUX_PAD_CTRL(GPI_PAD_CTRL),
	IMX8MM_PAD_GPIO1_IO13_GPIO1_IO13 | MUX_PAD_CTRL(GPI_PAD_CTRL),
	IMX8MM_PAD_GPIO1_IO14_GPIO1_IO14 | MUX_PAD_CTRL(GPI_PAD_CTRL),
#elif defined CONFIG_IMX8MN
	IMX8MN_PAD_GPIO1_IO10__GPIO1_IO10 | MUX_PAD_CTRL(GPI_PAD_CTRL),
	IMX8MN_PAD_GPIO1_IO11__GPIO1_IO11 | MUX_PAD_CTRL(GPI_PAD_CTRL),
	IMX8MN_PAD_GPIO1_IO13__GPIO1_IO13 | MUX_PAD_CTRL(GPI_PAD_CTRL),
	IMX8MN_PAD_GPIO1_IO14__GPIO1_IO14 | MUX_PAD_CTRL(GPI_PAD_CTRL),
#endif
};
#endif /* CONFIG_CONSOLE_ENABLE_GPIO && !CONFIG_SPL_BUILD */

int board_early_init_f(void)
{
	struct wdog_regs *wdog = (struct wdog_regs *)WDOG1_BASE_ADDR;

	imx_iomux_v3_setup_multiple_pads(wdog_pads, ARRAY_SIZE(wdog_pads));

	set_wdog_reset(wdog);

	imx_iomux_v3_setup_multiple_pads(uart_pads, ARRAY_SIZE(uart_pads));
#ifdef CONFIG_CONSOLE_DISABLE
	gd->flags |= (GD_FLG_DISABLE_CONSOLE | GD_FLG_SILENT);
#endif /* CONFIG_CONSOLE_DISABLE */

	/* Init UART0 clock */
	init_uart_clk(0);

	return 0;
}

#ifdef CONFIG_BOARD_POSTCLK_INIT
int board_postclk_init(void)
{
	/* TODO */
	return 0;
}
#endif

#ifdef CONFIG_OF_BOARD_SETUP
int ft_board_setup(void *blob, bd_t *bd)
{
	fdt_fixup_ccimx8(blob);
	fdt_fixup_carrierboard(blob);

	return 0;
}
#endif

void platform_default_environment(void)
{
	som_default_environment();
}

int board_late_init(void)
{
	/* SOM late init */
	ccimx8_late_init();

	/* Set default dynamic variables */
	platform_default_environment();

#ifdef CONFIG_HAS_TRUSTFENCE
	copy_dek();
	copy_spl_dek();
#endif

	return 0;
}

#ifdef CONFIG_FEC_MXC
static void enet_device_phy_reset(void)
{
	struct gpio_desc desc;
	struct udevice *dev = NULL;
	int ret;

	ret = dm_gpio_lookup_name("gpio5_3", &desc);
	if (ret)
		return;

	ret = dm_gpio_request(&desc, "fec1_reset");
	if (ret)
		return;

	dm_gpio_set_dir_flags(&desc, GPIOD_IS_OUT);
	dm_gpio_set_value(&desc, 0);
	udelay(50);
	dm_gpio_set_value(&desc, 1);
	dm_gpio_free(dev, &desc);

	udelay(10);
}
/*@Mezrher*/

int board_phy_config(struct phy_device *phydev)
{
	int tmp;

	switch(ksz9xx1_phy_get_id(phydev) & MII_KSZ9x31_SILICON_REV_MASK) {
	case PHY_ID_KSZ9031:
		/*
		* The PHY adds 1.2ns for the RXC and 0ns for TXC clock by default. The MAC
		* and the layout don't add a skew between clock and data.
		* Add 0.3ns for the RXC path and 0.96 + 0.42 ns (1.38 ns) for the TXC path
		* to get the required clock skews.
		*/
		/* control data pad skew - devaddr = 0x02, register = 0x04 */
		ksz9031_phy_extended_write(phydev, 0x02,
					MII_KSZ9031_EXT_RGMII_CTRL_SIG_SKEW,
					MII_KSZ9031_MOD_DATA_NO_POST_INC, 0x0070);
		/* rx data pad skew - devaddr = 0x02, register = 0x05 */
		ksz9031_phy_extended_write(phydev, 0x02,
					MII_KSZ9031_EXT_RGMII_RX_DATA_SKEW,
					MII_KSZ9031_MOD_DATA_NO_POST_INC, 0x7777);
		/* tx data pad skew - devaddr = 0x02, register = 0x06 */
		ksz9031_phy_extended_write(phydev, 0x02,
					MII_KSZ9031_EXT_RGMII_TX_DATA_SKEW,
					MII_KSZ9031_MOD_DATA_NO_POST_INC, 0x0000);
		/* gtx and rx clock pad skew - devaddr = 0x02, register = 0x08 */
		ksz9031_phy_extended_write(phydev, 0x02,
					MII_KSZ9031_EXT_RGMII_CLOCK_SKEW,
					MII_KSZ9031_MOD_DATA_NO_POST_INC, 0x03f4);
		break;
	case PHY_ID_KSZ9131:
	default:
		/* read rxc dll control - devaddr = 0x2, register = 0x4c */
		tmp = ksz9031_phy_extended_read(phydev, 0x02,
					MII_KSZ9131_EXT_RGMII_2NS_SKEW_RXDLL,
					MII_KSZ9031_MOD_DATA_NO_POST_INC);
		/* disable rxdll bypass (enable 2ns skew delay on RXC) */
		tmp &= ~MII_KSZ9131_RXTXDLL_BYPASS;
		/* rxc data pad skew 2ns - devaddr = 0x02, register = 0x4c */
		tmp = ksz9031_phy_extended_write(phydev, 0x02,
					MII_KSZ9131_EXT_RGMII_2NS_SKEW_RXDLL,
					MII_KSZ9031_MOD_DATA_NO_POST_INC, tmp);
		/* read txc dll control - devaddr = 0x02, register = 0x4d */
		tmp = ksz9031_phy_extended_read(phydev, 0x02,
					MII_KSZ9131_EXT_RGMII_2NS_SKEW_TXDLL,
					MII_KSZ9031_MOD_DATA_NO_POST_INC);
		/* disable txdll bypass (enable 2ns skew delay on TXC) */
		tmp &= ~MII_KSZ9131_RXTXDLL_BYPASS;
		/* rxc data pad skew 2ns - devaddr = 0x02, register = 0x4d */
		tmp = ksz9031_phy_extended_write(phydev, 0x02,
					MII_KSZ9131_EXT_RGMII_2NS_SKEW_TXDLL,
					MII_KSZ9031_MOD_DATA_NO_POST_INC, tmp);
		break;
	}

	if (phydev->drv->config)
		phydev->drv->config(phydev);
	return 0;
}

static int setup_fec(void)
{
	struct iomuxc_gpr_base_regs *const iomuxc_gpr_regs
		= (struct iomuxc_gpr_base_regs *) IOMUXC_GPR_BASE_ADDR;
	struct gpio_desc enet_pwr;
	int ret;

	/* Power up the PHY */
	ret = dm_gpio_lookup_name("gpio5_4", &enet_pwr);
	if (ret)
		return -1;

	ret = dm_gpio_request(&enet_pwr, "fec1_pwr");
	if (ret)
		return -1;

	dm_gpio_set_dir_flags(&enet_pwr, GPIOD_IS_OUT);
	dm_gpio_set_value(&enet_pwr, 1);
	mdelay(1);	/* PHY power up time */

	/* Reset the PHY */
	enet_device_phy_reset();

	/* Use 125M anatop REF_CLK1 for ENET1, not from external */
	clrsetbits_le32(&iomuxc_gpr_regs->gpr[1],
			IOMUXC_GPR_GPR1_GPR_ENET1_TX_CLK_SEL_MASK, 0);
	return set_clk_enet(ENET_125MHZ);
}
#endif

int board_usb_init(int index, enum usb_init_type init)
{
	imx8m_usb_power(index, true);

	return 0;
}

int board_usb_cleanup(int index, enum usb_init_type init)
{
	imx8m_usb_power(index, false);

	return 0;
}

int board_ehci_usb_phy_mode(struct udevice *dev)
{
	return USB_INIT_DEVICE;
}

static int board_power_led_init(void)
{
	/* MCA_IO13 is connected to POWER_LED */
	const char *name = "MCA-GPIO_13";
	struct gpio_desc desc;
	int ret;

	ret = dm_gpio_lookup_name(name, &desc);
	if (ret)
		goto error;

	ret = dm_gpio_request(&desc, "Power LED");
	if (ret)
		goto error;

	ret = dm_gpio_set_dir_flags(&desc, GPIOD_IS_OUT);
	if (ret)
		goto errfree;

	ret = dm_gpio_set_value(&desc, 1);
	if (ret)
		goto errfree;

	return 0;
errfree:
	dm_gpio_free(NULL, &desc);
error:
	return ret;
}

int board_init(void)
{
#if defined(CONFIG_CONSOLE_ENABLE_GPIO) && !defined(CONFIG_SPL_BUILD)
	const char *ext_gpios[] = {
		"GPIO1_10",	/* J46.3 */
		"GPIO1_11",	/* J46.5 */
		"GPIO1_13",	/* J46.7 */
		"GPIO1_14",	/* J46.9 */
	};
	const char *ext_gpio_name = ext_gpios[CONFIG_CONSOLE_ENABLE_GPIO_NR];
	imx_iomux_v3_setup_multiple_pads(ext_gpios_pads,
					 ARRAY_SIZE(ext_gpios_pads));
	if (console_enable_gpio(ext_gpio_name))
		gd->flags &= ~(GD_FLG_DISABLE_CONSOLE | GD_FLG_SILENT);
#endif /* CONFIG_CONSOLE_ENABLE_GPIO && !CONFIG_SPL_BUILD */

	/* SOM init */
	ccimx8_init();

	board_power_led_init();

#ifdef CONFIG_MXC_SPI
	setup_spi();
#endif

#ifdef CONFIG_FEC_MXC
	setup_fec();
#endif

	return 0;
}

int mmc_map_to_kernel_blk(int devno)
{
	return devno;
}

int checkboard(void)
{
	board_version = get_carrierboard_version();
	board_id = get_carrierboard_id();

	print_som_info();
	print_carrierboard_info();
	print_bootinfo();

	return 0;
}

#ifdef CONFIG_FSL_FASTBOOT
#ifdef CONFIG_ANDROID_RECOVERY
int is_recovery_key_pressing(void)
{
	return 0; /*TODO*/
}
#endif /*CONFIG_ANDROID_RECOVERY*/
#endif /*CONFIG_FSL_FASTBOOT*/
