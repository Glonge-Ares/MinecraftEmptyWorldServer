#include <iostream>         //Main
#include <sys/socket.h>     //Sockets
#include <string.h>         //Strings
#include <arpa/inet.h>      //Converter
#include <thread>           //Threads
#include <unistd.h>
#include <ctime>          //Time
#include <stdlib.h>
#include <stdio.h>
//#include <vector>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sqlite3.h>
#include <errno.h>

#include <set>
#include <map>

#define MAX_EVENTS 30000



#include "Nonblock.h"		//defines setnonblocking() function
#include "Packets.h"


class Client
{
	public:
		int fd;

		unsigned char incoming_buf[512];
		//int incoming_len = 0;
		//int incoming_offset = 0;

		unsigned char outgoing_buf[256];
		int outgoing_len = 0;
		int outgoing_offset = 0;
		int last_keep_alive = 0;
		bool is_first_packet = true;
};

//algorithm:
//send magic packet
//send uuid packet
//send world_info packet
//send player_pos packet
//send world_time




void on_epollin(int epollfd, int fd, std::map <int, Client*>* clientList, std::set <int>* readAgain)
{
	auto it = clientList->find(fd);	
	
	if(it == clientList->end())
		return;
	
	Client* client = it->second;
	
	errno = 0;
	int ret;
	do
	{
		ret = recv(client->fd, client->incoming_buf, sizeof(client->incoming_buf),0);
		std::cout << "ret " << ret << "\n";
		
	}while(errno != EAGAIN && ret > 0 && errno != EWOULDBLOCK);
	
	if(ret < 0)
		readAgain->insert(fd);
	
	if(errno == EAGAIN || errno == EWOULDBLOCK)
		readAgain->erase(fd);
	
	if(!client->is_first_packet)
		return;
	else
		client->is_first_packet = false;
	
	
	std::cout << "epollin\n";
	
	std::string uuid_packet;
	uuid_packet.push_back(0x29);
	uuid_packet.push_back(0x00);
	uuid_packet.push_back(0x02);
	uuid_packet += "$00000000-0000-0000-0000-000000000000";
	uuid_packet.push_back(0x01);
	uuid_packet.push_back('a');
	
	
	memcpy(client->outgoing_buf + client->outgoing_len, &magic, sizeof(magic));
	client->outgoing_len += sizeof(magic);
	
	memcpy(client->outgoing_buf + client->outgoing_len, uuid_packet.c_str(), uuid_packet.size());
	client->outgoing_len += uuid_packet.size();
	
	memcpy(client->outgoing_buf + client->outgoing_len, &world_info, sizeof(world_info));
	client->outgoing_len += sizeof(world_info);

	memcpy(client->outgoing_buf + client->outgoing_len, &player_pos, sizeof(player_pos));
	client->outgoing_len += sizeof(player_pos);
	
	memcpy(client->outgoing_buf + client->outgoing_len, &world_time, sizeof(world_time));
	client->outgoing_len += sizeof(world_time);
	
	memcpy(client->outgoing_buf + client->outgoing_len, &keep_alive, sizeof(keep_alive));
	client->outgoing_len += sizeof(keep_alive);
	
	client->last_keep_alive = time(0);
	
	epoll_event Eevent;
	Eevent.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLET;
	Eevent.data.fd = fd;
	epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &Eevent);
	
	
	return;
}

void on_epollout(int epollfd, int fd, std::map <int, Client*>* clientList)
{
	std::cout << "Epollout\n";
	auto it = clientList->find(fd);	
	
	if(it == clientList->end())
		return;
	
	Client* client = it->second;
	
	int ret = send(fd, &client->outgoing_buf + client->outgoing_offset, client->outgoing_len, 0);
	
	
	
	client->outgoing_offset += ret;
	client->outgoing_len -= ret;
	
	if(client->outgoing_len > 0)
		return;
	
	if(ret < 0)
	{
		close(fd);
		return;
	}
	
	if(client->outgoing_len == 0)
	{
		epoll_event Eevent;
		Eevent.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET;
		Eevent.data.fd = fd;
		epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &Eevent);
	}
	
	return;
}

void on_epollerr(int epollfd, int fd, std::map <int, Client*>* clientList)
{
	std::cout << "Epollerr\n";
	auto it = clientList->find(fd);	
	
	if(it == clientList->end())
		return;
	
	
	close(it->second->fd);
	clientList->erase(it);
	
	
	
	return;
}



void on_keepalive(int epollfd, Client* client)
{
	std::cout << "Keepalive\n";
	srand(time(0));
	
	//unsigned char mkeep_alive[] = {0x0a, 0x00, 0x1f, 0,0,0,rand()%128,rand()%128,rand()%128,rand()%128,rand()%128};
	client->outgoing_offset = 0;
	client->outgoing_len = 0;
	memcpy(client->outgoing_buf, &keep_alive, sizeof(keep_alive));
	client->outgoing_len += sizeof(keep_alive);

	memcpy(client->outgoing_buf + client->outgoing_len, &world_time, sizeof(world_time));
	client->outgoing_len += sizeof(world_time);

	client->last_keep_alive = time(0);
	
	
	
	std::string chat_message = "{\"extra\":[\"Fallback\"], \"text\":\"\"}";
	
	std::string chat_packet;
	chat_packet.push_back((uint8_t)chat_message.size()+4);
	chat_packet.push_back(0x00);
	chat_packet.push_back(0x0F);
	chat_packet.push_back((uint8_t)chat_message.size());
	chat_packet += chat_message;
	
	//memcpy(client->outgoing_buf + client->outgoing_len, chat_packet.c_str(), chat_packet.size());
	//client->outgoing_len += chat_packet.size();
	
	epoll_event Eevent;
	Eevent.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLET;
	Eevent.data.fd = client->fd;
	epoll_ctl(epollfd, EPOLL_CTL_MOD, client->fd, &Eevent);
	
	return;
}




int main()
{

	int sock = socket(AF_INET, SOCK_STREAM, 0);    
	int port = 25589;
	
	int listen_res, connection;
	int epollfd = epoll_create(1);
	
	std::map <int, Client*> clientList;
	std::set <int> readAgain;
	
	
	
	
	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);

	struct sockaddr_in from;
	socklen_t len = sizeof(from);
	
	
	
	struct epoll_event ev, events[MAX_EVENTS];
	
	
	
	if ((bind(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) < 0)
		printf("\033[01;31m[ERROR]\033[0m Binding error\n");
	else
		printf("\033[01;32m[OK]\033[0m Binding\n");
	
	
	setnonblocking(sock);
    	ev.events = EPOLLIN;
    	ev.data.fd = sock;
	
	
	if(epoll_ctl(epollfd, EPOLL_CTL_ADD, sock, &ev) == -1)
		printf("\033[01;31m[ERROR]\033[0m epoll_ctl(): server's socket\n");
	
	int nfds = 0;
	
	while(true)
	{
		nfds = epoll_wait(epollfd, events, MAX_EVENTS, 500);

        	if (nfds == -1) 
			printf("\033[01;31m[ERROR]\033[0m nfds error\n");
		
		


	//std::cout << time(0) << "\n";


		for (register size_t k = 0; k < nfds; ++k)
		{
			if(events[k].data.fd == sock)							//если есть запрос на подключение, принимаем.
			{            
				listen_res = listen(events[k].data.fd, 1);
                
			 	if(listen_res = 1)
                		{
					connection = accept(sock, (struct sockaddr*)&from, &len); 
					
					if(connection == -1)
						continue;
					
					if (setnonblocking(connection) == -1)
						printf("\033[01;31m[ERROR]\033[0m setnonblocking()\n");

					
					ev.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET;
					//ev.events = EPOLLRDHUP | EPOLLERR | EPOLLET | EPOLLOUT;
					ev.data.fd = connection;
					
					
					if(epoll_ctl(epollfd, EPOLL_CTL_ADD, connection, &ev) == -1)
						printf("\033[01;31m[ERROR]\033[0m epoll_ctl(connection) returned an error\n");
					else
						printf("\033[01;33m[INFO]\033[0m New Client's IP: %s\n", inet_ntoa(from.sin_addr));
					
					Client* client = new Client();
					
					client->fd = connection;
					
					clientList.insert(std::pair<int, Client*>((int)connection, (Client*)client));
					
					std::cout << "inserted\n";
				}
				
			}
			else
			{
				
				
				
				
				
				if (events[k].events & (EPOLLRDHUP | EPOLLHUP))
				{
					//printf("\033[01;33m[INFO]\033[0m Disconnect event\n");
					
					//ThreadPool.add(task);
					on_epollerr(epollfd, events[k].data.fd, &clientList);
					readAgain.erase(events[k].data.fd);
					close(events[k].data.fd);
					epoll_ctl(epollfd, EPOLL_CTL_DEL, events[k].data.fd, &ev);
				}
				
				if (events[k].events & EPOLLIN)
				{
					on_epollin(epollfd, events[k].data.fd, &clientList, &readAgain);
		       		}

				if (events[k].events & EPOLLOUT)
				{
					on_epollout(epollfd, events[k].data.fd, &clientList);
					//printf("\033[01;33m[INFO]\033[0m EPOLLOUT event happened\n");
					//ThreadPool.add(task);
		       		}


       			}
					
		}
		
		for(auto mClient : clientList)
		{
			Client* client = mClient.second;
			int time_passed = time(0) - client->last_keep_alive;
			
			if(time(0) - client->last_keep_alive > 20 && !client->is_first_packet)//Send keep alive every 20 seconds
				on_keepalive(epollfd, client);
		}
		
		for(auto ReadEvent : readAgain)
		{
			//pool_task task;
			//task.event = EPOLLIN;
			//task.fd = ReadEvent;
			printf("\033[01;33m[INFO]\033[0m ReadAgain event happened\n");
			on_epollin(epollfd, ReadEvent, &clientList, &readAgain);
			//ThreadPool.add(task);
			
		}
		
		
		
		
		
		
	}
	
	
	
	
	
	
}
