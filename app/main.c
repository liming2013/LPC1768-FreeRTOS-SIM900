
/* data type definitions */
#include "stdint.h"

/* stdlibs */
#include "stdlib.h"

/* driver includes */
#include "uart.h"
#include "misc.h"
#include "lcd.h"
#include "delay.h"
#include "gsm.h"

/* device includes */
#include "system_LPC17xx.h"
#include "LPC17xx.h"
#include "stdio.h"

/* syscalls */
#include "syscalls.h"

/* string */
#include "string.h"

/* configurations */
#include "FreeRTOSConfig.h"

/* kernel files */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* application config */
#include "config.h"

/* streams (stdout, stdin, stderr) */
FILE * stdin;
FILE * stdout;
FILE * stderr;

/* Setup Hardware Function */
//////////////////////////////////////////////////////
void 	prvSetupHardware(void);
//////////////////////////////////////////////////////

/* Tasks */
//////////////////////////////////////////////////////
static void systemBoot(void * pvParameters);
static void connectGPRS(void * pvParameters);
static void updateModemStatus(void * pvParameters);
static void displayProcess(void * pvParameters);
static void scanCard(void * pvParameters);
static void scankeyPad(void * pvParameters);
static void httpProc(void * pvParameters);
//////////////////////////////////////////////////////

/* Semaphores */
//////////////////////////////////////////////////////
xSemaphoreHandle modemSema;
xSemaphoreHandle displaySema;
xSemaphoreHandle scanCardSema;
xSemaphoreHandle debugSema;
//////////////////////////////////////////////////////

/* Task Handles */
//////////////////////////////////////////////////////
xTaskHandle systemBootHandle;
xTaskHandle connectGPRSHandle;
xTaskHandle updateModemStatusHandle;
xTaskHandle displayProcessHandle;
xTaskHandle scanCardHandle;
xTaskHandle httpTaskHandle;
xTaskHandle keypadTaskHandle;
//////////////////////////////////////////////////////

/* Queues */
//////////////////////////////////////////////////////
xQueueHandle httpQueue;
xQueueHandle lcdQueue;
xQueueHandle keypadQueue;
//////////////////////////////////////////////////////

/* APN/OPR List size */
#define APN_OPR_LIST_LEN 8

/* List of operators */
const char * oprList[]	=	{	
								"airtel",
								"cellone",
								"idea",
								"aircel",
								"tata docomo",
								"t24",
								"reliance",
								"vodafone"
							};

/* List of Access Points for different operator */
const char * apnList[]	=	{	
								"airtelgprs.com",
								"bsnlnet",
								"internet",
								"aircelgprs.pr",
								"TATA.DOCOMO.INTERNET",
								"TATA.DOCOMO.INTERNET",
								"rcomnet",
								"www"
							};

/** @brief 
 *  @param 
 *  @return 
 */
void prvSetupHardware(void)
{
	SystemInit();
	SystemCoreClockUpdate();

	/* init uart's */
	uart0_init(__UART0_BAUDRATE);				// RFID Reader port
	uart1_init(__UART1_BAUDRATE);				// debug port
	uart3_init(__UART3_BAUDRATE);				// modem port

	/* initiate lcd */
	lcd_init();									// init lcd

	/* init buffers for gsm modem */
	gsm_buff_init();
}


/** @brief 
 *  @param 
 *  @return 
 */
int main(void)
{
	/* Setup the Hardware */
	prvSetupHardware();

	#ifdef __DEBUG_MESSAGES__
		debug_out("system started\r\n");
		debug_out("hardware setup completed\r\n");
		debug_out("creating the tasks\r\n");
	#endif

	/* Create Boot Task */
	xTaskCreate(	systemBoot,
					(signed portCHAR *)"boot",
					configMINIMAL_STACK_SIZE,
					NULL,
					tskIDLE_PRIORITY,
					&systemBootHandle);

	#ifdef __DEBUG_MESSAGES__
		debug_out("boot task created\r\n");
		debug_out("starting the os\r\n");
	#endif

	/* Start the scheduler */
	vTaskStartScheduler();

	/* Never reach here */
	return 0;
}

/** @brief 
 *  @param 
 *  @return 
 */
static void systemBoot(void * pvParameters)
{
	int8_t __response;
	for(;;)
	{
		/* create semaphores */
		//////////////////////////////////////////////////////
		modemSema 			= xSemaphoreCreateMutex();
		displaySema			= xSemaphoreCreateMutex();
		scanCardSema		= xSemaphoreCreateMutex();
		debugSema			= xSemaphoreCreateMutex();
		//////////////////////////////////////////////////////

		/* ping modem for 8 times */
		//////////////////////////////////////////////////////
		for(uint8_t i = 0;i < 8;i++)
		{
			if(gsm_ping_modem())
			{
				debug_out("ping: ");
				debug_putc(i + 48); 
				debug_out("\r\n");
			}	
		}
		//////////////////////////////////////////////////////

		/* Get operator name */
		__response = gsm_get_operator_name(&modem);

		/* convert to lower for comparison */
		strtolower(modem.operator_name);

		/* find out apn */
		//////////////////////////////////////////////////////
		for(uint8_t i = 0;i < APN_OPR_LIST_LEN;i++)
		{
			if(strstr(modem.operator_name, oprList[i]))
			{
				strcpy(modem.setapn, apnList[i]);
				break;
			}
		}
		//////////////////////////////////////////////////////

		#ifdef __DEBUG_MESSAGES__
			debug_out("apn search completed\r\n");
			debug_out("access point name for operator ");
			debug_out(modem.operator_name);
			debug_out(" is ");
			debug_out(modem.setapn);
			debug_out("\r\n");
			debug_out("reading apn from modem\r\n");
		#endif

		/* read apn from modem */
		__response = gsm_get_accesspoint(&modem);

		#ifdef __DEBUG_MESSAGES__
			debug_out("read apn completed\r\n");
			debug_out("access point read from modem: ");
			debug_out(modem.getapn);
			debug_out("\r\n");
		#endif

		/* Check with predefined APN */
		if(strstr(modem.getapn, modem.setapn) == NULL)
		{
			/* If both are not matched set access point */
			#ifdef __DEBUG_MESSAGES__
				debug_out("access point read from modem is different from actual\r\n");
				debug_out("So setting actual accesspoint name\r\n");
			#endif

			/* set access point */
			__response = gsm_set_accesspoint(&modem);

			if(__response)
			{
				#ifdef __DEBUG_MESSAGES__
					debug_out("set accesspoint name success\r\n");
					debug_out("reading apn again from modem\r\n");
				#endif		

				/* read access point */
				__response = gsm_get_accesspoint(&modem);

				#ifdef __DEBUG_MESSAGES__
					debug_out("read apn completed\r\n");
					debug_out("apn name read: ");
					debug_out(modem.getapn);
					debug_out(", actual: ");
					debug_out(modem.setapn);
					debug_out("\r\n");
				#endif					
			}
		}

		/* else modem has valid apn name */
		else
		{
			#ifdef __DEBUG_MESSAGES__
				debug_out("modem has valid apn no need to set\r\n");
			#endif			
		}

		/* create queues */
		//////////////////////////////////////////////////////
		httpQueue  	= xQueueCreate(10, 6);
		lcdQueue   	= xQueueCreate(10, sizeof(uint8_t     ));
		keypadQueue = xQueueCreate(10, sizeof(char));
		//////////////////////////////////////////////////////

		#ifdef __DEBUG_MESSAGES__
			debug_out("queues are created, now creating the\r\ntasks after that this task will delete\r\n");
		#endif				

		/* Create GPRS Task */
		//////////////////////////////////////////////////////
		xTaskCreate(	connectGPRS,
						(signed portCHAR *)"gprs",
						configMINIMAL_STACK_SIZE,
						NULL,
						tskIDLE_PRIORITY,
						&connectGPRSHandle);

		/* Create GPRS Task */
		xTaskCreate(	httpProc,
						(signed portCHAR *)"http",
						configMINIMAL_STACK_SIZE,
						NULL,
						tskIDLE_PRIORITY,
						&httpTaskHandle);

		/* Create GPRS Task */
		xTaskCreate(	scanCard,
						(signed portCHAR *)"scancard",
						configMINIMAL_STACK_SIZE,
						NULL,
						tskIDLE_PRIORITY,
						&scanCardHandle);

		/* Create GPRS Task */
		xTaskCreate(	displayProcess,
						(signed portCHAR *)"display",
						configMINIMAL_STACK_SIZE,
						NULL,
						tskIDLE_PRIORITY,
						&displayProcessHandle);
		//////////////////////////////////////////////////////

		/* delete the boot task */
		vTaskDelete(systemBootHandle);
	}
}

/** @brief 
 *  @param 
 *  @return 
 */
static void connectGPRS(void * pvParameters)
{
	int8_t __response;

	// #ifdef __DEBUG_MESSAGES__
	// 	debug_out("GPRS task started\r\n");
	// #endif

	for(;;)
	{
		/* Check IP Status and check whether shutdown is required */
		#ifdef __DEBUG_MESSAGES__
			debug_out("reading tcp status from modem\r\n");
		#endif

		/* Get TCP Status */
		__response = gsm_get_tcpstatus(&modem);

		/* lower case the state for comparison */
		strtolower(modem.tcpstatus);

		#ifdef __DEBUG_MESSAGES__
			debug_out("reading tcp status completed\r\n");
			debug_out("status: ");
			debug_out(modem.tcpstatus);
			debug_out("\r\n");
		#endif

		/* check tcpstatus and do what action is required to bring wireless up */
		if(strstr(modem.tcpstatus, "ip start") || strstr(modem.tcpstatus, "ip initial"))
		{
			#ifdef __DEBUG_MESSAGES__
				debug_out("ip start state bringing wireless up\r\n");
			#endif				
			if(gsm_start_gprs())
			{
				#ifdef __DEBUG_MESSAGES__
					debug_out("GPRS start success\r\n");
					debug_out("reading ip address\r\n");
				#endif	

				/* read ip address from modem */
				if(gsm_get_ip_address(&modem))
				{
					#ifdef __DEBUG_MESSAGES__
						debug_out("Read IP address success\r\n");
						debug_out(modem.ip_addr);
					#endif	
				}
			}
		}

		/* Not Implemented */
		else if(strstr(modem.tcpstatus, "ip config"))
		{
			#ifdef __DEBUG_MESSAGES__
				debug_out("ip configure\r\n");
			#endif				
		}

		/* If gprs is already activated just read ip address from the modem */
		else if(strstr(modem.tcpstatus, "ip gprsact") || strstr(modem.tcpstatus, "ip status"))
		{
			#ifdef __DEBUG_MESSAGES__
					debug_out("gprs act\r\n");
			#endif	

			#ifdef __DEBUG_MESSAGES__
				debug_out("reading ip address\r\n");
			#endif	

			/* read ip address from modem */
			if(gsm_get_ip_address(&modem))
			{
				#ifdef __DEBUG_MESSAGES__
					debug_out("Read IP address success\r\n");
					debug_out(modem.ip_addr);
				#endif	
			}
		}

		/* Not Implemented */
		else if(strstr(modem.tcpstatus, "tcp connecting"))
		{
			#ifdef __DEBUG_MESSAGES__
				debug_out("connecting\r\n");
			#endif				
		}

		/* If it already connected disconnect 1st and then read ip address*/
		else if(strstr(modem.tcpstatus, "connect ok"))
		{
			#ifdef __DEBUG_MESSAGES__
				debug_out("connect ok\r\n");
			#endif	

			/* disconnect */
			if(gsm_tcp_disconnect())
			{
				#ifdef __DEBUG_MESSAGES__
					debug_out("disconnect OK\r\n");
				#endif	

				/* read ip address from modem */
				if(gsm_get_ip_address(&modem))
				{
					#ifdef __DEBUG_MESSAGES__
						debug_out("Read IP address success\r\n");
						debug_out(modem.ip_addr);
					#endif	
				}
			}			
		}

		/* TODO: Not tested */
		else if(strstr(modem.tcpstatus, "tcp closing"))
		{
			#ifdef __DEBUG_MESSAGES__
				debug_out("tcp closing\r\n");
			#endif	

			/* read ip address from modem */
			if(gsm_get_ip_address(&modem))
			{
				#ifdef __DEBUG_MESSAGES__
					debug_out("Read IP address success\r\n");
					debug_out(modem.ip_addr);
				#endif	
			}			
		}

		/* TODO: Not tested */ 
		else if(strstr(modem.tcpstatus, "tcp closed"))
		{
			#ifdef __DEBUG_MESSAGES__
				debug_out("tcp closed\r\n");
			#endif				

			/* read ip address from modem */
			if(gsm_get_ip_address(&modem))
			{
				#ifdef __DEBUG_MESSAGES__
					debug_out("Read IP address success\r\n");
					debug_out(modem.ip_addr);
				#endif	
			}	
		}

		/* FIXME: Dont know what to do? If deactivated :( */
		else if(strstr(modem.tcpstatus, "pdp deact"))
		{
			// FIXME: What to do?
			#ifdef __DEBUG_MESSAGES__
				debug_out("pdp deactivated shutdown is required\r\n");
			#endif			
		}
		else
		{
			/* dont know what status it is */
			/* read ip address from modem */
			if(gsm_get_ip_address(&modem))
			{
				#ifdef __DEBUG_MESSAGES__
					debug_out("Read IP address success\r\n");
					debug_out(modem.ip_addr);
				#endif	
			}
		}

		#ifdef __DEBUG_MESSAGES__
			debug_out("gprs task completed, suspending now\r\n");
		#endif	

		/* suspend the task */
		vTaskSuspend(connectGPRSHandle);
	}
}

static void httpProc(void * pvParameters)
{
	uint8_t i;
	char rfid[10];
	char * path;
	path = (char *) malloc(32);
	for(;;)
	{
		/* check whether queue is created successfully */
	    if( httpQueue != 0 )
	    {
	    	/* Wait for queue */
	        if( xQueueReceive( httpQueue, ( rfid ), ( portTickType ) 10 ) )
	        {
	        	/* send card number to server and get validation result */
	        	strcpy(path, BASE);
	        	memcpy(&path[strlen(path)], rfid, 6);
	        	path[strlen(BASE) + 6] = NULL;
	        	
				if(gsm_http_get(URL, path))
	        	{
		        	if( xSemaphoreTake( debugSema, ( portTickType ) 50 ) == pdTRUE )
					{
						debug_out("HTTP OK\r\n");
						/*debug_out(path);
						debug_out("\r\n");*/
						xSemaphoreGive(debugSema);
					}
					/* {"id":18,"created_at":"2015-04-27T12:55:06.337Z","updated_at":"2015-04-28T02:45:33.063Z","amount":1000,"card":"1A2643","name":"person2"} */

					http_read_data(&modem);

					debug_out("DATA\r\n");
					debug_out(modem.httpdata);
					debug_out("\r\n");

					if(gsm_tcp_disconnect())
					{
						if( xSemaphoreTake( debugSema, ( portTickType ) 50 ) == pdTRUE )
						{
							debug_out("DISCONNECT OK\r\n");
							xSemaphoreGive(debugSema);
						}	
					}
					else
					{
						if( xSemaphoreTake( debugSema, ( portTickType ) 50 ) == pdTRUE )
						{
							debug_out("DISCONNECT OK\r\n");
							xSemaphoreGive(debugSema);
						}	
					}
				}

				else
	        	{
					if( xSemaphoreTake( debugSema, ( portTickType ) 50 ) == pdTRUE )
					{
						debug_out("HTTP FAIL\r\n");
						xSemaphoreGive(debugSema);
					}
	        	}
		  
	        	
	        	/* wait for modem semaphore */
	        	// if(xSemaphoreTake(modemSema, (portTickType) 10) == pdTRUE)
	        	// {
	        	// 	/* got resource */

	        	// 	 send card number to server and get validation result 
	        		

	        	// }
	        	// else
	        	// {
	        	// 	/* resource busy */
	        	// }
			}
			else
				modem_flush_rx();
		}
	}
}


static void updateModemStatus(void * pvParameters)
{
	for(;;)
	{

	}	
}

static void displayProcess(void * pvParameters)
{
	for(;;)
	{

	}	
}

static void scanCard(void * pvParameters)
{
	uint8_t len;
	char card_no[15];
	char rfid[15];
	uint8_t i = 0;
	for(;;)
	{
		while(i != 12)
		{
			if(uart0_fifo.num_bytes > 0)
				card_no[i++] = uart0_getc();
		}

		char * c = &card_no[4];
		memcpy(rfid, c, 6);
		//rfid[6] = NULL;

		i = 0;
		uart0_flushrx();

		/* send queue to http process */

		xQueueSend( httpQueue, ( void * ) rfid, ( portTickType ) 10 );
	}	
}



