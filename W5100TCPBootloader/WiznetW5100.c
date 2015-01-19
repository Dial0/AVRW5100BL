
#include "WiznetW5100.h"
#include <avr/io.h>
#include <util/delay.h>



unsigned char exchange_SPI(unsigned char send_data) 
{
	//hardware specific function, private function that is not in the header file
	//sends a byte then waits for transmission, then reads a byte
	
	unsigned char recv_data;
	SPDR = send_data;			//Put data in SPI Data Register
	while(!(SPSR & (1<<SPIF))); //wait for transmission to finish
	recv_data = SPDR;			//Retrieve the reply data from data register
	return recv_data;
}

unsigned char wiznet_read_address(unsigned short address)
{
	//Private function that is not exposed in header
	//reads a byte from a 16bit register on the W5100 ethernet chip
	
	unsigned char data;
	PORTB &= ~(1<<PB4);						//Chip select
	exchange_SPI(0x0F);						//read address command
	
	exchange_SPI((address & 0xFF00) >> 8);  //send 16bit address
	exchange_SPI((address & 0x00FF));		//only dummy data is returned, which can be ignored
	
	data = exchange_SPI(0x00);				//read the byte, 0x00 is dummy data to drive the SPI clock
	
	PORTB |= (1<<PB4);						//Chip deselect
	
	return data;
}

void wiznet_write_address(unsigned short address,unsigned char data )
{
	//Private function that is not exposed in header
	//write a byte to a 16bit register on the W5100 ethernet chip
	
	PORTB &= ~(1<<PB4);						//Chip select
	
	exchange_SPI(0xF0);						//write address command
	
	exchange_SPI((address & 0xFF00) >> 8);	//send 16bit address
	exchange_SPI((address & 0x00FF));
			
	exchange_SPI(data);						//send data byte
	
	PORTB |= (1<<PB4);						//Chip deselect
	
	return;
}

void wiznet_set_config(const unsigned char gateway_ip[4], const unsigned char subnet_mask[4],
					   const unsigned char mac_address[6], const unsigned char device_ip_address[4])
{

	wiznet_write_address(0x0001,gateway_ip[0]);
	wiznet_write_address(0x0002,gateway_ip[1]);
	wiznet_write_address(0x0003,gateway_ip[2]);
	wiznet_write_address(0x0004,gateway_ip[3]);
	
	wiznet_write_address(0x0005,subnet_mask[0]);
	wiznet_write_address(0x0006,subnet_mask[1]);
	wiznet_write_address(0x0007,subnet_mask[2]);
	wiznet_write_address(0x0008,subnet_mask[3]);
	
	wiznet_write_address(0x0009,mac_address[0]);
	wiznet_write_address(0x000A,mac_address[1]);
	wiznet_write_address(0x000B,mac_address[2]);
	wiznet_write_address(0x000C,mac_address[3]);
	wiznet_write_address(0x000D,mac_address[4]);
	wiznet_write_address(0x000E,mac_address[5]);
	
	wiznet_write_address(0x000F,device_ip_address[0]);
	wiznet_write_address(0x0010,device_ip_address[1]);
	wiznet_write_address(0x0011,device_ip_address[2]);
	wiznet_write_address(0x0012,device_ip_address[3]);
}

void wiznet_init(void)
{
	//WizNet W5100 Ethernet Chip Select is on PB4
	DDRB |= (1<<PB4);				//set PB4 as output
	PORTB |= (1<<PB4);				//Deselect W5100
	
	//Setup SPI
	DDRB |= (1<<PB0);				//SPI Chipselect, set to output	ensure SPI operates in Master Mode			
	DDRB |= (1<<PB1) | (1<<PB2);	//SPI CLK and MOSI, set as outputs
	DDRB &= ~(1<<PB3);				//MISO input
	
	
	//SPI Control Register
	SPCR = (1<<SPE)|(1<<MSTR);		//Enable SPI and Set to Master Mode
									// CPOL and CPHA are 0, Speed set to OSC/4 (OSC/2 when SPI2X enabled), No interrupts enabled
									
	//SPI Status Register								
	SPSR = (1<<SPI2X);				//Set SPI Double Speed, making speed OSC/2 (8MHz, using 16MHz external clock) 					
	
	
	wiznet_write_address(0x0000,0x80); //Write Reset CMD to Wiznet CMD register
	
	
}



unsigned short wiznet_Rx_size(unsigned short Socket_offset)
{
	//read size of data in receive buffer, which is stored in 2 bytes,
	//making a 16bit unsigned value
	unsigned char msb = wiznet_read_address(0x0426+Socket_offset);
	unsigned char lsb = wiznet_read_address(0x0427+Socket_offset);
	unsigned short rx_size = ((msb & 0x00ff)<<8) + lsb; 
	return rx_size;
}

unsigned short wiznet_Tx_size(unsigned short offset)
{
	//read size of available space in transmit buffer,
	//which is stored in 2 bytes, making a 16bit unsigned value
	unsigned char msb = wiznet_read_address(0x0420+offset);
	unsigned char lsb = wiznet_read_address(0x0421+offset);
	unsigned short tx_buffer = ((msb & 0x00ff)<<8) + lsb;
	return tx_buffer;
}

void wiznet_socket_listen(unsigned short offset, unsigned short port)
{
	//split the 16bit unsigned short into 2 bytes
	unsigned char source_port[2];
	source_port[0] = (port >> 8) & 0xFF;
	source_port[1] = port & 0xFF;

	wiznet_write_address(0x0400+offset,0x01); //set socket to tcp mode
	
	wiznet_write_address(0x0404+offset,source_port[0]); //write the 2 bytes to the source port registers
	wiznet_write_address(0x0405+offset,source_port[1]);
	
	wiznet_write_address(0x0401+offset,0x01); //open
	wiznet_write_address(0x0401+offset,0x02); //Listen
	
}


unsigned short wiznet_send_tcp(unsigned char* data, unsigned short data_size,unsigned short socket)
{
	//returns the amount of bytes written to the W5100 chip
	
	
	//read the socket status register to check the status of the connection
	unsigned char sock_status = wiznet_read_address(0x0403+socket);
	
	if(sock_status != 23) //if socket is not open
	{
		//0 bytes written, cannot send data if there is no active connection
		return 0;
	}
	
	unsigned char msb = wiznet_read_address(0x0424+socket);
	unsigned char lsb = wiznet_read_address(0x0425+socket);
	unsigned short tx_wr_pt = ((msb & 0x00ff)<<8) + lsb; //find the location of the write pointer
	
	for(unsigned short i = 0 ; i< data_size;i++)
	{
		//write the data, the real address is calculated from the relative write pointer
		//0x4000 is the offset for the transmit buffers, and 0x07FF is the buffer size
		//and the relative address with buffer size to cause writing to the buffer to wrap around
		//if it exceeds the buffer size
		unsigned short RelativeAddr = (tx_wr_pt+i) & 0x07FF; 
		unsigned short SocketOffset = socket*8; //correct the offset for the selected socket
		unsigned short realaddr = 0x4000 + SocketOffset + RelativeAddr; //base + socket + relative buffer address
		wiznet_write_address(realaddr,data[i]);
	}
	wiznet_write_address(0x0424+socket,((tx_wr_pt+data_size) & 0xFF00)>>8);
	wiznet_write_address(0x0425+socket,(tx_wr_pt+data_size) & 0x00FF);
	
	wiznet_write_address(0x0401+socket,0x20); //Write CMD to transmit the data in the buffer and update the write pointer
	
	return data_size;
}

void wiznet_receive_tcp(unsigned char *buffer, unsigned short read_amount, unsigned short Socket)
{
	//the Socket_offset variable is used to select which TCP socket is read

	//get address of receive buffer read pointer
	unsigned char msb = wiznet_read_address(0x0428+Socket);
	unsigned char lsb = wiznet_read_address(0x0429+Socket);
	unsigned short rx_re_pt = ((msb & 0x00ff)<<8) + lsb;			
	
	for(int i = 0 ; i<read_amount;i++)
	{
		//calculate the read address in the W5100 chip as the pointer only give a relative address
		// 0x6000 is the receive buffers offset, 0x07FF is the receive buffer size
		unsigned short RelativeAddr = (rx_re_pt+i) & 0x07FF;
		unsigned short SocketOffset = Socket*8;
		unsigned short realaddr = 0x6000 + SocketOffset + RelativeAddr;
		
		buffer[i] = wiznet_read_address(realaddr); //read byte into buffer and increment
	}
	
	wiznet_write_address(0x0428+Socket,((rx_re_pt+read_amount) & 0xFF00)>>8);	//set new pointer address (high and low byte)
	wiznet_write_address(0x0429+Socket,(rx_re_pt+read_amount) & 0x00FF);
	wiznet_write_address(0x0401+Socket,0x40);									//send received data cmd, (updates the receive buffer pointer)
}




