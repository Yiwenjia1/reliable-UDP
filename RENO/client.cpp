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

#define MAX_WAIT_TIME  1000 //��ʱʱ�ޣ�ms��
#define MAX_SEND_TIMES  6 //����ش�����
#define Max_File_Size 15000000 //����ļ���С
#define Max_Msg_Size 10000 //������ݰ���С,MSS

// ���岻ͬ�Ŀ��Ʊ�־������SYN, ACK, FIN���ļ���
const unsigned short SYN = 0x1;//0001
const unsigned short ACK = 0x2;//0010
const unsigned short FIN = 0x4;//0100
const unsigned short FileName = 0x8;//1000

#pragma pack(1)
struct Packet
{
	//ͷ����һ��20�ֽڣ�
	unsigned short SrcPort, DestPort;//Դ�˿ںš�Ŀ�Ķ˿ں�
	unsigned int SeqNum;// ���к�
	unsigned int AckNum;//ȷ�Ϻ�
	unsigned int length;//���ݶγ���
	unsigned short flag;//��־λ
	unsigned short checkNum;//У���
	BYTE data[Max_Msg_Size];// ���ݶΣ����10000�ֽ�

	Packet();
	void calculateChecksum();
	bool check();
};


#pragma pack()
//���캯������ʼ��ȫΪ0
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


float cwnd = 1;//���ڴ�С
int ssthresh = 16;//��ֵ
int status = 0;//״̬
bool resend = false;
int lastAckNum = -1;

int seq = 0;

//���г䵱���ͻ�����
queue<Packet> messageBuffer;
//��������
int base = 2;//����ţ����ͷ����ڵ���߽�
int nextseqnum = 2;//��һ�������͵����ݰ����к�

//��ʱ
int timer;

//��������
mutex outputMutex;
//��־λ
bool finish = false;

//ʵ��client����������
bool initiateConnection(SOCKET clientSocket, SOCKADDR_IN serverAddr)
{
	int AddrLen = sizeof(serverAddr);
	Packet buffer1, buffer2, buffer3;

	//���͵�һ�����ֵ���Ϣ��SYN��Ч��seq=x�����seq����0����
	buffer1.SrcPort = ClientPORT;
	buffer1.DestPort = RouterPORT;
	buffer1.flag |= SYN;
	buffer1.SeqNum = seq;
	buffer1.calculateChecksum();


	int sendByte = sendto(clientSocket, (char*)&buffer1, sizeof(buffer1), 0, (sockaddr*)&serverAddr, AddrLen);
	clock_t buffer1start = clock();
	if (sendByte > 0)
	{
		cout << "client���͵�һ�����֣�"
			<< "SrcPort: " << buffer1.SrcPort << ", "
			<< "DestPort: " << buffer1.DestPort << ", "
			<< "SeqNum: " << buffer1.SeqNum << ", "
			<< "Flag: " << "[SYN: " << (buffer1.flag & SYN ? "1" : "0") << "], "
			<< "CheckNum: " << buffer1.checkNum << endl;
	}

	int resendCount = 0;
	//���յڶ������ֵ���Ϣ
	while (true)
	{
		int recvByte = recvfrom(clientSocket, (char*)&buffer2, sizeof(buffer2), 0, (sockaddr*)&serverAddr, &AddrLen);
		if (recvByte > 0)
		{
			//�ɹ��յ���Ϣ�����У��͡�ACK��SYN��ack
			if ((buffer2.flag & ACK) && (buffer2.flag & SYN) && buffer2.check() && (buffer2.AckNum == buffer1.SeqNum + 1))
			{
				cout << "client���յڶ������ֳɹ�" << endl;
				break;
			}
			else {
				cout << "���յڶ���������Ϣ���ʧ��" << endl;
			}
		}

		//client���͵�һ�����ֳ�ʱ�����·��Ͳ����¼�ʱ
		if (clock() - buffer1start > MAX_WAIT_TIME)
		{
			if (++resendCount > MAX_SEND_TIMES) {
				cout << "client���͵�һ�����ֳ�ʱ�ش��ѵ�������������ʧ��" << endl;
				return false;
			}
			cout << "client���͵�һ�����֣���" << resendCount << "�γ�ʱ�������ش�......" << endl;
			int sendByte = sendto(clientSocket, (char*)&buffer1, sizeof(buffer1), 0, (sockaddr*)&serverAddr, AddrLen);
			buffer1start = clock();
			if (sendByte <= 0) {
				cout << "client��һ�������ش�ʧ��" << endl;
				return false;
			}
		}
	}

	//���͵��������ֵ���Ϣ��ACK��Ч��seq=x+1,ack=y+1��
	buffer3.SrcPort = ClientPORT;
	buffer3.DestPort = RouterPORT;
	buffer3.flag |= ACK;
	buffer3.SeqNum = ++seq;
	buffer3.AckNum = buffer2.SeqNum + 1;
	buffer3.calculateChecksum();

	sendByte = sendto(clientSocket, (char*)&buffer3, sizeof(buffer3), 0, (sockaddr*)&serverAddr, AddrLen);

	if (sendByte == 0)
	{
		cout << "client���͵���������ʧ��" << endl;
		return false;
	}

	cout << "client���͵���������:"
		<< "SrcPort: " << buffer3.SrcPort << ", "
		<< "DestPort: " << buffer3.DestPort << ", "
		<< "SeqNum: " << buffer3.SeqNum << ", "
		<< "AckNum: " << (buffer3.flag & ACK ? to_string(buffer3.AckNum) : "0") << ", "
		<< "Flag: "
		<< "[ACK:" << (buffer3.flag & ACK ? "1" : "0") << "], "
		<< "CheckNum: " << buffer3.checkNum << endl;
	cout << "client���ӳɹ���" << endl;
	cout << endl;
	return true;
}

void updateBuffer(int ackNum) {//�Ƴ����к�С�ڵ��� ackNum �����ݰ�
	while (!messageBuffer.empty()) {
		const Packet& frontMsg = messageBuffer.front();
		if (frontMsg.SeqNum <= ackNum) {
			messageBuffer.pop();  // ������ȷ�ϵı���
		}
		else {
			break;  // һ������һ��δȷ�ϵı��ģ��˳�	
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
					safeOutput("client�յ�: AckNum = " + to_string(recvMsg.AckNum) + "��ACK" + "\n");
				}
				if (base != nextseqnum)
					timer = clock();
				// �жϽ��������
				if (recvMsg.AckNum == nummessage + 1)
				{
					safeOutput("\n-----------------�ļ��������-----------------");
					finish = true;
					return 0;
				}

				switch (status)
				{
				case 0: // ������
					//new ack
					if (lastAckNum != recvMsg.AckNum)
					{
						cwnd++; // ���ڴ�С����
						safeOutput("�յ���ACK�����ڴ�С���ӵ� " + std::to_string(cwnd) + "\n");
						count = 0;
						lastAckNum = recvMsg.AckNum;
					}
					// duplicate ack 
					else
					{
						count++;
					}
					// ������ٻָ�
					if (count == 3)
					{
						resend = 1;
						status = 2;
						ssthresh = max(cwnd / 2, 1);
						cwnd = ssthresh + 3;
						safeOutput("\n================================================"
							"\n�����յ���������ACK��������ٻָ�״̬\n"
							"ssthresh Ϊ " + std::to_string(ssthresh) +
							"��cwndΪ " + std::to_string(cwnd) + "\n"
							"================================================\n" + "\n");
						break;
					}
					else if (cwnd >= ssthresh) // ����ӵ������
					{
						status = 1;
						safeOutput("\n================================================"
							"\n������״̬�´��ڴ�С������ֵ������ӵ������״̬\n"
							"ssthresh Ϊ " + std::to_string(ssthresh) +
							"��cwndΪ " + std::to_string(cwnd) + "\n"
							"================================================\n" + "\n");
					}
					break;

				case 1: // ӵ������
					if (lastAckNum != recvMsg.AckNum)
					{
						cwnd += 1.0 / cwnd;
						safeOutput("�յ���ACK�����ڴ�С���ӵ� " + std::to_string(cwnd) + "\n");
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
							"\n�����յ���������ACK��������ٻָ�״̬\n"
							"ssthresh Ϊ " + std::to_string(ssthresh) +
							"��cwndΪ " + std::to_string(cwnd) + "\n"
							"================================================\n" + "\n");
					}
					break;

				case 2: // ���ٻָ�
					if (lastAckNum != recvMsg.AckNum)
					{
						status = 1;
						cwnd = ssthresh;
						count = 0;
						lastAckNum = recvMsg.AckNum;
						safeOutput("\n================================================"
							"\n���ٻָ��յ���ACK������ӵ������״̬\n"
							"ssthresh Ϊ " + std::to_string(ssthresh) +
							"��cwndΪ " + std::to_string(cwnd) + "\n"
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
	filename = "D:\\3.1\\���������\\Lab3\\lab3-1\\�����ļ�\\" + filename;
	ifstream fin(filename.c_str(), ifstream::binary);
	if (!fin) {
		safeOutput("�޷����ļ���");
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
				safeOutput("��ʼ״̬���������׶�\n");
				safeOutput("ssthresh Ϊ " + std::to_string(ssthresh) +
					"��cwndΪ " + std::to_string(cwnd) + "\n" + "================================================\n" + "\n");
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
			if (base == nextseqnum)//������ʱ�����������ݰ����ѱ�ȷ��
			{
				timer = clock();
			}
			messageBuffer.push(sendMsg);
			sendto(clientSocket, (char*)&sendMsg, sizeof(sendMsg), 0, (sockaddr*)&serverAddr, sizeof(SOCKADDR_IN));
			safeOutput("client����: SeqNum = " + std::to_string(sendMsg.SeqNum) + ", CheckNum = " + to_string(sendMsg.checkNum) + ", Data Length = " + to_string(sendMsg.length) + " bytes�����ݱ���" + "\n");
			nextseqnum++;

			safeOutput("[��ǰ�������] �����ܴ�С��" + std::to_string(cwnd) +
				"���ѷ��͵�δ�յ�ȷ�ϵı�������Ϊ:" + std::to_string(nextseqnum - base) + "\n");

			std::ostringstream oss;
			if (nextseqnum - base) {
				oss << "�������к�Ϊ��";
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
						"\n��ʱ������������״̬\n"
						"ssthresh Ϊ " + std::to_string(ssthresh) +
						"��cwndΪ " + std::to_string(cwnd) + "\n"
						"================================================\n" + "\n");
				}
				status = 0;
			}
			if (resend) {
				cout << "�����յ���������ACK�������ش�!" << endl << endl;
			}
			else {
				cout << "��ʼ��ʱ�ش�!" << endl << endl;
			}
			//�ط���ǰ��������message
			for (int i = 0; i < nextseqnum - base; i++) {
				Packet resendMsg = messageBuffer.front();
				sendto(clientSocket, (char*)&resendMsg, sizeof(resendMsg), 0, (sockaddr*)&serverAddr, sizeof(SOCKADDR_IN));
				safeOutput("�����ش�: SeqNum = " + to_string(resendMsg.SeqNum) + "�����ݱ���\n");
				messageBuffer.push(resendMsg);  // ���������·������
				messageBuffer.pop();  // �Ӷ������Ƴ�
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
	cout << "\n�ܴ���ʱ��Ϊ: " << totalTime << "s" << endl;
	cout << "ƽ��������: " << std::fixed << std::setprecision(2) << throughput << " bit/s" << endl;
}

//ʵ��client���Ĵλ���
bool terminateConnection(SOCKET clientSocket, SOCKADDR_IN serverAddr)
{
	//���������seq
	seq = nextseqnum;
	int AddrLen = sizeof(serverAddr);
	Packet buffer1, buffer2, buffer3, buffer4;

	//���͵�һ�λ��֣�FIN��ACK��Ч��seq��֮ǰ�ķ��������ݰ�������кţ�֮ǰ�����к�ÿ��+1)
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
		cout << "client���͵�һ�λ���ʧ�ܣ��˳�" << endl;
		return false;
	}
	cout << "client���͵�һ�λ��֣�"
		<< "SrcPort: " << buffer1.SrcPort << ", "
		<< "DestPort: " << buffer1.DestPort << ", "
		<< "SeqNum: " << buffer1.SeqNum << ", "
		<< "Flag: "
		<< "[ACK:" << (buffer1.flag & ACK ? "1" : "0")
		<< "] [FIN: " << (buffer1.flag & FIN ? "1" : "0") << "], "
		<< "CheckNum : " << buffer1.checkNum << endl;
	int resendCount = 0;
	//���յڶ��λ��ֵ���Ϣ
	while (1)
	{
		int recvByte = recvfrom(clientSocket, (char*)&buffer2, sizeof(buffer2), 0, (sockaddr*)&serverAddr, &AddrLen);
		if (recvByte == 0)
		{
			cout << "client�ڶ��λ��ֽ���ʧ��" << endl;
			return false;
		}
		else if (recvByte > 0)
		{
			//�ɹ��յ���Ϣ�����У��͡�ACK��ack
			if ((buffer2.flag & ACK) && buffer2.check() && (buffer2.AckNum == buffer1.SeqNum + 1))
			{
				cout << "client���յڶ��λ��ֳɹ�" << endl;
				break;
			}
			else
			{
				continue;
			}
		}
		//client���͵�һ�λ��ֳ�ʱ�����·��Ͳ����¼�ʱ
		if (clock() - buffer1start > MAX_WAIT_TIME)
		{
			cout << "client���͵�һ�λ��֣���" << ++resendCount << "�γ�ʱ�������ش�......" << endl;
			int sendByte = sendto(clientSocket, (char*)&buffer1, sizeof(buffer1), 0, (sockaddr*)&serverAddr, AddrLen);
			buffer1start = clock();
			if (sendByte > 0) {
				cout << "client���͵�һ�λ����ش��ɹ�" << endl;
				break;
			}
			else {
				cout << "client���͵�һ�λ����ش�ʧ��" << endl;
			}
		}
		if (resendCount == MAX_SEND_TIMES)
		{
			cout << "client���͵�һ�λ��ֳ�ʱ�ش��ѵ�������������ʧ��" << endl;
			return false;
		}
	}

	//���յ����λ��ֵ���Ϣ
	while (1)
	{
		int recvByte = recvfrom(clientSocket, (char*)&buffer3, sizeof(buffer3), 0, (sockaddr*)&serverAddr, &AddrLen);
		if (recvByte == 0)
		{
			cout << "client���յ����λ���ʧ��" << endl;
			return false;
		}
		else if (recvByte > 0)
		{
			//�յ���Ϣ�����У��͡�FIN��ACK
			if ((buffer3.flag & ACK) && (buffer3.flag & FIN) && buffer3.check())
			{
				cout << "client���յ����λ��ֳɹ�" << endl;
				break;
			}
			else
			{
				continue;
			}
		}
	}

	//���͵��Ĵλ��ֵ���Ϣ��ACK��Ч��ack���ڵ����λ�����Ϣ��seq+1��seq�Զ����µ�����
	buffer4.SrcPort = ClientPORT;
	buffer4.DestPort = RouterPORT;
	buffer4.flag |= ACK;
	buffer4.SeqNum = ++seq;//
	buffer4.AckNum = buffer3.SeqNum + 1;
	buffer4.calculateChecksum();
	sendByte = sendto(clientSocket, (char*)&buffer4, sizeof(buffer4), 0, (sockaddr*)&serverAddr, AddrLen);
	if (sendByte == 0)
	{
		cout << "client���͵��Ĵλ���ʧ��" << endl;
		return false;
	}
	cout << "client���͵��Ĵλ��֣�"
		<< "SrcPort: " << buffer4.SrcPort << ", "
		<< "DestPort: " << buffer4.DestPort << ", "
		<< "SeqNum: " << buffer4.SeqNum << ", "
		<< "AckNum: " << (buffer4.flag & ACK ? to_string(buffer4.AckNum) : "N/A") << ", "
		<< "Flag: "
		<< "[ACK:" << (buffer4.flag & ACK ? "1" : "0") << "], "
		<< "CheckNum: " << buffer4.checkNum << endl;


	//���Ĵλ���֮����ȴ�2MSL����ֹ���һ��ACK��ʧ
	//��ʱclient����TIME_WAIT״̬
	clock_t tempclock = clock();
	cout << "client������2MSL�ĵȴ�ʱ��" << endl;
	Packet tmp;
	while (clock() - tempclock < 2 * MAX_WAIT_TIME)
	{
		int recvByte = recvfrom(clientSocket, (char*)&tmp, sizeof(tmp), 0, (sockaddr*)&serverAddr, &AddrLen);
		if (recvByte == 0)
		{
			cout << "TIME_WAIT״̬ʱ�յ�������Ϣ���˳�" << endl;
			return false;
		}
		else if (recvByte > 0)
		{
			sendByte = sendto(clientSocket, (char*)&buffer4, sizeof(buffer4), 0, (sockaddr*)&serverAddr, AddrLen);
			cout << "TIME_WAIT״̬ʱ�������һ��ACK��ʧ���ط�" << endl;
		}
	}
	cout << "\nclient�ر����ӳɹ���" << endl;
	return true;
}


int main()
{
	//��ʼ��Winsock����
	WSADATA wsaDataStruct;
	int result = WSAStartup(MAKEWORD(2, 2), &wsaDataStruct);
	if (result != 0) {
		cout << "Winsock�����ʼ��ʧ�ܣ�������룺" << result << endl;
		return -1;
	}
	cout << "Winsock�����ʼ���ɹ�" << endl;

	//����socket����UDP�׽���
	SOCKET clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (clientSocket == INVALID_SOCKET)
	{
		cerr << "�׽��ִ���ʧ�ܣ�������룺" << WSAGetLastError() << endl;
		return -1;
	}

	// �����׽���Ϊ������ģʽ
	unsigned long mode = 1;
	if (ioctlsocket(clientSocket, FIONBIO, &mode) != NO_ERROR)
	{
		cerr << "�޷����׽�������Ϊ������ģʽ��������룺" << WSAGetLastError() << endl;
		closesocket(clientSocket);
		return -1;
	}
	cout << "�׽��ִ����ɹ�, ����Ϊ������ģʽ" << endl;
	cout << endl;

	//��ʼ��·������ַ
	SOCKADDR_IN serverAddr;
	serverAddr.sin_family = AF_INET;
	int result1 = inet_pton(AF_INET, "127.0.0.1", &(serverAddr.sin_addr));
	serverAddr.sin_port = htons(RouterPORT);

	//��ʼ���ͻ��˵�ַ
	SOCKADDR_IN clientAddr;
	clientAddr.sin_family = AF_INET;
	int result2 = inet_pton(AF_INET, "127.0.0.1", &(clientAddr.sin_addr));
	clientAddr.sin_port = htons(ClientPORT);

	//bind
	bind(clientSocket, (LPSOCKADDR)&clientAddr, sizeof(clientAddr));

	//��������
	bool connected = initiateConnection(clientSocket, serverAddr);
	if (!connected) {
		cerr << "�ͻ��˽�������ʧ�ܣ��˳�����" << endl;
		return -1;
	}

	////���ô��ڴ�С
	//cout << "�����봰�ڴ�С��";
	//cin >> windowsize;

	//������Ƶ��Ƿ���һ���ļ��ͻ��˳�
	string filename;
	cout << "������Ҫ���͵��ļ�����" << endl;
	cin >> filename;
	cout << endl;
	sendFile(filename, serverAddr, clientSocket);

	//�Ͽ�����
	cout << "�ͻ���׼���Ͽ�����..." << endl;
	bool breaked = terminateConnection(clientSocket, serverAddr);
	if (!breaked) {
		cerr << "�ͻ��˶Ͽ�����ʧ�ܣ��˳�����" << endl;
		return -1;
	}
	//CloseHandle(mutex);

	closesocket(clientSocket);
	WSACleanup();
	system("pause");
	return 0;
}