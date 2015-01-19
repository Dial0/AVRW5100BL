
#include <util/delay.h>		//Delay functions (_delay_ms,_delay_us)
#include <avr/io.h>			//Macros for AVR registers (e.g. DDRB, PORTB)
#include <avr/interrupt.h>	//Interrupt Vectors and Macros (TIMER1_COMPA_vect)
#include <avr/pgmspace.h>	//Macros for reading Program Flash memory (pgm_read_byte_far)
#include <avr/boot.h>		//Boot loader macros
#include <avr/wdt.h>		//Watchdog Timer macros

#include "WiznetW5100.h"			//Wiznet W5100 ethernet chip driver functions

//Wiznet W5100 chip has 4 available sockets
//0x0000 socket 0, 0x0100 socket 1, 0x0200 socket 2, 0x0300 socket 3
static const unsigned short Sock_Offset = 0x0000; //using socket 0
static const unsigned short Listen_Port = 13005;  //13005 TCP port

//Wiznet W5100 Network Configuration
unsigned char gateway_ip[4] = {10,0,0,28};
unsigned char subnet_mask[4] = {255,255,255,0};
unsigned char mac_address[6] = {0xDE,0xAD,0xBE,0xEF,0xFE,0xED};
unsigned char device_ip_address[4] = {10,0,0,75};

unsigned char Sec_Timeout;

//This EEPROM value is only used to create a EEPROM file that is written to the MCU
//during programming of the boot loader, this value is NOT initialized when the boot loader starts
unsigned char EEMEM Programmed = 0x00;

//18 bytes for storing TCP/IP connection setup
unsigned char EEMEM GateWayIP [4] = {10,0,0,28};
unsigned char EEMEM SubNetMask [4] = {255,255,255,0};
unsigned char EEMEM MacAddress [6] = {0xDE,0xAD,0xBE,0xEF,0xFE,0xED};
unsigned char EEMEM DeviceIPAddress [4] = {10,0,0,75};

ISR(TIMER1_COMPA_vect)	//One second timer interrupt
{
	if(Sec_Timeout > 0)	
	{
		//only decrement the timeout if its above 0
		//as the timeout check is a check to see if Sec_Timeout is 0
		
		PORTB ^= 1 << 7; //flash/toggle the LED each second to signal we are in boot loader mode
		Sec_Timeout--;
	}
}

//Write IP settings into EEPROM
void write_IP_EEPROM(unsigned char gateway_ip[4],unsigned char subnet_mask[4],
					 unsigned char mac_address[6],unsigned char device_ip_address[4])
{
	eeprom_update_block((const void*)gateway_ip , GateWayIP, 4);
	eeprom_update_block((const void*)subnet_mask , SubNetMask, 4);
	eeprom_update_block((const void*)mac_address , MacAddress, 6);
	eeprom_update_block((const void*)device_ip_address , DeviceIPAddress, 4);
}

//Read IP settings from EEPROM
void read_IP_EEPROM(unsigned char gateway_ip[4],unsigned char subnet_mask[4],
					unsigned char mac_address[6], unsigned char device_ip_address[4])
{
	eeprom_read_block((void*)gateway_ip , GateWayIP, 4);
	eeprom_read_block((void*)subnet_mask , SubNetMask, 4);
	eeprom_read_block((void*)mac_address , MacAddress, 6);
	eeprom_read_block((void*)device_ip_address , DeviceIPAddress, 4);
}

void boot_program_page (uint32_t page, uint8_t *buf)
{
	uint8_t sreg = SREG;	//Save current interrupts
	
	cli();					//Disable interrupts
	
	uint32_t Page_Add = page*SPM_PAGESIZE; //get the first address of the selected page
	
	eeprom_busy_wait ();
	boot_page_erase (Page_Add);
	boot_spm_busy_wait ();	//Wait until the memory is erased
	
	for (uint16_t i=0; i<SPM_PAGESIZE; i+=2)
	{
		//Set up little-endian word, and write one word at a time
		uint16_t w = *buf++;
		w += (*buf++) << 8;
		boot_page_fill (Page_Add + i, w);
	}
	boot_page_write (Page_Add); //Store buffer in flash page
	boot_spm_busy_wait();	//Wait until the memory is written
	
	SREG = sreg;			//Restore interrupts (if any were set)
}

void boot_read_page (uint32_t page, uint8_t *buf)
{
	boot_rww_enable ();		//Enable reading of main flash
	while(boot_rww_busy());	//Wait till main flash section is ready for reading
	uint32_t Page_Add = page*SPM_PAGESIZE; //get the first address of the selected page
	
	//read the page one byte at a time into the buffer
	for (uint16_t i = 0; i < SPM_PAGESIZE; i++ )
	{
		buf[i] = pgm_read_byte_far((Page_Add)+i);
	}
}

int main(void)
{
	//These three lines must be run first at startup,
	//to ensure the MCU does not get stuck in a reset loop
	
	cli();				//Disable all interrupts
	MCUSR = 0;			//Clear the MCU Status Register, this removes any reset condition flags
	wdt_disable();		//Disable the watchdog timer
	
	
	
	DDRB |= 1<<7;		//Set PortB 7 as output
						//This pin has an LED connected, that is flashed to indicate the bootloader is running
	
	//these two writes to MCUCR must occur within 4 cycles
	MCUCR = (1<<IVCE);	//Enable changing of interrupt vectors
	MCUCR = (1<<IVSEL); //Set interrupt vectors to use boot loader section
	
	//SD Card Chip Select is on PG5
	DDRG |= (1<<PG5);				//Set PG5 as output
	PORTG |= (1<<PG5);				//Set high to deselect SD card
	
	_delay_ms(200);		//loop doing nothing for 200ms,
						//to give the Wiznet chip some time to startup after a cold boot
	
	//Read the IP Settings from EEPROM into sram
	read_IP_EEPROM(gateway_ip,subnet_mask,mac_address,device_ip_address);
	
	//Setup the Wiznet Ethernet Chip
	wiznet_init();
	wiznet_set_config(gateway_ip,subnet_mask,mac_address,device_ip_address);
	wiznet_socket_listen(Sock_Offset,Listen_Port);	//Setup a TCP listener port
	
	Sec_Timeout = 10;	//Set the timeout value, when this expires, boot loader will jump to main code
	
	TCCR1A = 0x00;
	TCCR1B = (1<<WGM12) | (1<<CS12) | (0<<CS11) | (1<<CS10); // Set clk/1024 (15625Hz at 16MHz), and also clear timer on compare match(CTC)
	TCCR1C = 0x00;
	OCR1A = 15625;			//Set compare register to 15625 to give a 1Hz interrupt
	TIMSK1 = (1<<OCIE1A);	//enable output compare interrupt enable
		
	sei(); //Global enable interrupts
	
	enum {WAIT_START, HEADER, PROGRAMMING, VERIFY, OK, END, IPSET} status;
	status = WAIT_START;
	
	
	//buffer for ethernet communication
	//same size as a page, allowing reading/writing one page at a time
	uint8_t Buffer[SPM_PAGESIZE]; 
	
	uint32_t PageIndex = 0;	//current active page
	uint32_t Pages = 0;		//total pages to verify/write
	
    while(1)
    {
		
		if(Sec_Timeout == 0)
		{
			//if timeout exceeded then
			//set case to END
			status = END;
			
			//Flash the LED fast to signal bootloader exit
			for(uint8_t i = 0; i< 10; i++)
			{
				PORTB |= 1<<7;
				_delay_ms(100);
				PORTB &= ~(1 << 7);
				_delay_ms(100);
			}
		}

		unsigned short RX_Data = wiznet_Rx_size(Sock_Offset);	//Get the amount of data in the receive buffer
		unsigned short TX_Size = wiznet_Tx_size(Sock_Offset);	//Get the free space in the transmit buffer
		
		switch (status)
		{
			case WAIT_START:
			{
				if(RX_Data >= 4)
				{
					//Must receive a PROG command to start the process of writing a new application
					wiznet_receive_tcp(Buffer,4,Sock_Offset);
					if(Buffer[0] == 'P' && Buffer[1] == 'R' && Buffer[2] == 'O' && Buffer[3] == 'G')
					{
						unsigned char Version_Reply[] = "V1.0\r\n";			//send the version of the logger/bootloader
						wiznet_send_tcp(Version_Reply,6,Sock_Offset);		//5 bytes to include the null termination
						Sec_Timeout = 10;									//reset the timeout to 10 seconds
						status = HEADER;
					}
					else if(Buffer[0] == 'I' && Buffer[1] == 'P' && Buffer[2] == 'S' && Buffer[3] == 'T')
					{
						unsigned char Version_Reply[] = "V1.0\r\n";			//send the version of the logger/bootloader
						wiznet_send_tcp(Version_Reply,6,Sock_Offset);		//5 bytes to include the null termination
						Sec_Timeout = 10;									//reset the timeout to 10 seconds
						status = IPSET;
					}
				}
				break;
			}
			case IPSET: //update the TCP/IP settings
			{
				if(RX_Data >= 18) //wait for full TCP/IP settings packet
				{
					wiznet_receive_tcp(Buffer,18,Sock_Offset);
								
					//Packet Structure - 18 bytes
					//----------------------
					//Packet[0-3] - GateIP 4 bytes
					//Packet[4-7] - SubMask 4 bytes
					//Packet[8-13] - MacAdd 6 bytes
					//Packet[14-17] - DeviceIP 4 bytes
					//----------------------
								
					//copy packet data into correct arrays
					for (int i = 0; i< 6; i++)
					{
						if(i < 4)
						{
							gateway_ip[i] = Buffer[i];
							subnet_mask[i] = Buffer[i+4];
							device_ip_address[i] = Buffer[i+14];
						}
						mac_address[i] = Buffer[i+8];
					}
								
					write_IP_EEPROM(gateway_ip,subnet_mask,mac_address,device_ip_address);
								
					Sec_Timeout = 0; //set timeout to zero to restart the MCU and load new IP settings
					status = WAIT_START;
				}
				break;
			}
			case HEADER:
			{
				if(RX_Data >= 4)
				{
					//the header is a 32bit value (4 bytes) that indicates the amount of pages to be written
					wiznet_receive_tcp(Buffer,4,Sock_Offset);
					Pages = (uint32_t)Buffer[0] + ((uint32_t)Buffer[1]<<8) + ((uint32_t)Buffer[2]<<16) + ((uint32_t)Buffer[3]<<24);
					
					//clear the programmed status, as the program can no longer be guaranteed to be OK
					eeprom_write_byte(&Programmed,0x00); 
					Sec_Timeout = 10; //reset the timeout
					status = PROGRAMMING;
				}
				break;
			}
			case PROGRAMMING:
			{
				//check there is a full page ready in the receive buffer
				if(RX_Data >= SPM_PAGESIZE)
				{
					//write one page at a time and increment the page index
					wiznet_receive_tcp(Buffer,SPM_PAGESIZE,Sock_Offset);
					boot_program_page(PageIndex,Buffer);
					PageIndex++;
				}
				
				if(PageIndex == Pages) 
				{
					//when all pages have been written
					PageIndex = 0;		//reset the index to be used in verifying
					Sec_Timeout = 10;	//reset timer
					status = VERIFY;	//Go to verify case
					break;
				}
				
				break;
			}
			case VERIFY:
			{
				//check there is enough room in the transmit buffer for a full page
				if(TX_Size >= SPM_PAGESIZE)
				{
					//read a full page then transmit it over TCP
					boot_read_page(PageIndex,Buffer);
					int16_t bytes_written = wiznet_send_tcp(Buffer,SPM_PAGESIZE,Sock_Offset);
					if(bytes_written == 0)
					{
						//connection must have been lost
						status = END;
					}
					PageIndex++;
				}

				if(PageIndex == Pages)
				{
					//wait for the OK message
					Sec_Timeout = 10;	//reset timer
					status = OK;
				}
				break;
			}
			case OK:
			{
				if(TX_Size >= 2)
				{
					wiznet_receive_tcp(Buffer,2,Sock_Offset);
					if(Buffer[0] == 'O' && Buffer[1] == 'K')
					{
						eeprom_write_byte(&Programmed,0x01); //Program is OK, set Programmed byte
						Sec_Timeout = 10; //reset the timeout
						status = END;
					}
				}
				break;
			}
			case END:
			{
				if(eeprom_read_byte(&Programmed))
				{
					//Disable all interrupts
					cli();
									
					//reset the ports
					PORTB = 0x00;
					DDRB = 0x00;
					PORTG = 0x00;
					DDRG = 0x00;
									
					//Reset SPI registers
					SPCR = 0x00;
					SPSR = 0x00;
									
					//reset Timer settings
					TCCR1A = 0x00;
					TCCR1B = 0x00;
					TCCR1C = 0x00;
					OCR1A = 0x00;
					TIMSK1 = 0x00;
									
					//Put interrupt vectors back in main flash land
					//these two writes must occur within 4 cycles
					MCUCR = (1<<IVCE);
					MCUCR = 0;
									
					// enable the main flash for use by the main application
					boot_rww_enable ();
					//if MCU is successfully programmed
					//Jump to the application
					asm("jmp 0000");
				}
				else
				{
					//there was an error with programming
					//don't try to run code
					//wait for a new program to be uploaded
					status = WAIT_START;
					Sec_Timeout = 10;
					break;
				}
				

			}
		}

    }
}