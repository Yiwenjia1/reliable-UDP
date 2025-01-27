#include <ws2tcpip.h> 
#include <iostream>
#include <winsock2.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <fstream>

#pragma comment (lib, "ws2_32.lib")

using namespace std;

const int ServerPORT = 40000;
const int RouterPORT = 30000;

int seq = 0;
const int windowsize = 1;

#define MAX_WAIT_TIME  1000 //超时时限（ms）
#define MAX_SEND_TIMES  6 //最大重传次数
#define Max_File_Size 15000000 //最大文件大小
#define Max_Msg_Size 10000 //最大数据包大小

// 定义不同的控制标志，包括SYN, ACK, FIN和文件名
const unsigned short SYN = 0x1;//0001
const unsigned short ACK = 0x2;//0010
const unsigned short FIN = 0x4;//0100
const unsigned short FileName = 0x8;//1000

#pragma pack(1)
struct Packet
{
	//头部（一共20字节）
	unsigned short SrcPort, DestPort;//源端口号、目的端口号
	unsigned int SeqNum;// 序列号
	unsigned int AckNum;//确认号
	unsigned int length;//数据段长度
	unsigned short flag;//标志位
	unsigned short checkNum;//校验和
	BYTE data[Max_Msg_Size];// 数据段，最大10000字节

	Packet();
	void calculateChecksum();
	bool check();
};


#pragma pack()
//构造函数，初始化全为0
Packet::Packet()
{
	SeqNum = 0;
	AckNum = 0;
	length = 0;
	flag = 0;
	memset(&data, 0, sizeof(data));
}

bool Packet::check()
{

	unsigned int sum = 0;
	unsigned short* ptr = (unsigned short*)this;

	for (int i = 0; i < sizeof(*this) / 2; i++)
	{
		sum += *ptr++;
		if (sum & 0xFFFF0000)
		{
			sum &= 0xFFFF;
			sum++;
		}
	}
	if ((sum & 0xFFFF) == 0xFFFF)
	{
		return true;
	}
	return false;

}

void Packet::calculateChecksum()
{
	this->checkNum = 0;
	int sum = 0;
	unsigned short* ptr = (unsigned short*)this;


	for (int i = 0; i < sizeof(*this) / 2; i++)
	{
		sum += *ptr++;
		if (sum & 0xFFFF0000)
		{
			sum &= 0xFFFF;
			sum++;
		}
	}
	this->checkNum = ~(sum & 0xFFFF);

}


//实现server的三次握手
bool initiateConnection(SOCKET serverSocket, SOCKADDR_IN clientAddr)
{
	int AddrLen = sizeof(clientAddr);
	Packet buffer1, buffer2, buffer3;
	int resendtimes = 0;

	while (true)
	{
		//接收第一次握手的消息
		int recvByte = recvfrom(serverSocket, (char*)&buffer1, sizeof(buffer1), 0, (sockaddr*)&clientAddr, &AddrLen);

		if (recvByte > 0)
		{
			//成功收到消息，检查SYN、检验和
			if (!(buffer1.flag & SYN) || !buffer1.check())
			{
				cout << "server接收第一次握手成功，检查失败" << endl;
				return false;
			}
			cout << "server接收第一次握手成功" << endl;

			//发送第二次握手的消息（SYN、ACK有效，seq=y（相对seq也是0）ack=x+1（也就是1））
			buffer2.SrcPort = ServerPORT;
			buffer2.DestPort = RouterPORT;
			buffer2.SeqNum = seq;
			buffer2.AckNum = buffer1.SeqNum + 1;
			buffer2.flag |= SYN;
			buffer2.flag |= ACK;
			buffer2.calculateChecksum();

			int sendByte = sendto(serverSocket, (char*)&buffer2, sizeof(buffer2), 0, (sockaddr*)&clientAddr, AddrLen);
			clock_t start_time = clock();
			if (sendByte == 0)
			{
				cout << "server发送第二次握手失败" << endl;
				return false;
			}
			cout << "server发送第二次握手："
				<< "SrcPort: " << buffer2.SrcPort << ", "
				<< "DestPort: " << buffer2.DestPort << ", "
				<< "SeqNum: " << buffer2.SeqNum << ", "
				<< "AckNum: " << (buffer2.flag & ACK ? to_string(buffer2.AckNum) : "N/A") << ", "
				<< "Flag: " << "[SYN: " << (buffer2.flag & SYN ? "1" : "0")
				<< "] [ACK: " << (buffer2.flag & ACK ? "1" : "0") << "], "
				<< "CheckNum: " << buffer2.checkNum << endl;


			//接收第三次握手的消息
			while (true)
			{
				int recvByte = recvfrom(serverSocket, (char*)&buffer3, sizeof(buffer3), 0, (sockaddr*)&clientAddr, &AddrLen);
				if (recvByte > 0)
				{
					//成功收到消息，检查ACK、校验和、ack
					if ((buffer3.flag & ACK) && buffer3.check() && (buffer3.AckNum == buffer2.SeqNum + 1))
					{
						seq++;
						cout << "server接收第三次握手成功" << endl;
						cout << "server连接成功！" << endl;
						return true;
					}
					else
					{
						cout << "server接收第三次握手成功，检查失败" << endl;
						return false;
					}
				}

				//server发送第二次握手超时，重新发送并重新计时
				if (clock() - start_time > MAX_WAIT_TIME)
				{
					if (++resendtimes > MAX_SEND_TIMES) {
						cout << "server发送第二次握手超时重传已到最大次数，发送失败" << endl;
						return false;
					}
					cout << "server发送第二次握手，第" << resendtimes << "次超时，正在重传......" << endl;
					int sendByte = sendto(serverSocket, (char*)&buffer2, sizeof(buffer2), 0, (sockaddr*)&clientAddr, AddrLen);
					if (sendByte > 0) {
						cout << "server发送第二次握手重传成功" << endl;
					}
					else {
						cout << "server第二次握手重传失败" << endl;
					}
					start_time = clock();  // 更新超时计时
				}
			}
		}
	}
	return false;
}

//实现单个报文接收
//接收到数据后返回一个 ACK 报文 作为确认
bool recvPacket(Packet& recvMsg, SOCKET serverSocket, SOCKADDR_IN clientAddr)
{
	int AddrLen = sizeof(clientAddr);
	while (1)
	{
		int recvByte = recvfrom(serverSocket, (char*)&recvMsg, sizeof(recvMsg), 0, (sockaddr*)&clientAddr, &AddrLen);
		if (recvByte > 0)
		{
			//检查是否收到的数据包有效并且序列号符合预期，因为接收窗口大小为1，所以expectedSeqNum就是前一次的+1
			if (recvMsg.check() && (recvMsg.SeqNum == seq + 1))
			{
				//如果是期望的数据包，处理并发送ACK
				Packet replyMessage;
				replyMessage.SrcPort = ServerPORT;
				replyMessage.DestPort = RouterPORT;
				replyMessage.flag |= ACK;
				replyMessage.SeqNum = seq++;
				replyMessage.AckNum = recvMsg.SeqNum;
				replyMessage.calculateChecksum();
				sendto(serverSocket, (char*)&replyMessage, sizeof(replyMessage), 0, (sockaddr*)&clientAddr, sizeof(SOCKADDR_IN));//发送
				cout << "server收到：SeqNum = " << recvMsg.SeqNum << ", CheckNum = " << recvMsg.checkNum << ", Data Length = " << recvMsg.length << " bytes的数据报文" << endl;
				cout << "server发送：SeqNum = " << replyMessage.SeqNum << "，AckNum = " << replyMessage.AckNum << " 的ACK报文" << endl;
				return true;
			}

			//如果是重复的数据包（序列号不符合预期），丢弃该数据包，重新发送ACK，ack=seq,无序/过期
			else if (recvMsg.check() && (recvMsg.SeqNum > seq + 1))
			{
				Packet replyMessage;
				replyMessage.SrcPort = ServerPORT;
				replyMessage.DestPort = RouterPORT;
				replyMessage.flag |= ACK;
				replyMessage.SeqNum = seq;
				replyMessage.AckNum = seq;
				replyMessage.calculateChecksum();
				sendto(serverSocket, (char*)&replyMessage, sizeof(replyMessage), 0, (sockaddr*)&clientAddr, sizeof(SOCKADDR_IN));
				cout << "[WARN] server收到 SeqNum = " << recvMsg.SeqNum << "的数据报文，并重新发送 AckNum = " << replyMessage.AckNum << " 的ACK报文" << endl;
			}
		}
		else if (recvByte == 0)
		{
			return false;
		}
	}
	return true;
}

//实现文件接收
//接收文件名、文件大小以及实际的文件数据，并将这些数据存储到一个文件中
void recvFile(SOCKET serverSocket, SOCKADDR_IN clientAddr)
{
	int AddrLen = sizeof(clientAddr);
	//接收文件名和文件大小
	Packet nameMessage;
	unsigned int fileSize;
	char fileName[50] = { 0 };
	while (1)
	{
		int recvByte = recvfrom(serverSocket, (char*)&nameMessage, sizeof(nameMessage), 0, (sockaddr*)&clientAddr, &AddrLen);
		if (recvByte > 0)
		{
			//如果成功收到装载文件名和文件大小的消息，则进行一下输出并回复对应的ACK报文
			if (nameMessage.check() && (nameMessage.SeqNum == seq + 1) && (nameMessage.flag & FileName))
			{
				//解析出文件的名称和大小
				fileSize = nameMessage.length;
				for (int i = 0; nameMessage.data[i]; i++)
					fileName[i] = nameMessage.data[i];
				cout << "\n接收文件名：" << fileName << "，文件大小：" << fileSize << "字节" << endl << endl;
				//发送 ACK 确认报文
				Packet replyMessage;
				replyMessage.SrcPort = ServerPORT;
				replyMessage.DestPort = RouterPORT;
				replyMessage.flag |= ACK;
				replyMessage.SeqNum = seq++;
				replyMessage.AckNum = nameMessage.SeqNum;
				replyMessage.calculateChecksum();
				sendto(serverSocket, (char*)&replyMessage, sizeof(replyMessage), 0, (sockaddr*)&clientAddr, sizeof(SOCKADDR_IN));
				cout << "server收到：SeqNum = " << nameMessage.SeqNum << ", CheckNum = " << nameMessage.checkNum << "Flag: " << "[FileName:" << (nameMessage.flag & FileName ? "1" : "0") << "], " << " Data Length = " << nameMessage.length << " bytes的数据报文" << endl;
				cout << "server发送：SeqNum = " << replyMessage.SeqNum << ", AckNum = " << replyMessage.AckNum << " 的ACK报文" << endl;
				cout << "文件名和文件大小信息接收成功，开始接收文件数据" << endl << endl;
				break;
			}

			//如果seq值不正确，则丢弃报文，返回上一个的ACK报文（ack=seq）
			else if (nameMessage.check() && (nameMessage.SeqNum != seq + 1) && (nameMessage.flag & FileName))
			{
				Packet replyMessage;
				replyMessage.SrcPort = ServerPORT;
				replyMessage.DestPort = RouterPORT;
				replyMessage.flag |= ACK;
				replyMessage.SeqNum = seq++;
				replyMessage.AckNum = nameMessage.SeqNum;
				replyMessage.calculateChecksum();
				sendto(serverSocket, (char*)&replyMessage, sizeof(replyMessage), 0, (sockaddr*)&clientAddr, sizeof(SOCKADDR_IN));
				cout << "[WARN] server收到 SeqNum = " << nameMessage.SeqNum << "的数据报文，并重新发送 AckNum = " << replyMessage.AckNum << " 的ACK报文" << endl;
			}
			cout << endl;
		}
	}

	//按照数据的大小将文件分为多个数据段
	int totalPackets = fileSize / Max_Msg_Size;//可以装满的报文数量
	int remainingBytes = fileSize % Max_Msg_Size;//剩余数据量大小
	BYTE* fileBuffer = new BYTE[fileSize];

	for (int i = 0; i < totalPackets; i++)
	{
		Packet dataMsg;
		if (recvPacket(dataMsg, serverSocket, clientAddr))
		{
			cout << "第" << i + 1 << "个数据报文接收成功" << endl << endl;
		}
		else
		{
			cout << "第" << i + 1 << "个数据报文接收失败" << endl << endl;
			return;
		}
		//读取数据
		for (int j = 0; j < Max_Msg_Size; j++)
		{
			fileBuffer[i * Max_Msg_Size + j] = dataMsg.data[j];
		}
	}

	if (remainingBytes > 0)
	{
		Packet dataMsg;
		if (recvPacket(dataMsg, serverSocket, clientAddr))
		{
			cout << "第" << totalPackets + 1 << "个数据报文接收成功" << endl << endl;
		}
		else
		{
			cout << "第" << totalPackets + 1 << "个数据报文接收失败" << endl << endl;
			return;
		}
		//读取数据
		for (int j = 0; j < remainingBytes; j++)
		{
			fileBuffer[totalPackets * Max_Msg_Size + j] = dataMsg.data[j];
		}
	}

	//写入文件
	cout << "\n文件传输成功，开始写入文件" << endl;
	FILE* outputfile;
	errno_t err = fopen_s(&outputfile, fileName, "wb");
	if (fileBuffer != 0)
	{
		fwrite(fileBuffer, fileSize, 1, outputfile);
		fclose(outputfile);
	}
	cout << "文件写入成功" << endl << endl;
	delete[] fileBuffer;//释放内存
}

//实现server的四次挥手
bool terminateConnection(SOCKET serverSocket, SOCKADDR_IN clientAddr)
{
	int AddrLen = sizeof(clientAddr);
	Packet buffer1, buffer2, buffer3, buffer4;
	while (1)
	{
		//客户端发送FIN报文（请求关闭连接）
		//接收第一次挥手的消息
		int recvByte = recvfrom(serverSocket, (char*)&buffer1, sizeof(buffer1), 0, (sockaddr*)&clientAddr, &AddrLen);
		if (recvByte == 0)
		{
			cout << "第一次挥手接收失败，退出" << endl;
			return false;
		}

		else if (recvByte > 0)
		{
			//检查FIN、ACK、检验和
			if (!(buffer1.flag && FIN) || !(buffer1.flag && ACK) || !buffer1.check())
			{
				cout << "第一次挥手接收成功，检查失败！" << endl;
				return false;
			}
			cout << "server接收第一次挥手成功" << endl;

			//服务端响应ACK报文（确认收到关闭请求）
			//发送第二次挥手的消息（ACK有效，ack是第一次挥手消息的seq+1,seq值自动递增）
			buffer2.SrcPort = ServerPORT;
			buffer2.DestPort = RouterPORT;
			buffer2.SeqNum = seq++;
			buffer2.AckNum = buffer1.SeqNum + 1;
			buffer2.flag |= ACK;
			buffer2.calculateChecksum();

			int sendByte = sendto(serverSocket, (char*)&buffer2, sizeof(buffer2), 0, (sockaddr*)&clientAddr, AddrLen);
			clock_t t2 = clock();
			if (sendByte == 0)
			{
				cout << "server发送第二次挥手失败" << endl;
				return false;
			}
			cout << "server发送第二次挥手："
				<< "SrcPort: " << buffer2.SrcPort << ", "
				<< "DestPort: " << buffer2.DestPort << ", "
				<< "SeqNum: " << buffer2.SeqNum << ", "
				<< "AckNum: " << (buffer2.flag & ACK ? to_string(buffer2.AckNum) : "N/A") << ", "
				<< "Flag: "
				<< "[ACK:" << (buffer2.flag & ACK ? "1" : "0") << "], "
				<< "CheckNum: " << buffer2.checkNum << endl;
			break;
		}
	}
	//服务端发送FIN报文（请求关闭连接）
	//server发送第三次挥手的消息（FIN、ACK有效，seq自动递增）
	buffer3.SrcPort = ServerPORT;
	buffer3.DestPort = RouterPORT;
	buffer3.flag |= FIN;
	buffer3.flag |= ACK;
	buffer3.SeqNum = seq++;
	buffer3.AckNum = buffer1.SeqNum + 1;
	buffer3.calculateChecksum();

	int sendByte = sendto(serverSocket, (char*)&buffer3, sizeof(buffer3), 0, (sockaddr*)&clientAddr, AddrLen);
	clock_t t3 = clock();
	if (sendByte == 0)
	{
		cout << "server发送第三次挥手失败" << endl;
		return false;
	}
	cout << "server发送第三次挥手："
		<< "SrcPort: " << buffer3.SrcPort << ", "
		<< "DestPort: " << buffer3.DestPort << ", "
		<< "SeqNum: " << buffer3.SeqNum << ", "
		<< "AckNum: " << (buffer3.flag & ACK ? to_string(buffer3.AckNum) : "N/A") << ", "
		<< "Flag: "
		<< "[ACK:" << (buffer3.flag & ACK ? "1" : "0")
		<< "] [FIN: " << (buffer3.flag & FIN ? "1" : "0") << "], "
		<< "CheckNum : " << buffer3.checkNum << endl;
	int resendtimes = 0;

	//客户端响应ACK报文（确认收到服务端的关闭请求）
	//接收第四次挥手的消息
	while (1)
	{
		int recvByte = recvfrom(serverSocket, (char*)&buffer4, sizeof(buffer4), 0, (sockaddr*)&clientAddr, &AddrLen);
		if (recvByte == 0)
		{
			cout << "server接收第四次挥手失败" << endl;
			return false;
		}
		else if (recvByte > 0)
		{
			//成功收到消息，检查校验和、ACK、ack
			if ((buffer4.flag && ACK) && buffer4.check() && (buffer4.AckNum == buffer3.SeqNum + 1))
			{
				cout << "server接收第四次挥手成功" << endl;
				break;
			}
			else
			{
				cout << "server接收第四次挥手成功，检查失败" << endl;
				return false;
			}
		}
		//第三次挥手超时重传
		if (clock() - t3 > MAX_WAIT_TIME)
		{
			cout << "server发送第三次挥手，第" << ++resendtimes << "次超时，正在重传......" << endl;
			int sendByte = sendto(serverSocket, (char*)&buffer3, sizeof(buffer3), 0, (sockaddr*)&clientAddr, AddrLen);
			t3 = clock();  // 更新超时计时
			if (sendByte > 0) {
				cout << "server发送第三次挥手重传成功" << endl;
			}
			else {
				cout << "server第三次挥手重传失败" << endl;
			}
			if (++resendtimes > MAX_SEND_TIMES) {
				cout << "server第三次挥手超时重传已到最大次数，发送失败" << endl;
				return false;
			}
		}
	}
	cout << "\nserver关闭连接成功！" << endl;
	return true;
}

int main()
{
	//初始化Winsock服务
	WSADATA wsaDataStruct;
	int result = WSAStartup(MAKEWORD(2, 2), &wsaDataStruct);
	if (result != 0) {
		cout << "Winsock服务初始化失败，错误代码：" << result << endl;
		return -1;
	}
	cout << "Winsock服务初始化成功" << endl;

	//创建socket，是UDP套接字，UDP是一种无连接的协议
	SOCKET serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (serverSocket == INVALID_SOCKET)
	{
		cerr << "套接字创建失败，错误代码：" << WSAGetLastError() << endl;
		return -1;
	}

	// 设置套接字为非阻塞模式
	unsigned long mode = 1;
	if (ioctlsocket(serverSocket, FIONBIO, &mode) != NO_ERROR)
	{
		cerr << "无法将套接字设置为非阻塞模式，错误代码：" << WSAGetLastError() << endl;
		closesocket(serverSocket);
		return -1;
	}
	cout << "套接字创建成功, 并设为非阻塞模式" << endl;

	//初始化服务器地址
	SOCKADDR_IN serverAddr;
	serverAddr.sin_family = AF_INET;
	int result3 = inet_pton(AF_INET, "127.0.0.1", &(serverAddr.sin_addr));
	serverAddr.sin_port = htons(ServerPORT);

	//bind
	int tem = bind(serverSocket, (LPSOCKADDR)&serverAddr, sizeof(serverAddr));
	if (tem == SOCKET_ERROR)
	{
		cout << "套接字绑定失败，错误代码：" << WSAGetLastError() << endl;
		return -1;
	}
	cout << "套接字绑定成功，服务器准备接收数据" << endl << endl;

	//初始化路由器地址
	SOCKADDR_IN clientAddr;
	clientAddr.sin_family = AF_INET;
	int result4 = inet_pton(AF_INET, "127.0.0.1", &(clientAddr.sin_addr));
	clientAddr.sin_port = htons(RouterPORT);

	//建立连接
	bool isConn = initiateConnection(serverSocket, clientAddr);
	if (isConn == 0) {
		cerr << "服务器与客户端的连接建立失败，退出程序" << endl;
		return -1;
	}

	//接收文件
	cout << endl;
	cout << "准备接收文件..." << endl;
	cout << "接收窗口大小 = " << windowsize << endl;
	recvFile(serverSocket, clientAddr);

	//关闭连接
	cout << "服务器准备断开连接..." << endl;
	bool breaked = terminateConnection(serverSocket, clientAddr);
	if (!breaked) {
		cerr << "服务器断开连接失败，退出程序" << endl;
		return -1;
	}

	closesocket(serverSocket);
	WSACleanup();
	system("pause");
	return 0;

}