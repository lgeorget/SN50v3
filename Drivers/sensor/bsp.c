#include "tremo_gpio.h"
#include "bsp.h"
#include "gpio_exti.h"
#include "tremo_delay.h"
#include "tremo_iwdg.h"
#include "lora_config.h"
#include "tremo_adc.h"
#include "log.h"
#include "tremo_uart.h"
#include "I2C_A.h"
#include "lora_app.h"
#include "I2C_sensor.h"
#include "ds18b20.h"
#include "weight.h"
#include "tfsensor.h"
#include "ult.h"
#include "pwm.h"
#include "TMP117_I2C.h"
#include <math.h>

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Exported functions ---------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

#define VBAT_FACTOR     3.06f

bool tmp117_connect_status;
uint8_t icnumber=0;
bool bh1750flags=0;
uint8_t mode2_flag=0;
static uint8_t flags=0;
extern uint8_t workmode;
extern uint8_t inmode,inmode2,inmode3;
extern uint16_t power_5v_time;
extern uint32_t count1,count2;
extern uint32_t max_gust,count_gust;
extern uint8_t pwm_timer;
extern uint16_t IC1[4],IC2[4];
static float tmp117_temp_record=10;
extern uint16_t intensity;
extern uint16_t adc_resistance;
extern bool debug_flags;

static const int GUST_PERIOD = 2250;
#define W_SIZE 300 // enough for 10 minutes
static float wd[W_SIZE];
static uint16_t ws[W_SIZE];
static int w_begin = 0, w_end = 0;
#define MAX_WIND_VANE_R 20000

static TimerEvent_t GustTimer;
static void OnGustTimerEvent( void );

static void OnGustTimerEvent( void )
{
	TimerSetValue(&GustTimer, GUST_PERIOD);
	TimerStart(&GustTimer);
	if (count_gust > max_gust)
		max_gust = count_gust;

	POWER_IoInit();
	uint16_t mv = ADC_Read(3,0);
	POWER_IoDeInit();
	uint16_t r = (adc_resistance*100) * mv / (5000 - mv);
	if (r > MAX_WIND_VANE_R) {
		r = MAX_WIND_VANE_R;
	}
	wd[w_end] = r*2*M_PI / MAX_WIND_VANE_R;
	ws[w_end] = count_gust;
	if (debug_flags==1) {
		LOG_PRINTF(LL_DEBUG,"mv:%u, r:%u, begin:%u, end:%u, last_wd:%f, last_ws:%u\r\n", mv, r, w_begin, w_end, wd[w_end], ws[w_end]);
	}
	if (w_end >= W_SIZE) { //circular buffer
		w_end = 0;
		w_begin = 1;
	}
	else {
		w_end++;
	}
	if (w_begin > w_end) {
		w_begin++;
	}
	if (w_begin >= W_SIZE) {
		w_begin = 0;
	}
	count_gust=0;
}


void BLE_power_Init(void)
{
	 //reset
	 rcc_enable_peripheral_clk(RCC_PERIPHERAL_GPIOD, true);
	 gpio_set_iomux(GPIOD, GPIO_PIN_10, 0);
	 gpio_init(GPIOD, GPIO_PIN_10, GPIO_MODE_OUTPUT_PP_LOW); 
	 delay_ms(300);	 
	 gpio_init(GPIOD, GPIO_PIN_10, GPIO_MODE_OUTPUT_PP_HIGH); 
		 
	 delay_ms(200);	
	
	 uint8_t buf[8];
	 lora_config_deveui_get(buf);
	 LOG_PRINTF(LL_DEBUG,"AT+NAME%02X%02X%02X%02X%02X%02X%02X%02X\r\n", 
							buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);

	 delay_ms(50);
		
	 gpio_init(GPIOD, GPIO_PIN_10, GPIO_MODE_OUTPUT_PP_LOW); 
	 delay_ms(200);	 
	 gpio_init(GPIOD, GPIO_PIN_10, GPIO_MODE_OUTPUT_PP_HIGH); 
}

void BSP_sensor_Init( void  )
{		
	 BLE_power_Init();	
	 GPIO_EXTI4_IoInit(0);
	 GPIO_EXTI8_IoInit(0);		
	 GPIO_EXTI15_IoInit(0);
	
	 if((workmode==1)||(workmode==3)||(workmode==12))
	 {
		 I2C_GPIO_MODE_Config();
		 if(check_sht20_connect()==1)
		 {
			 flags=1;
			 LOG_PRINTF(LL_DEBUG,"\n\rUse Sensor is STH2x\r\n");
			 delay_ms(20);
		 }
		 
		 if(check_sht31_connect()==1)
		 {
			 flags=2;
			 LOG_PRINTF(LL_DEBUG,"\n\rUse Sensor is STH3x\r\n");
			 delay_ms(20);
		 }
		 
		 if(flags==0)
		 {
			 uint16_t luxtemp;
			 luxtemp=bh1750_read();
			 if(luxtemp!=0)
			 {
				flags=3;
				LOG_PRINTF(LL_DEBUG,"\n\rUse Sensor is BH1750\r\n");	
        delay_ms(20);				 
			 }
		 }
		 
		 if(flags==0)
		 {
			 LOG_PRINTF(LL_DEBUG,"\n\rNo I2C device detected\r\n");
			 delay_ms(20);
		 }	 
     I2C_GPIO_MODE_ANALOG();	
	 }
	 else if(workmode==2)
	 {
	  POWER_IoInit();
		delay_ms(500); 
		
		GPIO_ULT_INPUT_Init();
		GPIO_ULT_OUTPUT_Init();	
		delay_ms(100);
		if(gpio_read(ULT_Echo_PORT, ULT_Echo_PIN)==0)
		{  
			mode2_flag=2;	 
			LOG_PRINTF(LL_DEBUG,"\n\rUse Sensor is ultrasonic distance measurement\r\n");
			delay_ms(20);
			GPIO_ULT_INPUT_DeInit();
			GPIO_ULT_OUTPUT_DeInit();			
		}	 
    else
		{			
			I2C_GPIO_MODE_Config();
			if(waitbusy(2)<999)
			{
			 mode2_flag=1;			
			 LOG_PRINTF(LL_DEBUG,"\n\rUse Sensor is LIDAR_Lite_v3HP\r\n");
			 delay_ms(20);
			}				
			
			if(mode2_flag==0)
			{	 
				if(check_deceive()==1)
				{
					mode2_flag=3;
					LOG_PRINTF(LL_DEBUG,"\n\rUse Sensor is TF-series sensor\r\n");	
					delay_ms(20);
				}	
				else
				{	
					LOG_PRINTF(LL_DEBUG,"\n\rNo distance measurement device detected\r\n");	
          delay_ms(20);					
				}					
			} 
		}		
    I2C_GPIO_MODE_ANALOG();			
		POWER_IoDeInit();			 
	 }
	 else if(workmode==5)
	 {
	  POWER_IoInit();	
		WEIGHT_SCK_Init();
		WEIGHT_DOUT_Init();
		Get_Maopi();
    delay_ms(500);
		Get_Maopi();	
		WEIGHT_SCK_DeInit();
		WEIGHT_DOUT_DeInit();				
		POWER_IoDeInit();		
		LOG_PRINTF(LL_DEBUG,"\n\rUse Sensor is HX711\r\n");	
    delay_ms(20);		 
	 }
	 else if((workmode==7)||(workmode==9))	
	 {
		 GPIO_EXTI4_IoInit(inmode2);
		 GPIO_EXTI15_IoInit(inmode3);		 
	 }	
	 else if(workmode==11)
	 {
		TMP117_I2C_GPIO_MODE_Config();
		get_tmp117_temp();		 
		if(tmp117_connect_status==1)
		{
			LOG_PRINTF(LL_DEBUG,"\n\rUse Sensor is TMP117\r\n");	
			delay_ms(30);		
		}			
	 }
	 
	 if((workmode==3)||(workmode==8))
	 {
		 GPIO_EXTI15_IoInit(inmode3);
	 }
	 else if (workmode!=12)
	 {
		GPIO_EXTI8_IoInit(inmode);	
	 }
	 
	 if (workmode==12)
	 {
			GPIO_EXTI4_IoInit(inmode2);
			GPIO_EXTI15_IoInit(inmode3);
      TimerInit(&GustTimer, OnGustTimerEvent);
      TimerSetValue(&GustTimer, GUST_PERIOD);
      TimerStart(&GustTimer);
	 }
	 
	 POWER_IoDeInit();	
	 GPIO_BLE_STATUS_Ioinit();
	 
	 rcc_enable_peripheral_clk(RCC_PERIPHERAL_GPIOB, true);
	 gpio_set_iomux(LED_RGB_PORT, LED_RED_PIN, 0);
	 gpio_init(LED_RGB_PORT, LED_RED_PIN, GPIO_MODE_OUTPUT_PP_LOW);	
	 gpio_set_iomux(LED_RGB_PORT, LED_GREEN_PIN, 0);
	 gpio_init(LED_RGB_PORT, LED_GREEN_PIN, GPIO_MODE_OUTPUT_PP_LOW);
	 gpio_set_iomux(LED_RGB_PORT, LED_BLUE_PIN, 0);	
	 gpio_init(LED_RGB_PORT, LED_BLUE_PIN, GPIO_MODE_OUTPUT_PP_LOW);
	
	 rcc_enable_peripheral_clk(RCC_PERIPHERAL_GPIOC, true);
	 gpio_init(GPIO_USERKEY_PORT, GPIO_USERKEY_PIN, GPIO_MODE_INPUT_PULL_UP);
	 gpio_config_interrupt(GPIO_USERKEY_PORT, GPIO_USERKEY_PIN, GPIO_INTR_FALLING_EDGE);
	 gpio_config_stop3_wakeup(GPIO_USERKEY_PORT, GPIO_USERKEY_PIN,true,GPIO_LEVEL_LOW);
	 NVIC_SetPriority(GPIO_IRQn, 3);
	 /* NVIC config */
	 NVIC_EnableIRQ(GPIO_IRQn);	
}

void BSP_sensor_Read( sensor_t *sensor_data , uint8_t message ,uint8_t mod_temp)
{		
	iwdg_reload();
  sensor_data->bat_mv=battery_voltage_measurement();
  if(sensor_data->bat_mv>3750)
	{
	  sensor_data->bat_mv=battery_voltage_measurement();
	}	

  if(message==1)
	{				
		LOG_PRINTF(LL_DEBUG,"\r\nBat_voltage:%d mv\n\r",sensor_data->bat_mv);
    delay_ms(20);				
	}	
			
  if(mod_temp==1)
	{
		sensor_data->temp1=DS18B20_Read(1,message);
    I2C_read_data(sensor_data,flags,message);
		POWER_open_time(power_5v_time);
		sensor_data->ADC_4=ADC_Read(1,message);
		sensor_data->in1=Digital_input_Read(3,message);
		sensor_data->exit_pa8=Digital_input_Read(2,message);		
	}	
	else if(mod_temp==2)
	{
		sensor_data->temp1=DS18B20_Read(1,message);
		POWER_open_time(power_5v_time);
		sensor_data->ADC_4=ADC_Read(1,message);
		sensor_data->in1=Digital_input_Read(3,message);	
		sensor_data->exit_pa8=Digital_input_Read(2,message);	
	  if(mode2_flag==1)
		{
			I2C_GPIO_MODE_Config();
			LidarLite_init();			 
			sensor_data->distance_mm=LidarLite();	
			I2C_GPIO_MODE_ANALOG();	
			if(message==1)
			{				
				LOG_PRINTF(LL_DEBUG,"lidar_lite_distance:%d cm\r\n",(sensor_data->distance_mm/10));
        delay_ms(20);				
			}				
		}
		else if(mode2_flag==2)
		{
			GPIO_ULT_INPUT_Init();
			GPIO_ULT_OUTPUT_Init();	
			sensor_data->distance_mm=ULT_test();
			GPIO_ULT_INPUT_DeInit();
			GPIO_ULT_OUTPUT_DeInit();	
			if(message==1)
			{	
				LOG_PRINTF(LL_DEBUG,"ULT_distance:%.1f cm\r\n",(sensor_data->distance_mm/10.0));
				delay_ms(20);	
			}
		}		
    else if(mode2_flag==3)
		{
			tfsensor_reading_t reading_t;
			tfsensor_read_distance(&reading_t);	
		  sensor_data->distance_mm = reading_t.distance_cm;		
			sensor_data->distance_signal_strengh = reading_t.distance_signal_strengh;		
			if(message==1)
			{	
				LOG_PRINTF(LL_DEBUG,"TF_distance:%d cm,TF_strength:%d\r\n",(sensor_data->distance_mm/10),sensor_data->distance_signal_strengh);
        delay_ms(20);				
			}				
		}
		else
		{
			sensor_data->distance_mm = 65535;		
			sensor_data->distance_signal_strengh = 65535;			
		}    		
	}
	else if(mod_temp==3)
	{
    I2C_read_data(sensor_data,flags,message);		
		POWER_open_time(power_5v_time);
		sensor_data->ADC_4=ADC_Read(1,message);
		delay_ms(50);
		sensor_data->ADC_5=ADC_Read(2,message);	
		delay_ms(50);
    sensor_data->ADC_8=ADC_Read(3,message);	
		sensor_data->exit_pb15=Digital_input_Read(3,message);					
	}
	else if(mod_temp==4)
	{
		sensor_data->temp1=DS18B20_Read(1,message);
		sensor_data->temp2=DS18B20_Read(2,message);
		sensor_data->temp3=DS18B20_Read(3,message);
		POWER_open_time(power_5v_time);
		sensor_data->ADC_4=ADC_Read(1,message);
		sensor_data->in1=Digital_input_Read(3,message);			
		sensor_data->exit_pa8=Digital_input_Read(2,message);				
	}
	else if(mod_temp==5)
	{
		sensor_data->temp1=DS18B20_Read(1,message);
		POWER_open_time(power_5v_time);
		sensor_data->ADC_4=ADC_Read(1,message);
		sensor_data->in1=Digital_input_Read(3,message);			
		sensor_data->exit_pa8=Digital_input_Read(2,message);	
		WEIGHT_SCK_Init();
		WEIGHT_DOUT_Init();		 
		sensor_data->Weight=Get_Weight();		
		WEIGHT_SCK_DeInit();
		WEIGHT_DOUT_DeInit();			
		if(message==1)
		{	
			LOG_PRINTF(LL_DEBUG,"HX711_Weight:%d g\r\n",(int)sensor_data->Weight);
			delay_ms(20);
		}    		
	}
	else if(mod_temp==6)
	{
		sensor_data->temp1=DS18B20_Read(1,message);
		POWER_open_time(power_5v_time);
		sensor_data->ADC_4=ADC_Read(1,message);
		sensor_data->in1=Digital_input_Read(3,message);	
    sensor_data->count_pa8=count1;
		if(message==1)
		{	
			LOG_PRINTF(LL_DEBUG,"PA8_count:%u\r\n",(unsigned int)count1);
			delay_ms(20);
		}      		
	}
	else if(mod_temp==7)
	{
		sensor_data->temp1=DS18B20_Read(1,message);
		POWER_open_time(power_5v_time);
		sensor_data->ADC_5=ADC_Read(2,message);
		sensor_data->exit_pa4=Digital_input_Read(1,message);	
		sensor_data->exit_pa8=Digital_input_Read(2,message);	
		sensor_data->exit_pb15=Digital_input_Read(3,message);	
	}
	else if(mod_temp==8)
	{
		if(message==1)
		{				
			LOG_PRINTF(LL_DEBUG,"adc_resistance:%d Ohms\r\n",adc_resistance*100);
       delay_ms(20);				
		}			
		POWER_open_time(power_5v_time);
		sensor_data->ADC_4=ADC_Read(1,message);
		delay_ms(50);
		sensor_data->ADC_5=ADC_Read(2,message);
		delay_ms(50);		
    sensor_data->ADC_8=ADC_Read(3,message);		
    sensor_data->exit_pb15=Digital_input_Read(3,message);				
	}
	else if(mod_temp==9)
	{
		sensor_data->temp1=DS18B20_Read(1,message);
		sensor_data->temp2=DS18B20_Read(2,message);
		sensor_data->temp3=DS18B20_Read(3,message);
		POWER_open_time(power_5v_time);
		sensor_data->exit_pb15=Digital_input_Read(3,message);	
    sensor_data->count_pa4=count2;
    sensor_data->count_pa8=count1;		
		if(message==1)
		{	
			LOG_PRINTF(LL_DEBUG,"PA8_count:%u\r\n",(unsigned int)count1);
			LOG_PRINTF(LL_DEBUG,"PA4_count:%u\r\n",(unsigned int)count2);
			delay_ms(40);
		}     		
	}	
	else if(mod_temp==10)
	{
		sensor_data->temp1=DS18B20_Read(1,message);
		POWER_open_time(power_5v_time);		
		sensor_data->ADC_4=ADC_Read(1,message);
		sensor_data->in1=Digital_input_Read(3,message);
		sensor_data->exit_pa8=Digital_input_Read(2,message);	
		
    icnumber=0;
    for(uint8_t y=0;y<4;y++)
		{
			IC1[y]=0;
			IC2[y]=0;
		}		
	  gptimer_pwm_input_capture(pwm_timer);
		if(pwm_timer==0)
		{
			delay_ms(500);
		}
		else
		{
			delay_ms(1500);
		}
		gptimer_pwm_Iodeinit();		
		
		if(middle_value(IC1)!=0)
		{
			sensor_data->pwm_freq = middle_value(IC1)+1;	
		}	
    else
		{			
      sensor_data->pwm_freq = 0;	
		}

		if(middle_value(IC2)!=0)
		{
			sensor_data->pwm_duty = middle_value(IC2)+1;
		}
		else
		{
			sensor_data->pwm_duty = 0;
		}
		
		if(message==1)
		{	
			if(pwm_timer==0)
			{
				LOG_PRINTF(LL_DEBUG,"PWM_pulse_period: %d us\r\n",sensor_data->pwm_freq);
				LOG_PRINTF(LL_DEBUG,"PWM_high_level_time: %d us\r\n",sensor_data->pwm_duty);
			}
			else
			{
				LOG_PRINTF(LL_DEBUG,"PWM_pulse_period: %d ms\r\n",sensor_data->pwm_freq);
				LOG_PRINTF(LL_DEBUG,"PWM_high_level_time: %d ms\r\n",sensor_data->pwm_duty);				
			}
			delay_ms(40);
		} 		
	}
  else if(mod_temp==11)
	{
		sensor_data->temp1=DS18B20_Read(1,message);
		TMP117_I2C_GPIO_MODE_Config();
		sensor_data->temp_tmp117=get_tmp117_temp();
		
		if(sensor_data->temp_tmp117-tmp117_temp_record>20 || sensor_data->temp_tmp117-tmp117_temp_record<-20)
		{
			TMP117_soft_reset();
			sensor_data->temp_tmp117=get_tmp117_temp();
		}
		
		if(sensor_data->temp_tmp117!=327.67)
		{
			tmp117_temp_record=sensor_data->temp_tmp117;
		}
	
		if(message==1)
		{		
			if(sensor_data->temp_tmp117!=327.67)
			{			
				LOG_PRINTF(LL_DEBUG,"TMP117_temp=%.2f\n\r",sensor_data->temp_tmp117);
			}
			else
			{
				LOG_PRINTF(LL_DEBUG,"TMP117_temp=NULL\n\r");				
			}
		}		
		POWER_open_time(power_5v_time);
		sensor_data->ADC_4=ADC_Read(1,message);
		sensor_data->in1=Digital_input_Read(3,message);
		sensor_data->exit_pa8=Digital_input_Read(2,message);		
	}		
	else if(mod_temp==12)
	{
		I2C_read_data(sensor_data,flags,message);
		POWER_open_time(power_5v_time);
		sensor_data->count_pa4=count1;
		sensor_data->count_pb15=count2;
		sensor_data->ADC_8=ADC_Read(3,message);
		delay_ms(50);
		
		double v = 0;
		double u = 0;
		uint32_t w_total = 0;
		if (w_begin > w_end) {	
			for (int i=w_begin ; i<W_SIZE ; i++) {
				u += ws[i] * sin(wd[i]);
				v += ws[i] * cos(wd[i]);
				w_total += ws[i];
			}
			for (int i=0 ; i<w_end ; i++) {
				u += ws[i] * sin(wd[i]);
				v += ws[i] * cos(wd[i]);
				w_total += ws[i];
			}
		}
		else
		{
			for (int i=w_begin ; i<w_end ; i++) {
				u += ws[i] * sin(wd[i]);
				v += ws[i] * cos(wd[i]);
				w_total += ws[i];
			}
		}

		if (w_total) {
			double mean_wd = atan2(v / w_total, u / w_total);
			uint16_t mean_wd_deg = (uint16_t)((5 * M_PI_2 - mean_wd) * 180 / M_PI) % 360;
			sensor_data->wind_dir=mean_wd_deg;
		} else {
			sensor_data->wind_dir=0xFFFF;
		}
		
		if(message==1)
		{
			LOG_PRINTF(LL_DEBUG,"PA4 count:%u\r\n",(unsigned int)count1);
			LOG_PRINTF(LL_DEBUG,"Rate:%u\r\n",(unsigned int)intensity);
			LOG_PRINTF(LL_DEBUG,"PB15 count:%u\r\n",(unsigned int)count2);
			LOG_PRINTF(LL_DEBUG,"max gust:%u\r\n",(unsigned int)max_gust);
			LOG_PRINTF(LL_DEBUG,"prevailing dir:%u\r\n",(unsigned int)sensor_data->wind_dir);
			delay_ms(20);
		}
	}
  POWER_IoDeInit();	
}

uint16_t battery_voltage_measurement(void)
{
	uint16_t bat_mv;

	#if defined LB_LS
  bat_mv=6*adc_in1_measurement(ADC_SOLAR_LEVEL_PORT,ADC_SOLAR_LEVEL_PIN,GPIO_SOLAR_BAT_CHANNEL);	
	#else
	gpio_set_iomux(ADC_BAT_OUTPUT_PORT, ADC_BAT_OUTPUT_PIN, 0);
	gpio_init(ADC_BAT_OUTPUT_PORT, ADC_BAT_OUTPUT_PIN, GPIO_MODE_OUTPUT_PP_LOW);	
	
  bat_mv=6*adc_in1_measurement(ADC_BAT_LEVEL_PORT,ADC_BAT_LEVEL_PIN,GPIO_ADC_BAT_CHANNEL);

//	LOG_PRINTF(LL_DEBUG,"%d\r\n",adc_in1_measurement(ADC_BAT_LEVEL_PORT,ADC_BAT_LEVEL_PIN,GPIO_ADC_BAT_CHANNEL));
	
	gpio_set_iomux(ADC_BAT_OUTPUT_PORT, ADC_BAT_OUTPUT_PIN, 0);
	gpio_init(ADC_BAT_OUTPUT_PORT, ADC_BAT_OUTPUT_PIN, GPIO_MODE_ANALOG);	
	#endif		
	return bat_mv;
}

float DS18B20_Read(uint8_t temp,uint8_t message)
{
	float temp_ds=0;
	if(temp==1)
	{
		temp_ds=DS18B20_GetTemp_SkipRom(temp);
		if(message==1)
		{			
			if((temp_ds>=-55)&&(temp_ds<=125))
			{
				LOG_PRINTF(LL_DEBUG,"DS18B20_temp1:%.1f\r\n",temp_ds);
				delay_ms(20);
			}
			else
			{
				LOG_PRINTF(LL_DEBUG,"DS18B20_temp1:null\r\n");
				delay_ms(20);
			}
		}			
	}
	else if(temp==2)
	{
		temp_ds=DS18B20_GetTemp_SkipRom(temp);
		if(message==1)
		{			
			if((temp_ds>=-55)&&(temp_ds<=125))
			{
				LOG_PRINTF(LL_DEBUG,"DS18B20_temp2:%.1f\r\n",temp_ds);
				delay_ms(20);
			}
			else
			{
				LOG_PRINTF(LL_DEBUG,"DS18B20_temp2:null\r\n");
				delay_ms(20);
			}
		}		
	}	
	else if(temp==3)
	{
		temp_ds=DS18B20_GetTemp_SkipRom(temp);
		if(message==1)
		{			
			if((temp_ds>=-55)&&(temp_ds<=125))
			{
				LOG_PRINTF(LL_DEBUG,"DS18B20_temp3:%.1f\r\n",temp_ds);
				delay_ms(20);
			}
			else
			{
				LOG_PRINTF(LL_DEBUG,"DS18B20_temp3:null\r\n");
				delay_ms(20);
			}
		}			
	}		
	return temp_ds;
}

uint16_t ADC_Read(uint8_t temp,uint8_t message)
{
	uint16_t adc_temp=0;
	if(temp==1)
	{
		adc_temp=adc_in1_measurement(ADC_IN3_LEVEL_PORT,ADC_IN3_LEVEL_PIN,GPIO_ADC_IN3_CHANNEL);
		if(message==1)
		{				
			LOG_PRINTF(LL_DEBUG,"ADC_PA4:%.3f V\r\n",(adc_temp/1000.0));
			delay_ms(20);
		}		
	}
	else if(temp==2)
	{
		adc_temp=adc_in1_measurement(ADC_IN2_LEVEL_PORT,ADC_IN2_LEVEL_PIN,GPIO_ADC_IN2_CHANNEL);
		if(message==1)
		{				
			LOG_PRINTF(LL_DEBUG,"ADC_PA5:%.3f V\r\n",(adc_temp/1000.0));
			delay_ms(20);
		}		
	}	
	else if(temp==3)
	{
		adc_temp=adc_in1_measurement(ADC_IN1_LEVEL_PORT,ADC_IN1_LEVEL_PIN,GPIO_ADC_IN1_CHANNEL);
		if(message==1)
		{				
			LOG_PRINTF(LL_DEBUG,"ADC_PA8:%.3f V\r\n",(adc_temp/1000.0));
			delay_ms(20);
		}			
	}		
  return adc_temp;	
}

bool Digital_input_Read(uint8_t temp,uint8_t message)
{
	bool pin_status=0;
	if(temp==1)
	{		
		pin_status=gpio_read(GPIO_EXTI4_PORT, GPIO_EXTI4_PIN);
		if(message==1)
		{				
			LOG_PRINTF(LL_DEBUG,"PA4_status:%d\r\n",pin_status);
			delay_ms(20);
		}	
	}
	else if(temp==2)
	{
		pin_status=gpio_read(GPIO_EXTI8_PORT, GPIO_EXTI8_PIN);	
		if(message==1)
		{				
			LOG_PRINTF(LL_DEBUG,"PA8_status:%d\r\n",pin_status);
			delay_ms(20);
		}			
	}	
	else if(temp==3)
	{
		pin_status=gpio_read(GPIO_EXTI15_PORT, GPIO_EXTI15_PIN);	
		if(message==1)
		{				
			LOG_PRINTF(LL_DEBUG,"PB15_status:%d\r\n",pin_status);
			delay_ms(20);
		}			
	}	
  return pin_status;	
}

uint16_t middle_value(uint16_t value[])
{
	uint16_t a,b,c,temp;
	a = value[1];
	b = value[2];
	c = value[3];
	
  if (a > b)
  {
    temp = a;
    a = b;
    b = temp;
  }
	
  if (a > c)
  {
    temp = a;
    a = c;
    c = temp;
  }
	
  if (b > c)
  {
    temp = b;
    b = c;
    c = temp;
  }
	
	return b;
}
/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
