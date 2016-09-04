#include<stdio.h>
#include<errno.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>

#include<sys/socket.h>
#include<arpa/inet.h>
#include <time.h>		// gettimeofday 
#include <fcntl.h>		// fcntl F_SETFL  F_SETFL
// Default max packet size (1500, minus allowance for IP, UDP, UMTP headers)
// (Also, make it a multiple of 4 bytes, just in case that matters.)
#define MAX_PACKET_SIZE 1456
#define PORT "10000"
#define REMOTE_IP "127.0.0.1"
// 25 packet per second 100k  100k*25 = 2.5M/s 
#define FREQUENCY 25	// 1/1200 = 833us 

int main(int argc,char**argv){

	int ret = 0 ;
	unsigned char rbuf[256];
	unsigned char* wbuf = (unsigned char*)malloc(MAX_PACKET_SIZE);
	memset( wbuf, 0x55, MAX_PACKET_SIZE );
	int sock;
	struct sockaddr_in targetaddr;
 
 
	
	sock=socket(AF_INET,SOCK_STREAM,0);
	if(sock==-1){
		perror("socket error");
		exit(1);
	}else{
		printf("socket success\n");
	}
	bzero(&targetaddr,sizeof(struct sockaddr_in));
	targetaddr.sin_family=AF_INET;
	targetaddr.sin_port=htons(atoi(PORT));
	targetaddr.sin_addr.s_addr=inet_addr(REMOTE_IP);
	
	// 这里是阻塞连接 非阻塞可能离开返回EINPROGRESS连接进行中 三次握手 需要用select来判断socket是否可写 可写的话代表连接建立
	// 非阻塞下可能还要设置excetpionFds
	if(!connect(sock,(struct sockaddr *)&targetaddr,sizeof(struct sockaddr))){
		printf("connect ok\n");
		bzero(rbuf,sizeof(rbuf));
		if(read(sock,rbuf,sizeof(rbuf))>0){
			printf("%s\n",rbuf);
		}
	}else{
		perror("connect fail");
		exit(1);
	}
	
	// 获取socket Buf的大小
	unsigned int curSize;
	unsigned int sizeSize = sizeof curSize;
	if (getsockopt(sock, SOL_SOCKET, SO_RCVBUF,
		(char*)&curSize, &sizeSize) < 0) {
		printf("i'm client getsockopt  SO_RCVBUF error = %s\n" , strerror(errno) );
	}else{
		printf("i'm client sock rcvbuf_size  = %d\n" , curSize );
	}
	if (getsockopt(sock, SOL_SOCKET, SO_SNDBUF,
		(char*)&curSize, &sizeSize) < 0) {
		printf("i'm client getsockopt  SO_SNDBUF error = %s\n" , strerror(errno) );
	}else{
		printf("i'm client sock sndbuf_size  = %d\n" , curSize );
	}
						
						
	struct timeval start ; memset(&start,0,sizeof(struct timeval));
	struct timeval current ; memset(&current,0,sizeof(struct timeval));
	gettimeofday( &start, NULL );
	unsigned long start_time =   start.tv_sec * 1000uL * 1000uL +   start.tv_usec ;
	unsigned long current_time = 0L;
	unsigned long last_time = start_time;
	unsigned long transfer_each = 1 * 1000uL * 1000uL / FREQUENCY ; // us
	unsigned long delayus = 0 ;
	unsigned long packet_num = 0 ;
	printf("i'm client,transfer once between = %lu us \n",  transfer_each );
	
	// NON-BLOCK
	int curFlags = fcntl(sock, F_GETFL, 0);
	#if 1
	ret =  fcntl(sock, F_SETFL, curFlags|O_NONBLOCK);
	if(ret < 0 ){
		printf("i'm client,set socket nonblock fail ret = %d , %s\n" , ret , strerror(errno));
	}else{
		curFlags = fcntl(sock, F_GETFL, 0); 
		printf("i'm client,set socket nonblock done ret = %d   \n", ret  );	
	}
	#endif
	printf("i'm client,socket is block ? %s\n" , ((curFlags&O_NONBLOCK)?"true":"false"));
	
	// 这个是连接超时 linux下可以用于accept connnect read write
	// 设置socket超时而不阻塞fd 也会导致返回EAGAIN
	// set send TIME-OUT 
	struct timeval sock_timeout = {
		3,
		0};
	ret =  setsockopt(sock, SOL_SOCKET , SO_SNDTIMEO, &sock_timeout,sizeof(sock_timeout));
	if(ret < 0 ){
		printf("i'm client,set socket timeout fail ret = %d , %s\n" , ret , strerror(errno));
	}else{
		memset(&sock_timeout,0,sizeof(struct timeval));
		unsigned int timeval_size = sizeof(sock_timeout) ; 
		getsockopt(sock, SOL_SOCKET , SO_SNDTIMEO, &sock_timeout, &timeval_size);
		printf("i'm client,set socket timeout done sock_timeout = %lu s %lu us  \n", sock_timeout.tv_sec , sock_timeout.tv_usec  );	
	}
	
	ret = 0 ;
	while(1){
  
		if( ret = send(sock,  wbuf,  MAX_PACKET_SIZE , MSG_DONTWAIT ) , ret <= 0){
		//if( ret = write(sock,  wbuf,  MAX_PACKET_SIZE  ) , ret <= 0){
			/* 
			
			在non-block的情况下 当读写端发送和接收缓冲都满了之后 返回 -1 errno=11/Resource temporarily unavailable
			但是sleep之后再write的话 就一直阻塞在write中???
			 
			
			Proto Recv-Q Send-Q Local Address           Foreign Address         State


			tcp        0 2431457 127.0.0.1:34458         127.0.0.1:10000         ESTABLISHED
			tcp   950831      0 127.0.0.1:10000         127.0.0.1:34458         ESTABLISHED


			tcp        0 396094 127.0.0.1:34458         127.0.0.1:10000         FIN_WAIT1  	写端进程已经被杀 Ctrl+C 
			tcp   952923      0 127.0.0.1:10000         127.0.0.1:34458         ESTABLISHED 读端/服务端还在接收剩下的,写端还有有数据


			tcp        0      0 127.0.0.1:34458         127.0.0.1:10000         FIN_WAIT2  	写端进程已经被杀 Ctrl+C ,并且写端send_buf已经空了
			tcp   150425      0 127.0.0.1:10000         127.0.0.1:34458         CLOSE_WAIT	读端/服务端还在接收剩下的,写端已经没有数据在发送缓冲


			tcp        0      0 127.0.0.1:34458         127.0.0.1:10000         TIME_WAIT    写端端口等待 这段时间该端口不能被使用 
			
			*/
			
			if( errno == EAGAIN || errno == EINTR ){
				printf("i'm client,try again after 3s ret = %d , errno=%d/%s\n" , ret , errno, strerror(errno));
				sleep(3);
				printf("i'm client,sleep done go to try!\n");
			}else{
				printf("i'm client, write error ret = %d , errno=%d/%s\n" , ret , errno, strerror(errno));
				break;
			}
		}else{
			gettimeofday( &current, NULL ); 
			current_time = current.tv_sec * 1000uL * 1000uL +   current.tv_usec ;
			
			// 控制速度 
			delayus = transfer_each  + last_time - current_time ;
			if( delayus > 0 ){
				//printf("sleep %lu\n", delayus );
				usleep( delayus  );
			}
			
			gettimeofday( &current, NULL ); 
			last_time =  current.tv_sec * 1000uL * 1000uL +   current.tv_usec ;
			
			packet_num++;
			if( (packet_num & 0x3F) == 0 )  
				printf("packet_num = %lu , freq = %lu\n", packet_num ,packet_num * 1000uL * 1000uL /( last_time - start_time ) );
		}
	}
	printf("i'm client, socket disconnect\n");
	close(sock);
	return 0;
}
