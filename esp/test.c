
#include "esp8266.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdint.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <string.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>


static ESP_AccessPoint_List_t list;


typedef enum {
	mch_port1901,
	mch_port10000,
} myChannels;


static int mainstate;
static int start;



static int cnt1, cnt2, cnt3;


void mainautomat(ESP_CallBack_Data_t *data)
{
	switch (data->type) {
		case ESP_Cmd_Receive: {
			ESP_Channel_t *ch = (ESP_Channel_t *)data->data;
			if (!ch) return;
			printf("recvd: %d , %#x, %hu, %d\n", ch->id, ch->data.remoteIP, ch->data.remotePort, ch->data.size);
			if (ch->id == mch_port1901) {
				printf("brcst recvd\n");
				char *buff = "my ip";
				ESP_Channel_t channel = {
						.id = mch_port1901,
						.data = {
								.buf = buff,
								.size = 5,
								.remoteIP = ch->data.remoteIP,
								.remotePort = ch->data.remotePort
						}
				};
				ESP_Send(&channel);
			} else {
				char *buff = "ans";
				ESP_Channel_t channel = {
						.id = mch_port10000,
						.data = {
								.buf = buff,
								.size = 50,
								.remoteIP = ch->data.remoteIP,
								.remotePort = ch->data.remotePort
						}
				};
				ESP_Send(&channel);
				cnt2++;
			}
		} break;
		default: {
			switch (mainstate) {
				case 0: {
					ESP_ChannelParams_t params = {
							.id = mch_port1901,
							.type = ESP_ChT_UDP,
							.tprms = {
									.udp = {
											.ownPort = 1901,
											.mode = ESP_ChUM_Var
									}
							}
					};
					ESP_Open(&params);
					mainstate = 1;
				} break;
				case 1: {
					ESP_ChannelParams_t params = {
							.id = mch_port10000,
							.type = ESP_ChT_UDP,
							.tprms = {
									.udp = {
											.ownPort = 10000,
											.mode = ESP_ChUM_Var
									}
							}
					};
					ESP_Open(&params);
					mainstate = 2;
				} break;
				case 2: {
					ESP_IGMPJoin(str_to_ip("239.255.255.251"));
					mainstate = 10;
				} break;
				case 10: {
					usleep(1000000);
					start = 1;
					mainstate = 11;
				} break;
				default: {
					printf("unexp event: %d\n", data->type);
				} break;
			}
		} break;
	}
}


void callback(ESP_CallBack_Data_t *data)
{
	if (data->res != ESP_Ok) {
		puts("error\n");
		return;
	}
	switch (data->type) {
		case ESP_Cmd_Init: {
			ESP_Find(ESP_APLL_S_SSID);
		} break;
		case ESP_Cmd_Find: {
			ESP_AccessPoint_List_t *ret = (ESP_AccessPoint_List_t *)data->data;
			memcpy(&list, ret, sizeof(ESP_AccessPoint_List_t));
			ESP_AccessPoint_Node_t *node;
			for (node = ret->head; node; node = node->next) {
				if ( strcmp("WIFI_HMI", node->ap.name) == 0 ) {
					ESP_Connect(node->ap.name, "12345678");
				}
			}
		} break;
		case ESP_Cmd_IGMPJoin:
		case ESP_Cmd_Receive:
		case ESP_Cmd_Connect:
		case ESP_Cmd_Open: {
			mainautomat(data);
		} break;
		case ESP_Cmd_Send: {
			;
		} break;
		default: {
			puts("error\n");
		}
		break;
	}
}



void test(void);



int main(int argc, char **argv)
{
	ESP_Init_t initStr;
	ESP_Create();
	initStr.nmode = ESP_NM_DHCP;
	initStr.callback = callback;
	ESP_Init(&initStr);

//	start = 1;

//	for (;;) {
//		usleep(10000);
//		if (start) {
//			test();
//			return 0;
//		}
//	}

	printf ("started\n");

	for (;;) {
			usleep(10000);
	}

	ESP_DeInit();

	return 0;
}



char msg[] = "\x4e\x4f\x54\x49\x46\x59\x20\x2a\x20\x48\x54\x54\x50\x2f\x31\x2e\x31\x0d\x0a\x48\x4f\x53\x54\x3a\x20\x32\x33\x39\x2e\x32\x35\x35\x2e\x32\x35\x35\x2e\x32\x35\x30\x3a\x31\x39\x30\x30\x0d\x0a\x43\x41\x43\x48\x45\x2d\x43\x4f\x4e\x54\x52\x4f\x4c\x3a\x20\x6d\x61\x78\x2d\x61\x67\x65\x3d\x31\x30\x30\x0d\x0a\x4c\x4f\x43\x41\x54\x49\x4f\x4e\x3a\x20\x68\x74\x74\x70\x3a\x2f\x2f\x31\x39\x32\x2e\x31\x36\x38\x2e\x31\x2e\x31\x3a\x31\x39\x30\x30\x2f\x69\x67\x64\x2e\x78\x6d\x6c\x0d\x0a\x4e\x54\x3a\x20\x75\x75\x69\x64\x3a\x39\x66\x30\x38\x36\x35\x62\x33\x2d\x66\x35\x64\x61\x2d\x34\x61\x64\x35\x2d\x38\x35\x62\x37\x2d\x37\x34\x30\x34\x36\x33\x37\x66\x64\x66\x33\x37\x0d\x0a\x4e\x54\x53\x3a\x20\x73\x73\x64\x70\x3a\x61\x6c\x69\x76\x65\x0d\x0a\x53\x45\x52\x56\x45\x52\x3a\x20\x69\x70\x6f\x73\x2f\x37\x2e\x30\x20\x55\x50\x6e\x50\x2f\x31\x2e\x30\x20\x54\x4c\x2d\x57\x52\x39\x34\x31\x4e\x2f\x32\x2e\x30\x0d\x0a\x55\x53\x4e\x3a\x20\x75\x75\x69\x64\x3a\x39\x66\x30\x38\x36\x35\x62\x33\x2d\x66\x35\x64\x61\x2d\x34\x61\x64\x35\x2d\x38\x35\x62\x37\x2d\x37\x34\x30\x34\x36\x33\x37\x66\x64\x66\x33\x37\x0d\x0a\x0d\x0a";



static void _m_recv(int sock, struct sockaddr_in *addr)
{
	unsigned int cnt = sizeof(struct sockaddr);
	char buf[1024];
	struct pollfd pfd;
	pfd.fd = sock;
	pfd.events = POLLIN;
	int pollres = poll(&pfd, 1, 100);
	if (pollres > 0) {
		memset(addr, 0, sizeof(struct sockaddr_in));
		size_t bytes_read = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)addr, &cnt);
		buf[bytes_read] = '\0';
		puts(buf);
		puts("\n");
	} else {
		puts(" ********************************* tst: timeout recv\n");
		cnt3++;
	}
}



static int _m_sock_open(uint16_t port, int brcst)
{
	int sock;
	struct sockaddr_in addr;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("err: socket");
		return 0;
	}

	if (brcst) {
		int broadcastEnable = 1;

		if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
			printf("err: brcst\n");
			return 0;
		}
	}

	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "eth1");

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	ifr.ifr_addr = ( *((struct sockaddr *)&addr) );
	if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, (void *)&ifr, sizeof(ifr)) < 0) {
		printf("err: bind\n");
		return 0;
	}

	return sock;
}



void test(void)
{
	char remoteIP[16];
	int state = 0;
	int sock = 0;
	struct sockaddr_in addr;

	while (1) {
		switch (state) {
			case 0: {
				sock = _m_sock_open(20000, 1);

				addr.sin_family = AF_INET;
				addr.sin_port = htons(1901);
				addr.sin_addr.s_addr = inet_addr("192.168.1.255");
				puts("first sent\n");
				sendto(sock, msg, sizeof(msg), 0, (struct sockaddr*)&addr, sizeof(addr));

				state = 1;
			} break;
			case 1: {
				_m_recv(sock, &addr);

//				addr.sin_addr.s_addr = inet_addr("192.168.1.102");
				strcpy(remoteIP, inet_ntoa(addr.sin_addr));
				printf("esp ip: %s\n", remoteIP);
				state = 2;
			} break;
			case 2: {
				for (;;) {
					cnt1++;
					addr.sin_port = htons(10000);
					const size_t size = 500; //ESP_CHANNEL_DATA_MAX_SIZE;
					char buf[size];
					size_t i;
					for (i = 0; i < size; ++i)
						buf[i] = i%10 + 0x30;
					sendto(sock, buf, size, 0, (struct sockaddr*)&addr, sizeof(addr));
					_m_recv(sock, &addr);
				}
			} break;
			case 3: {
				usleep(1000000);
				close(sock);
				return;
			} break;
		}
	}
}




