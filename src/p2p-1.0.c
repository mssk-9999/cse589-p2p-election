/*
 * p2p0.9.cc
 *
 *  Created on: Nov 19, 2011
 *      Author: zwx
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <stdlib.h>
#include <netdb.h>

#define MAX_BUFFER_SIZE	1024
#define TOKEN_LEN 10
#define MAX_PEER_NUM 2
#define PRIVATE	0x00
#define BROADCAST 0x01
#define SALUTE	0x10
#define	oops(msg) {printf("oops: "); perror(msg); exit(-1);}

int node_num; //total nodes number in the network, include host itself
int peer_num; //adjacent peer count
int pmc; //private message count
int bmc; //broadcast message count
int max_fd;
fd_set read_fds;
struct host_info {
	int tcp_fd;
	int udp_fd;
	struct sockaddr_in tcp_addr;
	struct sockaddr_in udp_addr;
	socklen_t addr_len;
	char name[MAX_BUFFER_SIZE];
	char s_ip[MAX_BUFFER_SIZE];
	char self_token[TOKEN_LEN + 1];
} host;
struct peer_info {
	int tcp_fd;
	struct sockaddr_in tcp_addr;
	struct sockaddr_in udp_addr;
	socklen_t addr_len;
	char name[MAX_BUFFER_SIZE];
	char s_ip[MAX_BUFFER_SIZE];
} peer[MAX_PEER_NUM];
struct braodcast_message {
	int is_host;
	int msg_id;
	uint32_t ip;
	uint16_t udp_port;
	char token[TOKEN_LEN + 1];
	char s_ip[MAX_BUFFER_SIZE];
} broc_msg[MAX_PEER_NUM];

/*
 * get ip address from host name
 */
void get_ip(char* host_ip) {
	struct sockaddr_in hs, sv;
	socklen_t len;
	int fd;
	char name[MAX_BUFFER_SIZE];
	char ip[MAX_BUFFER_SIZE];

	len = sizeof(sv);
	memset(&hs, 0, sizeof(hs));
	hs.sin_family = AF_INET;
	hs.sin_port = htons((53));
	inet_pton(AF_INET, "8.8.8.8", &(hs.sin_addr));
	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		oops("google socket");
	}
	if (connect(fd, (struct sockaddr *)&hs, sizeof(hs)) < 0) {
		oops("google connect");
	}
	gethostname(name, MAX_BUFFER_SIZE);
	getsockname(fd, (struct sockaddr*)&sv, &len);
	inet_ntop(AF_INET, &sv.sin_addr, ip, MAX_BUFFER_SIZE);

	strcpy(host_ip, ip);
}

/*
 * extract ip and port from input string
 */
void extract_ip_port(const char* line, char* ip, int* port) {
	int i, p, q, w;
	int slen;
	char sport[MAX_BUFFER_SIZE];

	slen = strlen(line);
	i = p = q = w = -1;
	while (line[++i] != ' ' && i < slen)
		;
	p = i;
	while (line[++i] != ' ' && i < slen)
		;
	q = i;
	i = slen;
	while (line[--i] == ' ' && i >= 0)
		;
	w = i;
	strncpy(ip, line + p + 1, q - p - 1);
	ip[q - p - 1] = '\0';
	strncpy(sport, line + q + 1, w - q);
	sport[w - q] = '\0';
	*port = atoi(sport);
}

void info_handler() {
	printf("%s | %s | %d | %d\n", host.s_ip, host.name, ntohs(
			host.tcp_addr.sin_port), ntohs(host.udp_addr.sin_port));
}

/*
 * connect with peer
 */
int connect_to_peer(char* ip, int port) {
	peer[peer_num].tcp_addr.sin_family = AF_INET;
	peer[peer_num].tcp_addr.sin_port = htons((port));
	inet_pton(AF_INET, ip, &peer[peer_num].tcp_addr.sin_addr);
	if ((peer[peer_num].tcp_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		return -1;
	}
	if (connect(peer[peer_num].tcp_fd,
			(struct sockaddr *) &peer[peer_num].tcp_addr,
			sizeof(peer[peer_num].tcp_addr)) < 0) {
		return -1;
	}
	//get peer info
	peer[peer_num].addr_len = sizeof(peer[peer_num].tcp_addr);
	if (getpeername(peer[peer_num].tcp_fd,
			(struct sockaddr*) &peer[peer_num].tcp_addr,
			&peer[peer_num].addr_len) < 0) {
		oops("getpeername");
	}
	getnameinfo((struct sockaddr*) &peer[peer_num].tcp_addr,
			sizeof(peer[peer_num].tcp_addr), peer[peer_num].name,
			sizeof(peer[peer_num].name), NULL, 0, 0);
	strcpy(peer[peer_num].s_ip, ip);
	return 0;
}

void connect_handler(const char *str) {
	char ip[MAX_BUFFER_SIZE];
	int port;
	extract_ip_port(str, ip, &port);
	if (connect_to_peer(ip, port) < 0) {
		printf("connect to %s | %d failure\n", ip, port);
	} else {
		printf("connect to %s | %d success\n", ip, port);
		peer_num++;
	}
}

void show_conn_handle() {
	int i;
	if (peer_num == 0) {
		printf("NO CONNECTION YET\n");
		return;
	}
	for (i = 0; i < peer_num; i++) {
		printf("%d ", i + 1);
		printf("| %s ", inet_ntoa(peer[i].tcp_addr.sin_addr));
		printf("| %s ", peer[i].name);
		printf("| %d ", ntohs(host.tcp_addr.sin_port));
		printf("| %d\n", ntohs(peer[i].tcp_addr.sin_port));
	}
	fflush(stdout);
}

/*
 * private message
 */
void compose_private_msg(int type, int len, char* token, char* message) {
	int i;
	int guid;

	//random id
	srand(time(0));
	guid = rand() % 10000000;
	i = 6;
	message[7] = '0';
	while (guid != 0 && i >= 0) {
		message[i] = guid % 10 + '0';
		guid /= 10;
		i--;
	}
	while (i >= 0) {
		message[i] = '0';
		i--;
	}
	//message type
	message[8] = type + '0';
	//payload length
	strncpy(message + 9, "10", 2);
	//payload
	strncpy(message + 11, token, len);
	message[21] = '\0';
}

/*
 * handler of the command ready
 */
void ready_handler() {
	char message[22];
	char init_token[11];
	int i;

	if (peer_num == 0) {
		printf("NO CONNECTION EXISTS\n");
		return;
	} else {
		for (i = 0; i < peer_num; i++) {
			while (1) {
				printf("Please specify a 10 digit number: ");
				fflush(stdout);
				fgets(init_token, sizeof(init_token) * 11, stdin);
				if (strlen(init_token) < 11) {//10 digits and a '\n'
					printf("token length is not valid\n");
					fflush(stdout);
					continue;
				}
				init_token[10] = '\0';

				compose_private_msg(PRIVATE, 10, init_token, message);
				if (write(peer[i].tcp_fd, message, strlen(message)) < 0) {
					oops("write");
				}
				break;
			}
			printf("send initial token %s to %s | %d\n", init_token, inet_ntoa(
					peer[i].tcp_addr.sin_addr),
					ntohs(peer[i].tcp_addr.sin_port));
			fflush(stdout);
		}
	}
}

void self_token_handler() {
	if (pmc == peer_num && peer_num != 0) {
		printf("my token is %s\n", host.self_token);
	} else {
		printf("WAITING ON PEER TOKEN\n");
	}
}

void all_tokens_handler() {
	int i;
	int flag = 0;
	for (i=0; i<bmc; i++) {
		if (!broc_msg[i].is_host) {
			printf("%s | %d | %s\n", broc_msg[i].s_ip, broc_msg[i].udp_port, broc_msg[i].token);
			flag = 1;
		}
	}
	if (pmc == peer_num && peer_num != 0) {
		printf("%s | %d | %s\n", host.s_ip, ntohs(host.udp_addr.sin_port), host.self_token);
		flag = 1;
	}
	if (!flag) {
		printf("NO TOKENS ARE DETERMINED YET\n");
	}
}

void close_handler() {
	int i;
	printf("connections are closed\n");

	for (i=0; i<peer_num; i++) {
		close(peer[i].tcp_fd);
	}
	close(host.tcp_fd);
	close(host.udp_fd);
	exit (0);
}

void var_handler() {
	printf("pmc=%d, bmc=%d, node_num=%d, udp fd=%d, tcp fd=%d\n", pmc, bmc, node_num, host.udp_fd, host.tcp_fd);
}

void command(const char *str) {
	if (strcmp(str, "info") == 0) {
		info_handler();
	} else if (strncmp(str, "connect", 7) == 0) {
		connect_handler(str);
	} else if (strcmp(str, "show-conn") == 0) {
		show_conn_handle();
	} else if (strcmp(str, "ready") == 0) {
		ready_handler();
	} else if (strcmp(str, "self-token") == 0) {
		self_token_handler();
	} else if (strcmp(str, "all-tokens") == 0) {
		all_tokens_handler();
	} else if (strcmp(str, "exit") == 0) {
		close_handler();
	} else if (strcmp(str, "var") == 0) { 
		var_handler();
	}
	else {
		printf("UNKNOWN COMMAND\n");
	}
}

void host_init() {
	const int on = 1;

	//initialize the TCP socket
	srand(time(0));
	gethostname(host.name, MAX_BUFFER_SIZE);
	if ((host.tcp_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		oops("socket");
	}
	bzero((void *) &host.tcp_addr, sizeof(host.tcp_addr));
	host.tcp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	host.tcp_addr.sin_port = htons((rand() % 65536));
	host.tcp_addr.sin_family = AF_INET;

	if (setsockopt(host.tcp_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
		oops("secsockopt");
	}
	if (bind(host.tcp_fd, (struct sockaddr *) &host.tcp_addr,
			sizeof(host.tcp_addr)) != 0) {
		oops("bind");
	}
	if (listen(host.tcp_fd, MAX_PEER_NUM - 1) != 0) {
		oops("listen");
	}
	gethostname(host.name, MAX_BUFFER_SIZE);
	get_ip(host.s_ip);
	inet_aton(host.s_ip, &host.tcp_addr.sin_addr);
	//initialize the UDP socket
	if ((host.udp_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		oops("udp socket");
	}
	memset((void *)&host.udp_addr, 0, sizeof(host.udp_addr));
	host.udp_addr.sin_family = AF_INET;
	host.udp_addr.sin_port = htons((rand() % 65536));
	host.udp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(host.udp_fd, (struct sockaddr *) &host.udp_addr,
			sizeof(host.udp_addr)) < 0) {
		oops("udp bind");
	}
	inet_aton(host.s_ip, &host.udp_addr.sin_addr);
}

void resetfd() {
	int i;

	FD_ZERO(&read_fds);
	FD_SET(0, &read_fds);
	FD_SET(host.tcp_fd, &read_fds);
	FD_SET(host.udp_fd, &read_fds);
	max_fd = host.tcp_fd;
	max_fd = (max_fd > host.udp_fd) ? max_fd : host.udp_fd;
	for (i = 0; i < peer_num; i++) {
		if (peer[i].tcp_fd != -1)
		{
			FD_SET(peer[i].tcp_fd, &read_fds);
			max_fd = (max_fd > peer[i].tcp_fd) ? max_fd : peer[i].tcp_fd;
		}
	}
}

int is_dup_msg(int msg_id, int bmc) {
	int i;
	for (i = 0; i < bmc; i++) {
		if (msg_id == broc_msg[i].msg_id) {
			return 1;
		}
	}
	return 0;
}

/*
 * extract the header information of the message
 */
void get_msg_header(char* msg, int* id, int* type, int* len, char* payload) {
	char buffer[128];
	//id
	strncpy(buffer, msg, 7);
	buffer[7] = '\0';
	*id = atoi(buffer);
	//type
	strncpy(buffer, msg + 8, 1);
	buffer[1] = '\0';
	*type = atoi(buffer);
	//len
	strncpy(buffer, msg + 9, 2);
	buffer[2] = '\0';
	*len = atoi(buffer);
	//payload
	strncpy(payload, msg + 11, *len);
	payload[*len] = '\0';
}

void compose_salute_msg(int type, char* message) {
	int i;
	int guid;

	//random id
	srand(time(0));
	guid = rand() % 10000000;
	i = 6;
	message[7] = '0';
	while (guid != 0 && i >= 0) {
		message[i] = guid % 10 + '0';
		guid /= 10;
		i--;
	}
	while (i >= 0) {
		message[i] = '0';
		i--;
	}
	//message type
	message[8] = type + '0';
	//payload length
	strncpy(message + 9, "36", 2);
	//payload
	strncpy(message + 11, host.self_token, 10);
	strcpy(message + 21, "ALL HAIL THE MIGHTY LEADER");
	message[47] = '\0';
}

void salute_leader() {
	struct sockaddr_in addr;
	int i;
	int max;
	int s;
	int len;
	char max_token[TOKEN_LEN + 1];
	char message[MAX_BUFFER_SIZE];
	char s_ip[MAX_BUFFER_SIZE];

	//who is the leader?
	max = -1;
	strcpy(max_token, host.self_token);
	for (i = 0; i < bmc; i++) {
		if (!broc_msg[i].is_host && strcmp(max_token, broc_msg[i].token) < 0) {
			strcpy(max_token, broc_msg[i].token);
			max = i;
		}
	}
	if (max == -1) {
		printf("leader is me! my token is %s\n", host.self_token);
	} else {
		printf("press any key to salute the leader!\n");
		getchar();
		inet_ntop(AF_INET, &broc_msg[max].ip, s_ip, INET_ADDRSTRLEN);
		printf("leader is %s, token is %s\n", s_ip, broc_msg[max].token);
		//send salute message to the leader
		compose_salute_msg(SALUTE, message);
		if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)	{
			oops("socket");
		}
		len = sizeof(addr);
		memset((char*)&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(broc_msg[max].udp_port);
		inet_aton(broc_msg[max].s_ip, &addr.sin_addr);
		if (sendto(s, message, sizeof(message), 0, (struct sockaddr*)&addr, len) < 0) {
			oops("sendto");
		}
		printf("SALUTE TO %s | %d !\n", broc_msg[max].s_ip, broc_msg[max].udp_port);
	}
}

/*
 * private message handler
 */
void receive_private_msg(int fd, char* init_token) {
	int i;

	if (strcmp(host.self_token, "") == 0 || strcmp(host.self_token, init_token)
			< 0) {
		strcpy(host.self_token, init_token);
	}

	for (i = 0; i < peer_num; i++) {
		if (peer[i].tcp_fd == fd)
			break;
	}
	printf("received initial token %s from %s | %d\n", init_token, inet_ntoa(
			peer[i].tcp_addr.sin_addr), ntohs(peer[i].tcp_addr.sin_port));
	fflush(stdout);
}

void compose_broadcast_msg(char* message, int type, char* token, uint32_t ip,
		uint16_t uport) {
	int i;
	int guid;
	char sip[4];
	char suport[2];

	//random guid
	srand(time(0));
	guid = rand() % 10000000;
	i = 6;
	message[7] = '0';
	while (guid != 0 && i >= 0) {
		message[i] = guid % 10 + '0';
		guid /= 10;
		i--;
	}
	while (i >= 0) {
		message[i] = '0';
		i--;
	}
	//message type
	message[8] = type + '0';
	//payload length
	strncpy(message + 9, "16", 2);
	//token
	strncpy(message + 11, token, 10);
	//ip address
	sip[3] = (ip >> 24) & 0xFF;
	sip[2] = (ip >> 16) & 0xFF;
	sip[1] = (ip >> 8) & 0xFF;
	sip[0] = ip & 0xFF;
	strncpy(message + 21, sip, 4);
	//udp port
	printf("compose broc msg uport=%d\n", uport);
	suport[1] = (uport >> 8) & 0xFF;
	suport[0] = uport & 0xFF;
	strncpy(message + 25, suport, 2);
	message[27] = '\0';
}

/*
 * broadcast message handler
 */
void send_broadcast_msg(int msg_id) {
	char message[MAX_BUFFER_SIZE];
	int i;
	compose_broadcast_msg(message, BROADCAST, host.self_token,
			host.udp_addr.sin_addr.s_addr, ntohs(host.udp_addr.sin_port));
	
	printf("self token is determined: %s\n", host.self_token);
	fflush(stdout);
	for (i = 0; i < peer_num; i++) {
		if (write(peer[i].tcp_fd, message, strlen(message)) < 0) {
			oops("write");
		}
		printf("send broc msg %d to %s | %d\n", msg_id, inet_ntoa(
				peer[i].tcp_addr.sin_addr), ntohs(peer[i].tcp_addr.sin_port));
		fflush(stdout);
	}
}

void save_broc_msg(int is_host, int msg_id, char* token, uint32_t ip, uint16_t uport) {
	struct sockaddr_in sa;
	char s_ip[TOKEN_LEN + 1];

	broc_msg[bmc].is_host = is_host;
	broc_msg[bmc].msg_id = msg_id;
	strcpy(broc_msg[bmc].token, token);
	broc_msg[bmc].ip = ip;
	broc_msg[bmc].udp_port = uport;
	sa.sin_addr.s_addr = ip;
	inet_ntop(AF_INET, &(sa.sin_addr), s_ip, INET_ADDRSTRLEN);
	strcpy(broc_msg[bmc].s_ip, s_ip);
}

void receive_broadcast_msg(int msg_id, int fd, char *payload) {
	char token[11];
	//char sip[MAX_BUFFER_SIZE];
	uint32_t ip;
	uint16_t uport;
	int i;
	for (i = 0; i < peer_num; i++) {
		if (peer[i].tcp_fd == fd)
			break;
	}

	printf("receive broc msg %d from %s | %d\n", msg_id, inet_ntoa(
			peer[i].tcp_addr.sin_addr), ntohs(peer[i].tcp_addr.sin_port));
	fflush(stdout);

	strncpy(token, payload, 10);
	token[10] = '\0';
	ip = *(uint32_t *) (payload + 10);
	uport = *(uint16_t *) (payload + 14);
	//inet_ntop(AF_INET, &ip, sip, INET_ADDRSTRLEN);
	//printf("token is %s, ip is %s, udp port %d\n", stoken, sip, uport);
	//printf("udp port is %d\n", uport);
	save_broc_msg(0, msg_id, token, ip, uport);
}

void forward_msg(int msg_id, char* msg, int fd) {
	int i;
	for (i=0; i<peer_num; i++){
		if (peer[i].tcp_fd == fd)
			break;
	}
	printf("forward broc msg %d to %s | %d\n", msg_id, inet_ntoa(peer[i].tcp_addr.sin_addr), ntohs(
			peer[i].tcp_addr.sin_port));
	if (write(fd, msg, strlen(msg)) < 0) {
		oops("write");
	}
}

void receive_salute_msg(struct sockaddr_in* from, char* msg) {
	printf("SALUTE: %s. from %s | %d\n", msg + 10, inet_ntoa(from->sin_addr), ntohs(from->sin_port));
	fflush(stdout);
}

int main(int ac, char* av[]) {
	int len;
	int i, j;
	int msg_id;
	int msg_type;
	int msg_len;
	char msg_payload[MAX_BUFFER_SIZE];
	char buffer[MAX_BUFFER_SIZE];
	struct sockaddr_in from;
	socklen_t fromlen;

	pmc = 0;
	bmc = 0;
	peer_num = 0;
	node_num = atoi(av[1]);
	strcpy(host.self_token, "");
	host_init();
	resetfd();

	command("info");
	while (1) {
		if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
			oops("select");
		}
		for (i = 0; i < max_fd + 1; i++) {
			if (FD_ISSET(i, &read_fds)) {
				//read from stdin
				if (i == 0) {
					if ((len = read(0, buffer, MAX_BUFFER_SIZE)) < 0) {
						oops("read");
					}
					if (len == 0) {
						break;
					}
					buffer[len - 1] = '\0';
					command(buffer);
				}
				//new connection request
				else if (i == host.tcp_fd) {
					peer[peer_num].addr_len = sizeof(peer[peer_num].tcp_addr);
					if ((peer[peer_num].tcp_fd = accept(host.tcp_fd,
							(struct sockaddr*) &peer[peer_num].tcp_addr,
							&peer[peer_num].addr_len)) < 0) {
						oops("accept");
					}
					inet_ntop(AF_INET, &peer[peer_num].tcp_addr.sin_addr, peer[peer_num].s_ip,
							sizeof(peer[peer_num].s_ip));
					printf("connected with %s | %d\n", peer[peer_num].s_ip, ntohs(
							peer[peer_num].tcp_addr.sin_port));
					peer_num++;
				}
				//message from udp socket
				else if (i == host.udp_fd) {
					fromlen = sizeof(from);
					if ((len = recvfrom(i, buffer, MAX_BUFFER_SIZE, 0, (struct sockaddr*)&from, &fromlen)) < 0){
						oops("recvfrom\n");
					}
					buffer[len] = '\0';
					get_msg_header(buffer, &msg_id, &msg_type, &msg_len, msg_payload);
					receive_salute_msg(&from, msg_payload);
				//data from connected socket
				} else {
					if ((len = read(i, buffer, MAX_BUFFER_SIZE)) < 0) {
						oops("read");
					}
					//peer node closes the tcp connection
					//CAUTION: here maybe exists bugs 
					if (len == 0) {
						close_handler();
					}
					buffer[len] = '\0';
					get_msg_header(buffer, &msg_id, &msg_type, &msg_len,
							msg_payload);
					//receive private message
					if (msg_type == PRIVATE) {
						receive_private_msg(i, msg_payload);
						pmc++;
						if (pmc == peer_num) { //self token is determined, and send broadcast message
							save_broc_msg(1, msg_id, "", 0, 0);
							send_broadcast_msg(msg_id);
							bmc++;
						}
						if (bmc == node_num) {
							salute_leader();
						}
						//receive broadcast message
					} else if (msg_type == BROADCAST) {
						if (!is_dup_msg(msg_id, bmc)) {
							receive_broadcast_msg(msg_id, i, msg_payload);
							//forward this message to other peers
							for (j = 0; j < peer_num; j++) {
								if (peer[j].tcp_fd != i) {
									forward_msg(msg_id, buffer, peer[j].tcp_fd);
								}
							}
							//whether leader is determined
							bmc++;
							if (bmc == node_num) {
								salute_leader();
							}
						} else {
							//discard duplicate broadcast message
							printf("discard duplicate broc %d\n", msg_id);
						}
					} else {
						//receive salute message
						printf("UNKNOWN MESSAGE\n");
					}
				}
				resetfd();
				break;
			}
		}
	}

	close(host.udp_fd);
	close(host.tcp_fd);
	return 0;
}
