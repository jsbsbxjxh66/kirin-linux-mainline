#include <huawei_platform/power/battery_type_identify.h>
#include "onewire_phy_pl061_gpio.h"

#define HWLOG_TAG HW_ONEWIRE_PHY
HWLOG_REGIST();

static struct platform_device *phy_pdev;

/*
 * The onewire line is driven through the gpiod consumer API.  The vendor
 * mixed raw PL061 register pokes with the GPIO driver's private struct
 * (struct pl061_gpio and its spinlock), neither of which mainline exports.
 * gpiod_direction_output()/gpiod_direction_input() perform the same
 * read-modify-write of the single direction bit, so the rest of the bank is
 * left untouched exactly as before.
 *
 * DT binding: onewire-gpios = <&gpio_chip pin flags>.
 */
static struct gpio_desc *ow_gpio;
static DEFINE_SPINLOCK(ow_lock);

static onewire_time_request *ow_trq;

static const char gpio_name[] = "onewire_phy_pl061_gpio";

/* private delay -- same as kernel 4.4 __delay -- using it carefully */
static void __hw_delay(unsigned long cycles)
{
	cycles_t start = get_cycles();

	while ((get_cycles() - start) < cycles) ;
}

/* drive the line low or high (direction becomes output) */
static void gpio_direction_output_unsafe(unsigned char value)
{
	gpiod_direction_output(ow_gpio, !!value);
}

/* release the line (direction becomes input) */
static void gpio_direction_input_unsafe(void)
{
	gpiod_direction_input(ow_gpio);
}

static void gpio_set_value_unsafe(unsigned char value)
{
	gpiod_set_value(ow_gpio, !!value);
}

static int gpio_get_value_unsafe(void)
{
	return gpiod_get_value(ow_gpio);
}

/* onewire reset operation */
static unsigned char onewire_reset(void)
{
	unsigned char presence;
	unsigned long flags;

	spin_lock_irqsave(&ow_lock, flags);
	gpio_direction_output_unsafe(LOW_VOLTAGE);
	__hw_delay(ow_trq->reset_init_low_cycles);
	gpio_set_value_unsafe(HIGH_VOLTAGE);
	gpio_direction_input_unsafe();
	__hw_delay(ow_trq->reset_slave_response_delay_cycles);
	presence = gpio_get_value_unsafe();
	__hw_delay(ow_trq->reset_hold_high_cycles);
	spin_unlock_irqrestore(&ow_lock, flags);
	return presence;
}

/* onewire bit operation */
static inline void onewire_read_bit(unsigned char *value)
{
	gpio_direction_output_unsafe(LOW_VOLTAGE);
	__hw_delay(ow_trq->read_init_low_cycles);
	gpio_direction_input_unsafe();
	*value = (*value) | (gpio_get_value_unsafe() << SHIFT_7);
	__hw_delay(ow_trq->read_residual_cycles);
}

static inline void onewire_write_bit(const unsigned char bitval)
{
	gpio_direction_output_unsafe(LOW_VOLTAGE);
	__hw_delay(ow_trq->write_init_low_cycles);
	gpio_set_value_unsafe(bitval);
	__hw_delay(ow_trq->write_hold_cycles);
	gpio_set_value_unsafe(HIGH_VOLTAGE);
	__hw_delay(ow_trq->write_residual_cycles);
}

/* onewire byte operation */
static unsigned char onewire_read_byte(void)
{
	unsigned char value = 0;
	unsigned long flags;

	spin_lock_irqsave(&ow_lock, flags);
	onewire_read_bit(&value);
	value >>= SHIFT_1;
	onewire_read_bit(&value);
	value >>= SHIFT_1;
	onewire_read_bit(&value);
	value >>= SHIFT_1;
	onewire_read_bit(&value);
	value >>= SHIFT_1;
	onewire_read_bit(&value);
	value >>= SHIFT_1;
	onewire_read_bit(&value);
	value >>= SHIFT_1;
	onewire_read_bit(&value);
	value >>= SHIFT_1;
	onewire_read_bit(&value);
	/* set gpio output high for powering when eeprom programing or hmac computation */
	gpio_direction_output_unsafe(HIGH_VOLTAGE);
	spin_unlock_irqrestore(&ow_lock, flags);
	return value;
}

static void onewire_write_byte(const unsigned char val)
{
	unsigned long flags;

	spin_lock_irqsave(&ow_lock, flags);
	onewire_write_bit(val & BIT_0);
	onewire_write_bit(val & BIT_1);
	onewire_write_bit(val & BIT_2);
	onewire_write_bit(val & BIT_3);
	onewire_write_bit(val & BIT_4);
	onewire_write_bit(val & BIT_5);
	onewire_write_bit(val & BIT_6);
	onewire_write_bit(val & BIT_7);
	spin_unlock_irqrestore(&ow_lock, flags);
}

/* wait for onewire IC process  */
static void onewire_wait_for_ic(unsigned int ms)
{
	msleep(ms);
}

/* set up onewire time request */
static void onewire_set_time_request(onewire_time_request *ic_ow_trq)
{
	if (ic_ow_trq) {
		ow_trq = ic_ow_trq;
		hwlog_info("Timer Frequence is %u, Hz is %u, Loops per jiffy is %lu\n",
			   arch_timer_get_cntfrq(), HZ, loops_per_jiffy);
		hwlog_info("reset_init_low_cycles = %ucycles(%uns),"
			   "reset_slave_response_delay_cycles = %ucycles(%uns),"
			   "reset_hold_high_cycles = %ucycles(%uns)\n",
			   ow_trq->reset_init_low_cycles, ow_trq->reset_init_low_ns,
			   ow_trq->reset_slave_response_delay_cycles, ow_trq->reset_slave_response_delay_ns,
			   ow_trq->reset_hold_high_cycles, ow_trq->reset_hold_high_ns);
		hwlog_info("write_init_low_cycles = %ucycles(%uns),"
			   "write_hold_cycles = %ucycles(%uns),"
			   "write_residual_cycles = %ucycles(%uns)\n",
			   ow_trq->write_init_low_cycles, ow_trq->write_init_low_ns,
			   ow_trq->write_hold_cycles, ow_trq->write_hold_ns,
			   ow_trq->write_residual_cycles, ow_trq->write_residual_ns);
		hwlog_info("read_init_low_cycles = %ucycles(%uns),"
			   "read_residual_cycles = %ucycles(%uns)\n",
			   ow_trq->read_init_low_cycles, ow_trq->read_init_low_ns,
			   ow_trq->read_residual_cycles, ow_trq->read_residual_ns);
	} else {
		hwlog_err("NULL point ic_ow_trq passed to %s.\n", __func__);
	}
}

static int pl061_gpio_register(onewire_phy_ops *ow_phy_ops, unsigned int id)
{
	unsigned int myid = 0;
	int ret;

	if (!phy_pdev || !phy_pdev->dev.of_node)
		return ONEWIRE_PHY_MATCH_FAIL;

	ret = of_property_read_u32(phy_pdev->dev.of_node, "phandle", &myid);
	if (ret) {
		hwlog_err("This physic controller is loaded, but never used!(phandle:%d, func:%s)",
			  ret, __func__);
		return ONEWIRE_PHY_MATCH_FAIL;
	}

	if (myid == id) {
		ow_phy_ops->reset            = onewire_reset;
		ow_phy_ops->read_byte        = onewire_read_byte;
		ow_phy_ops->write_byte       = onewire_write_byte;
		ow_phy_ops->set_time_request = onewire_set_time_request;
		ow_phy_ops->wait_for_ic      = onewire_wait_for_ic;
		return ONEWIRE_PHY_SUCCESS;
	}

	return ONEWIRE_PHY_MATCH_FAIL;
}

static ow_phy_reg_list pl061_gpio_reg_node;

static int onewire_phy_pl061_open_ic(void)
{
	unsigned long flags;

	spin_lock_irqsave(&ow_lock, flags);
	gpio_direction_output_unsafe(HIGH_VOLTAGE);
	spin_unlock_irqrestore(&ow_lock, flags);

	return 0;
}

static int onewire_phy_pl061_close_ic(void)
{
	unsigned long flags;

	spin_lock_irqsave(&ow_lock, flags);
	gpio_direction_input_unsafe();
	spin_unlock_irqrestore(&ow_lock, flags);

	return 0;
}

static const struct security_ic_ops onewire_phy_pl061_ops = {
	.open_ic = onewire_phy_pl061_open_ic,
	.close_ic = onewire_phy_pl061_close_ic,
};

static int onewire_phy_pl061_gpio_driver_probe(struct platform_device *pdev)
{
	int ret;

	hwlog_info("onewire_phy: pl061 gpio is probing...\n");

	phy_pdev = pdev;

	ow_gpio = devm_gpiod_get(&pdev->dev, "onewire", GPIOD_ASIS);
	if (IS_ERR(ow_gpio)) {
		ret = PTR_ERR(ow_gpio);
		hwlog_err("No gpio named onewire-gpios required by %s (%d).",
			  __func__, ret);
		return ONEWIRE_GPIO_FAIL;
	}
	gpiod_set_consumer_name(ow_gpio, gpio_name);

	pl061_gpio_reg_node.onewire_phy_register = pl061_gpio_register;
	list_add_tail(&pl061_gpio_reg_node.node, &ow_phy_reg_head);
	register_security_ic_ops(&onewire_phy_pl061_ops);

	hwlog_info("onewire_phy: pl061 gpio probed success.\n");

	return ONEWIRE_PHY_SUCCESS;
}

static int onewire_phy_pl061_gpio_driver_remove(struct platform_device *pdev)
{
	unregister_security_ic_ops(&onewire_phy_pl061_ops);
	return ONEWIRE_PHY_SUCCESS;
}

static const struct of_device_id onewire_phy_pl061_gpio_match_table[] = {
	{
		.compatible = "onewire-phy,pl061,gpio",
	},
	{ /*end*/},
};

static struct platform_driver onewire_phy_pl061_gpio_driver = {
	.probe		= onewire_phy_pl061_gpio_driver_probe,
	.remove		= onewire_phy_pl061_gpio_driver_remove,
	.driver		= {
		.name = "onewire_phy_pl061_gpio",
		.of_match_table = onewire_phy_pl061_gpio_match_table,
	},
};

int __init onewire_phy_pl061_gpio_driver_init(void)
{
	hwlog_info("onewire_phy: pl061 gpio driver init...\n");
	return platform_driver_register(&onewire_phy_pl061_gpio_driver);
}

void __exit onewire_phy_pl061_gpio_driver_exit(void)
{
	platform_driver_unregister(&onewire_phy_pl061_gpio_driver);
}

subsys_initcall_sync(onewire_phy_pl061_gpio_driver_init);
module_exit(onewire_phy_pl061_gpio_driver_exit);

MODULE_LICENSE("GPL");
