
#ifndef WIZNET_H_
#define WIZNET_H_

//setup for W5100, contains hardware specific settings
void wiznet_init(void);

//sets the network configuration on w5100
void wiznet_set_config(const unsigned char gateway_ip[4], const unsigned char subnet_mask[4],
const unsigned char mac_address[6], const unsigned char device_ip_address[4]);

//setup a TCP listener socket, on one of the 4 w5100 sockets, which is selected by the socket offset
//must specify a tcp port
void wiznet_socket_listen(unsigned short offset, unsigned short port);

//get the size of the receive buffer (amount of data in the buffer) in bytes
unsigned short wiznet_Rx_size(unsigned short offset);
//get the available size of the trasmit buffer in bytes
unsigned short wiznet_Tx_size(unsigned short offset);

//returns 0 if there is no established connection to send data, or returns the amount of data sent
//as these are low level functions it is important to check there is enough space in the buffer before
//attempting a transfer, otherwise older data will be overwritten
unsigned short wiznet_send_tcp(unsigned char* data, unsigned short data_size,unsigned short offset);

//receive tcp data from the w5100 receive buffer, if more data is requested that what is available
//older data that was previously in the buffer will be returned, so it is important to check the Rx Buffer
//before a receive tcp function is run
void wiznet_receive_tcp(unsigned char *buffer, unsigned short read_amount, unsigned short Socket_offset);


#endif /* WIZNET_H_ */