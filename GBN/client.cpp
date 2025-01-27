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

struct parameters {
	SOCKET clientSocket;      // 客户端的套接字，用于通信
	SOCKADDR_IN serverAddr;   // 服务器的地址信息
	int nummessage;           // 消息数量
};

int windowsize;

//队列充当发送缓冲区
queue<Packet> messageBuffer;
//滑动窗口
int base = 2;//基序号，发送方窗口的左边界
int nextseqnum = 2;//下一个待发送的数据包序列号

//计时
int timer;

//辅助加锁
HANDLE mutex;

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

queue<std::pair<int, int>> duplicateAckQueue;

//接收ack的线程
DWORD WINAPI recvackthread(PVOID useparameter)
{
	mutex = CreateMutex(NULL, FALSE, NULL); // 创建互斥锁
	parameters* p = (parameters*)useparameter;
	SOCKADDR_IN serverAddr = p->serverAddr;
	SOCKET clientSocket = p->clientSocket;
	int nummessage = p->nummessage;
	int AddrLen = sizeof(serverAddr);

	unsigned long mode = 1;
	ioctlsocket(clientSocket, FIONBIO, &mode);// 将套接字设为非阻塞模式
	int lastAckNum = -1;  // 用于记录上一个接收到的 ACK
	int count = 0;

	while (1)
	{
		Packet recvMsg;
		int recvByte = recvfrom(clientSocket, (char*)&recvMsg, sizeof(recvMsg), 0, (sockaddr*)&serverAddr, &AddrLen);
		//成功收到消息
		if (recvByte > 0)
		{
			//检查校验和
			if (recvMsg.check())
			{
				if (recvMsg.AckNum >= base) {
					WaitForSingleObject(mutex, INFINITE);  // 等待并获取互斥锁
					updateBuffer(recvMsg.AckNum);
					base = recvMsg.AckNum + 1;
					// 更新已处理的ACK号
					cout << "client收到: AckNum = " << recvMsg.AckNum << "的ACK" << endl;
					cout << "[窗口情况（收到消息）] 窗口总大小=" << windowsize << endl;
					cout << "已发送但未收到确认的报文数量为" << (nextseqnum - base) << endl;
					if (nextseqnum - base) {
						cout << "报文序列号为：";
					}
					queue<Packet> tempQueue = messageBuffer;  // 创建一个临时队列用于遍历
					while (!tempQueue.empty()) {
						Packet pkt = tempQueue.front();
						tempQueue.pop();
						cout << pkt.SeqNum << " ";
					}
					cout << endl << endl;
					ReleaseMutex(mutex);// 释放互斥锁
				}
				if (base != nextseqnum) {//有数据包正在等待确认,监控其是否超时
					timer = clock();
				}
				//判断结束的情况
				if (recvMsg.AckNum == nummessage + 1)
				{
					cout << "\n文件传输结束" << endl;
					finish = true;
					return 0;
				}
				if (lastAckNum != recvMsg.AckNum) {
					count = 0;
					lastAckNum = recvMsg.AckNum;
				}
				else {
					count++;
					duplicateAckQueue.push({ recvMsg.AckNum, count });
				}
			}
			// 校验失败，则忽略报文并继续等待
		}
	}
	CloseHandle(mutex); // 清理互斥锁
	return 0;
}


//实现文件传输
void sendFile(string filename, SOCKADDR_IN serverAddr, SOCKET clientSocket)
{
	mutex = CreateMutex(NULL, FALSE, NULL); // 创建互斥锁
	clock_t starttime = clock();
	string realname = filename;
	filename = "D:\\3.1\\计算机网络\\Lab3\\lab3-1\\测试文件\\" + filename;
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

	int totalPackets = fileSize / Max_Msg_Size;  // 可填满的数据包数量
	int remainingBytes = fileSize % Max_Msg_Size;   // 剩余数据部分
	int nummessage = (remainingBytes != 0) ? (totalPackets + 2) : (totalPackets + 1);  // 总的报文数量(?+1)

	// 参数结构体，用于传递给接收 ACK 的线程
	parameters useparameter;
	useparameter.serverAddr = serverAddr;
	useparameter.clientSocket = clientSocket;
	useparameter.nummessage = nummessage;

	// 创建一个线程接收 ACK
	HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)recvackthread, &useparameter, 0, 0);

	int count = 0;  // 用于控制发送的数据流

	while (1) {
		// 定期检查重复 ACK 队列并输出信息
		while (!duplicateAckQueue.empty()) {
			auto ackInfo = duplicateAckQueue.front();
			duplicateAckQueue.pop();
			cout << "[忽略重复的ACK] AckNum = " << ackInfo.first << ", 重复次数: " << ackInfo.second << endl;
		}

		// 如果窗口未满，并且有新的数据可以发送
		if (nextseqnum < base + windowsize && nextseqnum < nummessage + 2) {
			Packet datamessage;  // 创建一个数据报文对象
			if (nextseqnum == 2) {
				// 发送文件名和文件大小作为第一个报文
				datamessage.SrcPort = ClientPORT;
				datamessage.DestPort = RouterPORT;
				datamessage.length = fileSize;
				datamessage.flag |= FileName;  // 设置标志位，表示这是文件信息报文
				datamessage.SeqNum = nextseqnum;
				// 将文件名放到数据字段中
				for (int i = 0; i < realname.size(); i++)
					datamessage.data[i] = realname[i];
				datamessage.data[realname.size()] = '\0';
				datamessage.calculateChecksum();  // 设置校验和
			}
			else if (nextseqnum == totalPackets + 3 && remainingBytes > 0) {
				// 发送剩余数据报文
				datamessage.SrcPort = ClientPORT;
				datamessage.DestPort = RouterPORT;
				datamessage.SeqNum = nextseqnum;
				datamessage.length = remainingBytes;
				for (int j = 0; j < remainingBytes; j++) {
					datamessage.data[j] = fileBuffer[totalPackets * Max_Msg_Size + j];
				}
				datamessage.calculateChecksum();
			}
			else {
				//满载的数据报文发送
				datamessage.SrcPort = ClientPORT;
				datamessage.DestPort = RouterPORT;
				datamessage.SeqNum = nextseqnum;
				datamessage.length = Max_Msg_Size;
				for (int j = 0; j < Max_Msg_Size; j++)
				{
					datamessage.data[j] = fileBuffer[count * Max_Msg_Size + j];
				}
				datamessage.calculateChecksum();
				count++;
			}
			if (base == nextseqnum)//重新启动计时器
			{
				timer = clock();
			}
			{
			WaitForSingleObject(mutex, INFINITE);  // 等待并获取互斥锁
			messageBuffer.push(datamessage);
			sendto(clientSocket, (char*)&datamessage, sizeof(datamessage), 0, (sockaddr*)&serverAddr, sizeof(SOCKADDR_IN));
			nextseqnum++;
			cout << "client发送: SeqNum = " << datamessage.SeqNum << ", CheckNum = " << datamessage.checkNum <<", Data Length = " <<datamessage.length <<" bytes的数据报文" << endl;
			cout << "[窗口情况（发送消息）] 窗口总大小=" << windowsize << endl;
			cout << "已发送但未收到确认的报文数量为" << (nextseqnum - base) << endl;
			if (nextseqnum - base) {
				cout << "报文序列号为：";
			}
			queue<Packet> tempQueue = messageBuffer;  // 创建一个临时队列用于遍历
			while (!tempQueue.empty()) {
				Packet pkt = tempQueue.front();
				tempQueue.pop();
				cout << pkt.SeqNum << " ";
			}
			cout << endl << endl;
			ReleaseMutex(mutex);    // 释放互斥锁
			}
		}
		if (clock() - timer > MAX_WAIT_TIME) {
			WaitForSingleObject(mutex, INFINITE);  // 获取互斥锁
			// 重传所有未确认的报文
			cout << endl;
			if (!messageBuffer.empty()) {
				Packet resendMsg = messageBuffer.front();
				cout << "报文 " << resendMsg.SeqNum <<" 已超时" << endl;
			}
			for (int i = 0; i < nextseqnum - base; i++) {
				Packet resendMsg = messageBuffer.front();
				sendto(clientSocket, (char*)&resendMsg, sizeof(resendMsg), 0, (sockaddr*)&serverAddr, sizeof(SOCKADDR_IN));
				cout << "正在重传: SeqNum = " << resendMsg.SeqNum << "的数据报文" << endl;
				messageBuffer.push(resendMsg);  // 将报文重新放入队列
				messageBuffer.pop();  // 从队列中移除
			}
			ReleaseMutex(mutex);  // 释放互斥锁
			cout << endl;
			timer = clock();  // 重置计时器
		}
		//如果结束就退出
		if (finish == true) {
			break;
		}
	}

	CloseHandle(hThread);
	
	//计算传输时间和吞吐率
	clock_t endtime = clock();
	double totalTime = (double)(endtime - starttime) / CLOCKS_PER_SEC;
	double throughput = ((float)fileSize * 8) / totalTime;

	cout << "\n总传输时间为: " << totalTime << "s" << endl;
	cout << "平均吞吐率: " << std::fixed << std::setprecision(2) << throughput << " bit/s" << endl;
	delete[] fileBuffer;//释放内存
	CloseHandle(mutex); // 清理互斥锁
	return;
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

	//设置窗口大小
	cout << "请输入窗口大小：";
	cin >> windowsize;

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

	closesocket(clientSocket);
	WSACleanup();
	system("pause");
	return 0;
}