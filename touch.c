#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <asm/io.h>
#include <asm/irq.h>
//#include <asm/plat-s3c24xx/ts.h>
#include <asm/arch/regs-adc.h>
#include <asm/arch/regs-gpio.h>

static struct input_dev *s3c_ts_dev;

struct s3c_ts_reg
{
	unsigned long adccon;
	unsigned long adctsc;
	unsigned long adcdly;
	unsigned long adcdat0;
	unsigned long adcdat1;
	unsigned long adcupdn;
};

static volatile struct s3c_ts_reg  __iomem *s3c_ts_regs;
static void __iomem *clkcon;
static struct timer_list s3c_ts_timer;


static void enter_wait_pen_down_mode(void)
{
	s3c_ts_regs->adctsc = 0xd3;

}

static void enter_wait_pen_up_mode(void)
{

          s3c_ts_regs->adctsc = 0x1d3;
}

static void enter_measure_xy_mode(void)
{

	s3c_ts_regs->adctsc = (1<<3) |(1<<2);
}

static void start_adc(void)
{
	s3c_ts_regs->adccon |=(1<<0);
}

static int s3c_filter_ts(int x[],int y[])
{
	#define ERR_LIMIT 10
	int avr_x ,avr_y;
	int det_x,det_y;

	avr_x = (x[0] +x[1])/2;
	avr_y = (y[0] +y[1])/2;

	det_x = (x[2] > avr_x)?(x[2] - avr_x):(avr_x-x[2]);
	det_y = (y[2] > avr_y)?(y[2] - avr_y):(avr_y-y[2]);

	if( (det_x > ERR_LIMIT) || (det_y > ERR_LIMIT))
		return 0;

	avr_x = (x[1] + x[2])/2;
	avr_y = (y[1] + y[2])/2;

	det_x = (x[3] > avr_x) ? (x[3]-avr_x) : (avr_x-x[3]);	
	det_y = (y[3] > avr_y) ? (y[3]-avr_y) : (avr_y-y[3]);


	if((det_x > ERR_LIMIT) ||(det_y >ERR_LIMIT))
		return 0;

	return 1;
}



static irqreturn_t pen_down_up_irq(int irq,void *dev_id)
{
 
	if(s3c_ts_regs->adcdat0 & (1<<15))
	{
               //printk("irq up \n ");
		enter_wait_pen_down_mode();		
		input_report_abs(s3c_ts_dev,ABS_PRESSURE,0);	
		input_report_key(s3c_ts_dev,BTN_TOUCH,0);
		input_sync(s3c_ts_dev);
		
	}
	else
	{ 
		enter_measure_xy_mode();
		start_adc();
	}


	return IRQ_HANDLED;
}

static irqreturn_t adc_irq(int irq, void *dev_id)
{
	static int cnt = 0;
	static int x[4],y[4];
	int adcdat0,adcdat1;

	adcdat0 = s3c_ts_regs->adcdat0;
	adcdat1 = s3c_ts_regs->adcdat1;
          
	if(adcdat0 & (1<<15))
	{

		cnt = 0;
		enter_wait_pen_down_mode();
		input_report_abs(s3c_ts_dev,ABS_PRESSURE,0);
		input_report_key(s3c_ts_dev,BTN_TOUCH,0);
		input_sync(s3c_ts_dev);

	}
	else
	{
		x[cnt] = adcdat0 & 0x3ff;
		y[cnt] = adcdat1 & 0x3ff;
		cnt++;
		if(cnt == 4)
		{
			if(s3c_filter_ts(x,y))
			{
				printk("x= %d ,y= %d \n",(x[0]+x[1]+x[2]+x[3])/4,(y[0]+y[1]+y[2]+y[3])/4);
				input_report_abs(s3c_ts_dev,ABS_X,(x[0]+x[1]+x[2]+x[3])/4);
				input_report_abs(s3c_ts_dev,ABS_Y,(y[0]+y[1]+y[2]+y[3])/4);
				input_report_abs(s3c_ts_dev,ABS_PRESSURE,1);
				input_report_key(s3c_ts_dev,BTN_TOUCH,1);
				input_sync(s3c_ts_dev);
			}
		
			cnt = 0;
			enter_wait_pen_up_mode();
			mod_timer(&s3c_ts_timer,jiffies + HZ/100);
		}
		else
		{
			enter_measure_xy_mode();
			start_adc();
		}

	}
	
	return IRQ_HANDLED;

}

static void s3c_ts_timer_func(unsigned long data)
{

	if(s3c_ts_regs->adcdat0 & (1<<15))
	{

	      /* up key*/
		enter_wait_pen_down_mode();
		//printk("up \n");
	}
	else
	{
		/* down key */
		enter_measure_xy_mode();
		//printk("down************** \n");
		start_adc();
		
	}


}

static int s3c_ts_init(void)
{

	struct clk *adc_clk;
	int ret;
        unsigned int data_data;
/*
	adc_clk = clk_get(NULL,"abc");
	if(!adc_clk)
	{
	    printk(KERN_ERR "filed to get adc clock source \n");
	    return -ENOENT;
	}

	clk_enable(adc_clk);

*/
        clkcon = ioremap(0x4c00000c,0x4);

	(*(unsigned int *)clkcon) |= (1<<15);

	s3c_ts_regs = ioremap(0x58000000,sizeof(struct s3c_ts_reg ));

	if(s3c_ts_regs == NULL)
	{
		printk(KERN_ERR "failed to remap register block \n");

	}

	s3c_ts_regs->adccon = (1<<14) | (49<<6);
	s3c_ts_regs->adcdly = 0xffff;
	
	printk( "adcon =%x \n ",s3c_ts_regs->adccon);
	printk( "adcdly =%x \n ",s3c_ts_regs->adcdly);

        ret = request_irq(IRQ_ADC,adc_irq,IRQF_SAMPLE_RANDOM,"adc",NULL);
	printk("ret = %d \n",ret);
        ret = request_irq(IRQ_TC,pen_down_up_irq, IRQF_SAMPLE_RANDOM, "s3c_ts_pen",NULL);
	printk("ret = %d \n",ret);

	s3c_ts_dev = input_allocate_device();

	set_bit(EV_KEY,s3c_ts_dev->evbit);
	set_bit(EV_ABS,s3c_ts_dev->evbit);

	set_bit(BTN_TOUCH, s3c_ts_dev->keybit);
	
	input_set_abs_params(s3c_ts_dev,ABS_X,0,0x3FF,0,0);
	input_set_abs_params(s3c_ts_dev,ABS_Y,0,0x3FF,0,0);
	input_set_abs_params(s3c_ts_dev,ABS_PRESSURE,0,1,0,0);

	input_register_device(s3c_ts_dev);


	init_timer(&s3c_ts_timer);
	s3c_ts_timer.function = s3c_ts_timer_func;
                                 
	add_timer(&s3c_ts_timer);
	
	enter_wait_pen_down_mode();
        
	return 0;

}

static void s3c_ts_exit(void)
{

	free_irq(IRQ_TC,NULL);
	free_irq(IRQ_ADC,NULL);
	iounmap(s3c_ts_regs);
	iounmap(clkcon);

	input_unregister_device(s3c_ts_dev);
	input_free_device(s3c_ts_dev);
	del_timer(&s3c_ts_timer);
}

module_init(s3c_ts_init);
module_exit(s3c_ts_exit);
MODULE_LICENSE("GPL");

