#include <ws2tcpip.h> 
#include <iostream>
#include <winsock2.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <iomanip>
#include <fstream>

#pragma comment (lib, "ws2_32.lib")

using namespace std;

const int RouterPORT = 30000;
const int ClientPORT = 20000;

int seq = 0;

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

	//下面是实现的方法
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
	unsigned short* ptr = (unsigned short*)this;// 每次处理两个字节（16位）
	// 遍历 Packet 对象的所有 16 位数据（Packet 对象大小除以 2，每个循环处理两个字节）
	for (int i = 0; i < sizeof(*this) / 2; i++)
	{
		sum += *ptr++;
		// 如果 sum 的高16位有进位，则将高16位丢弃并加 1
		if (sum & 0xFFFF0000)
		{
			sum &= 0xFFFF;
			sum++;
		}
	}
	// 如果 sum 的低16位为 0xFFFF，则表示校验和正确
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

//实现client的三次握手
bool initiateConnection(SOCKET clientSocket, SOCKADDR_IN serverAddr)
{
	int AddrLen = sizeof(serverAddr);
	Packet buffer1, buffer2, buffer3;

	//发送第一次握手的消息（SYN有效)
	buffer1.SrcPort = ClientPORT;
	buffer1.DestPort = RouterPORT;
	buffer1.flag |= SYN;
	buffer1.SeqNum = seq;
	buffer1.calculateChecksum();


	int sendByte = sendto(clientSocket, (char*)&buffer1, sizeof(buffer1), 0, (sockaddr*)&serverAddr, AddrLen);
	clock_t buffer1start = clock();
	if (sendByte > 0)
	{
		cout << "client发送第一次握手："
			<< "SrcPort: " << buffer1.SrcPort << ", "
			<< "DestPort: " << buffer1.DestPort << ", "
			<< "SeqNum: " << buffer1.SeqNum << ", "
			<< "Flag: " << "[SYN: " << (buffer1.flag & SYN ? "1" : "0") << "], "
			<< "CheckNum: " << buffer1.checkNum << endl;
	}

	int resendCount = 0;
	//接收第二次握手的消息
	while (true)
	{
		int recvByte = recvfrom(clientSocket, (char*)&buffer2, sizeof(buffer2), 0, (sockaddr*)&serverAddr, &AddrLen);
		if (recvByte > 0)
		{
			//成功收到消息，检查校验和、ACK、SYN、ack
			if ((buffer2.flag & ACK) && (buffer2.flag & SYN) && buffer2.check() && (buffer2.AckNum == buffer1.SeqNum + 1))
			{
				cout << "client接收第二次握手成功" << endl;
				break;
			}
			else {
				cout << "接收第二次握手消息检查失败" << endl;
			}
		}

		//如果client发送第一次握手超时，重新发送并重新计时
		if (clock() - buffer1start > MAX_WAIT_TIME)
		{
			if (++resendCount > MAX_SEND_TIMES) {
				cout << "client发送第一次握手超时重传已到最大次数，发送失败" << endl;
				return false;
			}
			cout << "client发送第一次握手，第" << resendCount << "次超时，正在重传......" << endl;
			int sendByte = sendto(clientSocket, (char*)&buffer1, sizeof(buffer1), 0, (sockaddr*)&serverAddr, AddrLen);
			buffer1start = clock();
			if (sendByte <= 0) {
				cout << "client第一次握手重传失败" << endl;
				return false;
			}
		}
	}

	//发送第三次握手的消息（ACK有效）
	buffer3.SrcPort = ClientPORT;
	buffer3.DestPort = RouterPORT;
	buffer3.flag |= ACK;
	buffer3.SeqNum = ++seq;
	buffer3.AckNum = buffer2.SeqNum + 1;
	buffer3.calculateChecksum();

	sendByte = sendto(clientSocket, (char*)&buffer3, sizeof(buffer3), 0, (sockaddr*)&serverAddr, AddrLen);
	
	if (sendByte == 0)
	{
		cout << "client发送第三次握手失败" << endl;
		return false;
	}

	cout << "client发送第三次握手:"
		<< "SrcPort: " << buffer3.SrcPort << ", "
		<< "DestPort: " << buffer3.DestPort << ", "
		<< "SeqNum: " << buffer3.SeqNum << ", "
		<< "AckNum: " << (buffer3.flag & ACK ? to_string(buffer3.AckNum) : "0") << ", "
		<< "Flag: " 
		<<"[ACK:" << (buffer3.flag & ACK ? "1" : "0") << "], "
		<< "CheckNum: " << buffer3.checkNum << endl;
	cout << "client连接成功！" << endl;
	cout << endl;
	return true;
}

//实现单个报文发送
bool sendPacket(Packet& sendMsg, SOCKET clientSocket, SOCKADDR_IN serverAddr, bool FileFlag = false)
{
	sendto(clientSocket, (char*)&sendMsg, sizeof(sendMsg), 0, (sockaddr*)&serverAddr, sizeof(SOCKADDR_IN));
	if (FileFlag)
	{
		cout << "client发送："
			<< "SeqNum: " << sendMsg.SeqNum << ", "
			<< "CheckNum: " << sendMsg.checkNum << ", "
			<< "Flag: "
			<<"[FileName:" << (sendMsg.flag & FileName? "1":"0")<< "], "
			<< "Data Length: " << sendMsg.length << " bytes" << endl;
	}
	else
	{
		cout << "client发送："
			<< "SeqNum: " << sendMsg.SeqNum << ", "
			<< "CheckNum: " << sendMsg.checkNum << ", "
			<< "Data Length: " << sendMsg.length << " bytes" << endl;
	}
	clock_t msgStart = clock();
	Packet recvMsg;
	int AddrLen = sizeof(serverAddr);
	int resendCount = 0;

	while (1)
	{
		int recvByte = recvfrom(clientSocket, (char*)&recvMsg, sizeof(recvMsg), 0, (sockaddr*)&serverAddr, &AddrLen);
		if (recvByte > 0)
		{
			// 如果收到正确的ACK包，验证校验和和ACK号
			if ((recvMsg.flag & ACK) && (recvMsg.AckNum == sendMsg.SeqNum))
			{
				cout << "client收到：AckNum = " << recvMsg.AckNum << " 的ACK";
				return true;
			}
		}
		// 如果超时，则重新发送
		if (clock() - msgStart > MAX_WAIT_TIME)
		{
			cout << "[WARN]" << "seq = " << sendMsg.SeqNum << "的报文，第" << ++resendCount << "次超时，正在重传......" << endl;
			int sendByte = sendto(clientSocket, (char*)&sendMsg, sizeof(sendMsg), 0, (sockaddr*)&serverAddr, sizeof(SOCKADDR_IN));
			msgStart = clock();
			if (sendByte > 0) {
				cout << "报文重传成功" << endl;
				break;
			}
			else {
				cout << "报文重传失败" << endl;
			}
		}
		if (resendCount == MAX_SEND_TIMES)
		{
			cout << "超时重传已到最大次数，发送失败" << endl;
			return false;
		}
	}
	return true;
}

//实现文件传输
void sendFile(string filename, SOCKADDR_IN serverAddr, SOCKET clientSocket)
{

	clock_t starttime = clock();
	string realname = filename;
	filename ="D:\\3.1\\计算机网络\\Lab3\\lab3-1\\测试文件\\"+ filename;
	ifstream fin(filename.c_str(), ifstream::binary);
	if (!fin) {
		printf("无法打开文件！\n");
		return;
	}

	// 读取文件内容到缓冲区
	BYTE* fileBuffer = new BYTE[Max_File_Size];
	unsigned int fileSize = 0;
	BYTE byte = fin.get();
	while (fin) {
		fileBuffer[fileSize++] = byte;
		byte = fin.get();
	}
	fin.close();

	// 发送文件名和文件大小的报文，设置FileName标志位
	Packet nameMessage;
	nameMessage.SrcPort = ClientPORT;
	nameMessage.DestPort = RouterPORT;
	nameMessage.length = fileSize;
	nameMessage.flag |= FileName;
	nameMessage.SeqNum = ++seq;

	// 复制文件名到数据区域，并添加结束符
	for (int i = 0; i < realname.size(); i++)
		nameMessage.data[i] = realname[i];
	nameMessage.data[realname.size()] = '\0';
	nameMessage.calculateChecksum();
	if (!sendPacket(nameMessage, clientSocket, serverAddr,true))
	{
		cout << "发送文件名和大小的报文失败" << endl;
		return;
	}
	cout << endl;
	cout << "文件名和大小报文发送成功" << endl << endl;

	int totalPackets = fileSize / Max_Msg_Size;// 可填满的数据包数量
	int remainingBytes = fileSize % Max_Msg_Size;// 剩余数据部分
	
	for (int i = 0; i < totalPackets; i++)
	{
		Packet dataMsg;
		dataMsg.SrcPort = ClientPORT;
		dataMsg.DestPort = RouterPORT;
		dataMsg.SeqNum = ++seq;
		dataMsg.length = Max_Msg_Size;
		for (int j = 0; j < Max_Msg_Size; j++)
		{
			dataMsg.data[j] = fileBuffer[i * Max_Msg_Size + j];
		}
		dataMsg.calculateChecksum();
		if (!sendPacket(dataMsg, clientSocket, serverAddr))
		{
			cout << "第" << i + 1 << "个数据报文发送失败" << endl;
			return;
		}
		cout << endl;
		cout << "第" << i + 1 << "个数据报文发送成功" << endl << endl;
	}

	if (remainingBytes > 0)
	{
		Packet dataMsg;
		dataMsg.SrcPort = ClientPORT;
		dataMsg.DestPort = RouterPORT;
		dataMsg.SeqNum = ++seq;
		dataMsg.length = remainingBytes;
		for (int j = 0; j < remainingBytes; j++)
		{
			dataMsg.data[j] = fileBuffer[totalPackets * Max_Msg_Size + j];
		}
		dataMsg.calculateChecksum();
		if (!sendPacket(dataMsg, clientSocket, serverAddr))
		{
			cout << "第" << totalPackets + 1 << "个数据报文发送失败" << endl;
			return;
		}
		cout << endl;
		cout << "第" << totalPackets + 1 << "个数据报文发送成功" << endl << endl;
	}

	//计算传输时间和吞吐率
	clock_t endtime = clock();
	double totalTime = (double)(endtime - starttime) / CLOCKS_PER_SEC;
	double throughput = ((float)fileSize * 8) / totalTime;

	cout << "\n总传输时间为: " << totalTime << "s" << endl;
	cout << "平均吞吐率: "<<std::fixed << std::setprecision(2) << throughput << " bit/s" << endl;
	delete[] fileBuffer;//释放内存
	return;
}

//实现client的四次挥手
bool  terminateConnection(SOCKET clientSocket, SOCKADDR_IN serverAddr)
{
	int AddrLen = sizeof(serverAddr);
	Packet buffer1,buffer2,buffer3,buffer4;

	//发送第一次挥手（FIN、ACK有效）
	buffer1.SrcPort = ClientPORT;
	buffer1.DestPort = RouterPORT;
	buffer1.flag |= FIN;
	buffer1.flag |= ACK;
	buffer1.SeqNum = ++seq;
	buffer1.calculateChecksum();
	int sendByte = sendto(clientSocket, (char*)&buffer1, sizeof(buffer1), 0, (sockaddr*)&serverAddr, AddrLen);
	clock_t buffer1start = clock();
	if (sendByte == 0)
	{
		cout << "client发送第一次挥手失败，退出" << endl;
		return false;
	}
	cout << "client发送第一次挥手："
		<< "SrcPort: " << buffer1.SrcPort << ", "
		<< "DestPort: " << buffer1.DestPort << ", "
		<< "SeqNum: " << buffer1.SeqNum << ", "
		<< "Flag: "
		<<"[ACK:" << (buffer1.flag & ACK ? "1" : "0")
		<< "] [FIN: " << (buffer1.flag & FIN ? "1" : "0") << "], "
		<< "CheckNum : " << buffer1.checkNum << endl;
	int resendCount = 0;
	//接收第二次挥手的消息
	while (1)
	{
		int recvByte = recvfrom(clientSocket, (char*)&buffer2, sizeof(buffer2), 0, (sockaddr*)&serverAddr, &AddrLen);
		if (recvByte == 0)
		{
			cout << "client第二次挥手接收失败" << endl;
			return false;
		}
		else if (recvByte > 0)
		{
			//成功收到消息，检查校验和、ACK、ack
			if ((buffer2.flag & ACK) && buffer2.check() && (buffer2.AckNum == buffer1.SeqNum + 1))
			{
				cout << "client接收第二次挥手成功" << endl;
				break;
			}
			else
			{
				continue;
			}
		}
		//如果client发送第一次挥手超时，重新发送并重新计时
		if (clock() - buffer1start > MAX_WAIT_TIME)
		{
			cout << "client发送第一次挥手，第" << ++resendCount << "次超时，正在重传......" << endl;
			int sendByte = sendto(clientSocket, (char*)&buffer1, sizeof(buffer1), 0, (sockaddr*)&serverAddr, AddrLen);
			buffer1start = clock();
			if (sendByte > 0) {
				cout << "client发送第一次挥手重传成功" << endl;
				break;
			}
			else {
				cout << "client发送第一次挥手重传失败" << endl;
			}
		}
		if (resendCount == MAX_SEND_TIMES)
		{
			cout << "client发送第一次挥手超时重传已到最大次数，发送失败" << endl;
			return false;
		}
	}

	//接收第三次挥手的消息
	while (1)
	{
		int recvByte = recvfrom(clientSocket, (char*)&buffer3, sizeof(buffer3), 0, (sockaddr*)&serverAddr, &AddrLen);
		if (recvByte == 0)
		{
			cout << "client接收第三次挥手失败" << endl;
			return false;
		}
		else if (recvByte > 0)
		{
			//收到消息，检查校验和、FIN、ACK
			if ((buffer3.flag & ACK) && (buffer3.flag & FIN) && buffer3.check())
			{
				cout << "client接收第三次挥手成功" << endl;
				break;
			}
			else
			{
				continue;
			}
		}
	}

	//发送第四次挥手的消息（ACK有效）
	buffer4.SrcPort = ClientPORT;
	buffer4.DestPort = RouterPORT;
	buffer4.flag |= ACK;
	buffer4.SeqNum = ++seq;
	buffer4.AckNum = buffer3.SeqNum + 1;
	buffer4.calculateChecksum();
	sendByte = sendto(clientSocket, (char*)&buffer4, sizeof(buffer4), 0, (sockaddr*)&serverAddr, AddrLen);
	if (sendByte == 0)
	{
		cout << "client发送第四次挥手失败" << endl;
		return false;
	}
	cout << "client发送第四次挥手："
		<< "SrcPort: " << buffer4.SrcPort << ", "
		<< "DestPort: " << buffer4.DestPort << ", "
		<< "SeqNum: " << buffer4.SeqNum << ", "
		<< "AckNum: " << (buffer4.flag & ACK ? to_string(buffer4.AckNum) : "N/A") << ", "
		<< "Flag: " 
		<<"[ACK:" << (buffer4.flag & ACK ? "1" : "0") << "], "
		<< "CheckNum: " << buffer4.checkNum << endl;


	//第四次挥手之后还需等待2MSL，防止最后一个ACK丢失
	//此时client处于TIME_WAIT状态
	clock_t tempclock = clock();
	cout << "client正处于2MSL的等待时间" << endl;
	Packet tmp;
	while (clock() - tempclock < 2 * MAX_WAIT_TIME)
	{
		int recvByte = recvfrom(clientSocket, (char*)&tmp, sizeof(tmp), 0, (sockaddr*)&serverAddr, &AddrLen);
		if (recvByte == 0)
		{
			cout << "TIME_WAIT状态时收到错误消息，退出" << endl;
			return false;
		}
		else if (recvByte > 0)
		{
			sendByte = sendto(clientSocket, (char*)&buffer4, sizeof(buffer4), 0, (sockaddr*)&serverAddr, AddrLen);
			cout << "TIME_WAIT状态时发现最后一个ACK丢失，重发" << endl;
		}
	}
	cout << "\nclient关闭连接成功！" << endl;
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

	//创建UDP套接字
	SOCKET clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (clientSocket == INVALID_SOCKET)
	{
		cerr << "套接字创建失败，错误代码：" << WSAGetLastError() << endl;
		return -1;
	}

	// 设置套接字为非阻塞模式,便于检查超时
	unsigned long mode = 1;
	if (ioctlsocket(clientSocket, FIONBIO, &mode) != NO_ERROR)
	{
		cerr << "无法将套接字设置为非阻塞模式，错误代码：" << WSAGetLastError() << endl;
		closesocket(clientSocket);
		return -1;
	}
	cout << "套接字创建成功, 并设为非阻塞模式" << endl;
	cout << endl;

	//初始化路由器地址
	SOCKADDR_IN serverAddr;
	serverAddr.sin_family = AF_INET;
	int result1 = inet_pton(AF_INET, "127.0.0.1", &(serverAddr.sin_addr));
	serverAddr.sin_port = htons(RouterPORT);

	//初始化客户端地址
	SOCKADDR_IN clientAddr;
	clientAddr.sin_family = AF_INET;
	int result2 = inet_pton(AF_INET, "127.0.0.1", &(clientAddr.sin_addr));
	clientAddr.sin_port = htons(ClientPORT);

	//绑定套接字
	bind(clientSocket, (LPSOCKADDR)&clientAddr, sizeof(clientAddr));

	//建立连接
	bool connected = initiateConnection(clientSocket, serverAddr);
	if (!connected) {
		cerr << "客户端建立连接失败，退出程序" << endl;
		return -1;
	}

	//发送文件
	string filename;
	cout << "请输入要发送的文件名：" << endl;
	cin >> filename;
	cout << endl;
	sendFile(filename, serverAddr, clientSocket);

	//断开连接
	cout << "客户端准备断开连接..." << endl;
	bool breaked = terminateConnection(clientSocket, serverAddr);
	if (!breaked) {
		cerr << "客户端断开连接失败，退出程序" << endl;
		return -1;
	}

	// 清理套接字
	closesocket(clientSocket);
	WSACleanup();
	system("pause");
	return 0;
}