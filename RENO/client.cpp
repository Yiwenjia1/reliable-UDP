#include <ws2tcpip.h> 
#include <iostream>
#include <winsock2.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <fstream>
#include <queue>
#include <vector>
#include <iomanip>
#include <set>
#include<mutex>
#include <sstream>
#include <cmath>

#pragma comment (lib, "ws2_32.lib")

using namespace std;

const int RouterPORT = 30000;
const int ClientPORT = 20000;

#define MAX_WAIT_TIME  1000 //超时时限（ms）
#define MAX_SEND_TIMES  6 //最大重传次数
#define Max_File_Size 15000000 //最大文件大小
#define Max_Msg_Size 10000 //最大数据包大小,MSS

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


float cwnd = 1;//窗口大小
int ssthresh = 16;//阈值
int status = 0;//状态
bool resend = false;
int lastAckNum = -1;

int seq = 0;

//队列充当发送缓冲区
queue<Packet> messageBuffer;
//滑动窗口
int base = 2;//基序号，发送方窗口的左边界
int nextseqnum = 2;//下一个待发送的数据包序列号

//计时
int timer;

//辅助加锁
mutex outputMutex;
//标志位
bool finish = false;

//实现client的三次握手
bool initiateConnection(SOCKET clientSocket, SOCKADDR_IN serverAddr)
{
	int AddrLen = sizeof(serverAddr);
	Packet buffer1, buffer2, buffer3;

	//发送第一次握手的消息（SYN有效，seq=x（相对seq就是0））
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

		//client发送第一次握手超时，重新发送并重新计时
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

	//发送第三次握手的消息（ACK有效，seq=x+1,ack=y+1）
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
		<< "[ACK:" << (buffer3.flag & ACK ? "1" : "0") << "], "
		<< "CheckNum: " << buffer3.checkNum << endl;
	cout << "client连接成功！" << endl;
	cout << endl;
	return true;
}

void updateBuffer(int ackNum) {//移除序列号小于等于 ackNum 的数据包
	while (!messageBuffer.empty()) {
		const Packet& frontMsg = messageBuffer.front();
		if (frontMsg.SeqNum <= ackNum) {
			messageBuffer.pop();  // 弹出已确认的报文
		}
		else {
			break;  // 一旦遇到一个未确认的报文，退出	
		}
	}
}

void safeOutput(const std::string& message) {
	std::lock_guard<std::mutex> lock(outputMutex);
	std::cout << message;
}

queue<std::pair<int, int>> duplicateAckQueue;

struct parameters {
	SOCKADDR_IN serverAddr;
	SOCKET clientSocket;
	int nummessage;
};



DWORD WINAPI recvThread(PVOID pParam)
{
	parameters* para = (parameters*)pParam;
	SOCKADDR_IN serverAddr = para->serverAddr;
	SOCKET clientSocket = para->clientSocket;
	int nummessage = para->nummessage;
	int AddrLen = sizeof(serverAddr);
	unsigned long mode = 1;
	ioctlsocket(clientSocket, FIONBIO, &mode);
	int lastAckNum = -1;
	int count = 0;

	while (1)
	{
		Packet recvMsg;
		int recvByte = recvfrom(clientSocket, (char*)&recvMsg, sizeof(recvMsg), 0, (sockaddr*)&serverAddr, &AddrLen);
		if (recvByte > 0)
		{
			if (recvMsg.check())
			{
				if (recvMsg.AckNum >= base) {
					updateBuffer(recvMsg.AckNum);
					base = recvMsg.AckNum + 1;
					safeOutput("client收到: AckNum = " + to_string(recvMsg.AckNum) + "的ACK" + "\n");
				}
				if (base != nextseqnum)
					timer = clock();
				// 判断结束的情况
				if (recvMsg.AckNum == nummessage + 1)
				{
					safeOutput("\n-----------------文件传输结束-----------------");
					finish = true;
					return 0;
				}

				switch (status)
				{
				case 0: // 慢启动
					//new ack
					if (lastAckNum != recvMsg.AckNum)
					{
						cwnd++; // 窗口大小增加
						safeOutput("收到新ACK，窗口大小增加到 " + std::to_string(cwnd) + "\n");
						count = 0;
						lastAckNum = recvMsg.AckNum;
					}
					// duplicate ack 
					else
					{
						count++;
					}
					// 进入快速恢复
					if (count == 3)
					{
						resend = 1;
						status = 2;
						ssthresh = max(cwnd / 2, 1);
						cwnd = ssthresh + 3;
						safeOutput("\n================================================"
							"\n连续收到三次冗余ACK，进入快速恢复状态\n"
							"ssthresh 为 " + std::to_string(ssthresh) +
							"，cwnd为 " + std::to_string(cwnd) + "\n"
							"================================================\n" + "\n");
						break;
					}
					else if (cwnd >= ssthresh) // 进入拥塞避免
					{
						status = 1;
						safeOutput("\n================================================"
							"\n慢启动状态下窗口大小超过阈值，进入拥塞避免状态\n"
							"ssthresh 为 " + std::to_string(ssthresh) +
							"，cwnd为 " + std::to_string(cwnd) + "\n"
							"================================================\n" + "\n");
					}
					break;

				case 1: // 拥塞避免
					if (lastAckNum != recvMsg.AckNum)
					{
						cwnd += 1.0 / cwnd;
						safeOutput("收到新ACK：窗口大小增加到 " + std::to_string(cwnd) + "\n");
						count = 0;
						lastAckNum = recvMsg.AckNum;
					}
					else
					{
						count++;
					}
					if (count == 3)
					{
						resend = 1;
						status = 2;
						ssthresh = max(cwnd / 2, 1);
						cwnd = ssthresh + 3;
						safeOutput("\n================================================"
							"\n连续收到三次冗余ACK，进入快速恢复状态\n"
							"ssthresh 为 " + std::to_string(ssthresh) +
							"，cwnd为 " + std::to_string(cwnd) + "\n"
							"================================================\n" + "\n");
					}
					break;

				case 2: // 快速恢复
					if (lastAckNum != recvMsg.AckNum)
					{
						status = 1;
						cwnd = ssthresh;
						count = 0;
						lastAckNum = recvMsg.AckNum;
						safeOutput("\n================================================"
							"\n快速恢复收到新ACK，进入拥塞避免状态\n"
							"ssthresh 为 " + std::to_string(ssthresh) +
							"，cwnd为 " + std::to_string(cwnd) + "\n"
							"================================================\n" + "\n");
					}
					else
					{
						cwnd++;
						count++;
					}
					if (count == 3)
					{
						resend = true;
						ssthresh = max(cwnd / 2, 1);
						cwnd = ssthresh + 3;
					}
					break;
				}
			}
		}
	}
	return 0;
}

void sendFile(string filename, SOCKADDR_IN serverAddr, SOCKET clientSocket)
{
	int startTime = clock();
	string realname = filename;
	filename = "D:\\3.1\\计算机网络\\Lab3\\lab3-1\\测试文件\\" + filename;
	ifstream fin(filename.c_str(), ifstream::binary);
	if (!fin) {
		safeOutput("无法打开文件！");
		return;
	}
	BYTE* fileBuffer = new BYTE[Max_File_Size];
	unsigned int fileSize = 0;
	BYTE byte = fin.get();
	while (fin) {
		fileBuffer[fileSize++] = byte;
		byte = fin.get();
	}
	fin.close();
	int totalPackets = fileSize / Max_Msg_Size;
	int remainingBytes = fileSize % Max_Msg_Size;

	int nummessage = remainingBytes > 0 ? totalPackets + 2 : totalPackets + 1;

	parameters param;
	param.serverAddr = serverAddr;
	param.clientSocket = clientSocket;
	param.nummessage = nummessage;
	HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)recvThread, &param, 0, 0);
	int count = 0;
	while (1)
	{
		if (nextseqnum < base + cwnd && nextseqnum < nummessage + 2)
		{
			Packet sendMsg;
			if (nextseqnum == 2)
			{
				sendMsg.SrcPort = ClientPORT;
				sendMsg.DestPort = RouterPORT;
				sendMsg.length = fileSize;
				sendMsg.flag |= FileName;
				sendMsg.SeqNum = nextseqnum;
				for (int i = 0; i < realname.size(); i++)
					sendMsg.data[i] = realname[i];
				sendMsg.data[realname.size()] = '\0';
				sendMsg.calculateChecksum();
				safeOutput("================================================\n");
				safeOutput("初始状态：慢启动阶段\n");
				safeOutput("ssthresh 为 " + std::to_string(ssthresh) +
					"，cwnd为 " + std::to_string(cwnd) + "\n" + "================================================\n" + "\n");
			}
			else if (nextseqnum == totalPackets + 3 && remainingBytes > 0)
			{
				sendMsg.SrcPort = ClientPORT;
				sendMsg.DestPort = RouterPORT;
				sendMsg.SeqNum = nextseqnum;
				sendMsg.length = remainingBytes;
				for (int j = 0; j < remainingBytes; j++)
				{
					sendMsg.data[j] = fileBuffer[totalPackets * Max_Msg_Size + j];
				}
				sendMsg.calculateChecksum();
			}
			else
			{
				sendMsg.SrcPort = ClientPORT;
				sendMsg.DestPort = RouterPORT;
				sendMsg.SeqNum = nextseqnum;
				sendMsg.length = Max_Msg_Size;
				for (int j = 0; j < Max_Msg_Size; j++)
				{
					sendMsg.data[j] = fileBuffer[count * Max_Msg_Size + j];
				}
				sendMsg.calculateChecksum();
				count++;
			}
			if (base == nextseqnum)//启动计时器，所有数据包都已被确认
			{
				timer = clock();
			}
			messageBuffer.push(sendMsg);
			sendto(clientSocket, (char*)&sendMsg, sizeof(sendMsg), 0, (sockaddr*)&serverAddr, sizeof(SOCKADDR_IN));
			safeOutput("client发送: SeqNum = " + std::to_string(sendMsg.SeqNum) + ", CheckNum = " + to_string(sendMsg.checkNum) + ", Data Length = " + to_string(sendMsg.length) + " bytes的数据报文" + "\n");
			nextseqnum++;

			safeOutput("[当前窗口情况] 窗口总大小：" + std::to_string(cwnd) +
				"，已发送但未收到确认的报文数量为:" + std::to_string(nextseqnum - base) + "\n");

			std::ostringstream oss;
			if (nextseqnum - base) {
				oss << "报文序列号为：";
				queue<Packet> tempQueue = messageBuffer;
				while (!tempQueue.empty()) {
					Packet pkt = tempQueue.front();
					tempQueue.pop();
					oss << pkt.SeqNum << " ";
				}
				oss << "\n";
				safeOutput(oss.str());
			}
			safeOutput("\n");

		}
		if (clock() - timer > MAX_WAIT_TIME || resend)
		{
			if (clock() - timer > MAX_WAIT_TIME)
			{
				ssthresh = max(cwnd / 2, 1);
				cwnd = 1;
				if (status) {
					safeOutput("\n================================================"
						"\n超时，进入慢启动状态\n"
						"ssthresh 为 " + std::to_string(ssthresh) +
						"，cwnd为 " + std::to_string(cwnd) + "\n"
						"================================================\n" + "\n");
				}
				status = 0;
			}
			if (resend) {
				cout << "连续收到三次冗余ACK，快速重传!" << endl << endl;
			}
			else {
				cout << "开始超时重传!" << endl << endl;
			}
			//重发当前缓冲区的message
			for (int i = 0; i < nextseqnum - base; i++) {
				Packet resendMsg = messageBuffer.front();
				sendto(clientSocket, (char*)&resendMsg, sizeof(resendMsg), 0, (sockaddr*)&serverAddr, sizeof(SOCKADDR_IN));
				safeOutput("正在重传: SeqNum = " + to_string(resendMsg.SeqNum) + "的数据报文\n");
				messageBuffer.push(resendMsg);  // 将报文重新放入队列
				messageBuffer.pop();  // 从队列中移除
				safeOutput("\n");
			}
			timer = clock();
			resend = 0;
		}
		if (finish)
		{
			break;
		}
	}
	CloseHandle(hThread);
	int endTime = clock();
	double totalTime = (double)(endTime - startTime) / CLOCKS_PER_SEC;
	double throughput = ((float)fileSize * 8) / totalTime;
	cout << "\n总传输时间为: " << totalTime << "s" << endl;
	cout << "平均吞吐率: " << std::fixed << std::setprecision(2) << throughput << " bit/s" << endl;
}

//实现client的四次挥手
bool terminateConnection(SOCKET clientSocket, SOCKADDR_IN serverAddr)
{
	//用这个更新seq
	seq = nextseqnum;
	int AddrLen = sizeof(serverAddr);
	Packet buffer1, buffer2, buffer3, buffer4;

	//发送第一次挥手（FIN、ACK有效，seq是之前的发送完数据包后的序列号，之前的序列号每次+1)
	buffer1.SrcPort = ClientPORT;
	buffer1.DestPort = RouterPORT;
	buffer1.flag |= FIN;
	buffer1.flag |= ACK;
	buffer1.SeqNum = seq;
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
		<< "[ACK:" << (buffer1.flag & ACK ? "1" : "0")
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
		//client发送第一次挥手超时，重新发送并重新计时
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

	//发送第四次挥手的消息（ACK有效，ack等于第三次挥手消息的seq+1，seq自动向下递增）
	buffer4.SrcPort = ClientPORT;
	buffer4.DestPort = RouterPORT;
	buffer4.flag |= ACK;
	buffer4.SeqNum = ++seq;//
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
		<< "[ACK:" << (buffer4.flag & ACK ? "1" : "0") << "], "
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

	//创建socket，是UDP套接字
	SOCKET clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (clientSocket == INVALID_SOCKET)
	{
		cerr << "套接字创建失败，错误代码：" << WSAGetLastError() << endl;
		return -1;
	}

	// 设置套接字为非阻塞模式
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

	//bind
	bind(clientSocket, (LPSOCKADDR)&clientAddr, sizeof(clientAddr));

	//建立连接
	bool connected = initiateConnection(clientSocket, serverAddr);
	if (!connected) {
		cerr << "客户端建立连接失败，退出程序" << endl;
		return -1;
	}

	////设置窗口大小
	//cout << "请输入窗口大小：";
	//cin >> windowsize;

	//这里设计的是发送一次文件就会退出
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
	//CloseHandle(mutex);

	closesocket(clientSocket);
	WSACleanup();
	system("pause");
	return 0;
}