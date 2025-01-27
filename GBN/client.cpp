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

#define MAX_WAIT_TIME  1000 //��ʱʱ�ޣ�ms��
#define MAX_SEND_TIMES  6 //����ش�����
#define Max_File_Size 15000000 //����ļ���С
#define Max_Msg_Size 10000 //������ݰ���С

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

struct parameters {
	SOCKET clientSocket;      // �ͻ��˵��׽��֣�����ͨ��
	SOCKADDR_IN serverAddr;   // �������ĵ�ַ��Ϣ
	int nummessage;           // ��Ϣ����
};

int windowsize;

//���г䵱���ͻ�����
queue<Packet> messageBuffer;
//��������
int base = 2;//����ţ����ͷ����ڵ���߽�
int nextseqnum = 2;//��һ�������͵����ݰ����к�

//��ʱ
int timer;

//��������
HANDLE mutex;

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

queue<std::pair<int, int>> duplicateAckQueue;

//����ack���߳�
DWORD WINAPI recvackthread(PVOID useparameter)
{
	mutex = CreateMutex(NULL, FALSE, NULL); // ����������
	parameters* p = (parameters*)useparameter;
	SOCKADDR_IN serverAddr = p->serverAddr;
	SOCKET clientSocket = p->clientSocket;
	int nummessage = p->nummessage;
	int AddrLen = sizeof(serverAddr);

	unsigned long mode = 1;
	ioctlsocket(clientSocket, FIONBIO, &mode);// ���׽�����Ϊ������ģʽ
	int lastAckNum = -1;  // ���ڼ�¼��һ�����յ��� ACK
	int count = 0;

	while (1)
	{
		Packet recvMsg;
		int recvByte = recvfrom(clientSocket, (char*)&recvMsg, sizeof(recvMsg), 0, (sockaddr*)&serverAddr, &AddrLen);
		//�ɹ��յ���Ϣ
		if (recvByte > 0)
		{
			//���У���
			if (recvMsg.check())
			{
				if (recvMsg.AckNum >= base) {
					WaitForSingleObject(mutex, INFINITE);  // �ȴ�����ȡ������
					updateBuffer(recvMsg.AckNum);
					base = recvMsg.AckNum + 1;
					// �����Ѵ����ACK��
					cout << "client�յ�: AckNum = " << recvMsg.AckNum << "��ACK" << endl;
					cout << "[����������յ���Ϣ��] �����ܴ�С=" << windowsize << endl;
					cout << "�ѷ��͵�δ�յ�ȷ�ϵı�������Ϊ" << (nextseqnum - base) << endl;
					if (nextseqnum - base) {
						cout << "�������к�Ϊ��";
					}
					queue<Packet> tempQueue = messageBuffer;  // ����һ����ʱ�������ڱ���
					while (!tempQueue.empty()) {
						Packet pkt = tempQueue.front();
						tempQueue.pop();
						cout << pkt.SeqNum << " ";
					}
					cout << endl << endl;
					ReleaseMutex(mutex);// �ͷŻ�����
				}
				if (base != nextseqnum) {//�����ݰ����ڵȴ�ȷ��,������Ƿ�ʱ
					timer = clock();
				}
				//�жϽ��������
				if (recvMsg.AckNum == nummessage + 1)
				{
					cout << "\n�ļ��������" << endl;
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
			// У��ʧ�ܣ�����Ա��Ĳ������ȴ�
		}
	}
	CloseHandle(mutex); // ��������
	return 0;
}


//ʵ���ļ�����
void sendFile(string filename, SOCKADDR_IN serverAddr, SOCKET clientSocket)
{
	mutex = CreateMutex(NULL, FALSE, NULL); // ����������
	clock_t starttime = clock();
	string realname = filename;
	filename = "D:\\3.1\\���������\\Lab3\\lab3-1\\�����ļ�\\" + filename;
	ifstream fin(filename.c_str(), ifstream::binary);
	if (!fin) {
		printf("�޷����ļ���\n");
		return;
	}

	// ��ȡ�ļ����ݵ�������
	BYTE* fileBuffer = new BYTE[Max_File_Size];
	unsigned int fileSize = 0;
	BYTE byte = fin.get();
	while (fin) {
		fileBuffer[fileSize++] = byte;
		byte = fin.get();
	}
	fin.close();

	int totalPackets = fileSize / Max_Msg_Size;  // �����������ݰ�����
	int remainingBytes = fileSize % Max_Msg_Size;   // ʣ�����ݲ���
	int nummessage = (remainingBytes != 0) ? (totalPackets + 2) : (totalPackets + 1);  // �ܵı�������(?+1)

	// �����ṹ�壬���ڴ��ݸ����� ACK ���߳�
	parameters useparameter;
	useparameter.serverAddr = serverAddr;
	useparameter.clientSocket = clientSocket;
	useparameter.nummessage = nummessage;

	// ����һ���߳̽��� ACK
	HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)recvackthread, &useparameter, 0, 0);

	int count = 0;  // ���ڿ��Ʒ��͵�������

	while (1) {
		// ���ڼ���ظ� ACK ���в������Ϣ
		while (!duplicateAckQueue.empty()) {
			auto ackInfo = duplicateAckQueue.front();
			duplicateAckQueue.pop();
			cout << "[�����ظ���ACK] AckNum = " << ackInfo.first << ", �ظ�����: " << ackInfo.second << endl;
		}

		// �������δ�����������µ����ݿ��Է���
		if (nextseqnum < base + windowsize && nextseqnum < nummessage + 2) {
			Packet datamessage;  // ����һ�����ݱ��Ķ���
			if (nextseqnum == 2) {
				// �����ļ������ļ���С��Ϊ��һ������
				datamessage.SrcPort = ClientPORT;
				datamessage.DestPort = RouterPORT;
				datamessage.length = fileSize;
				datamessage.flag |= FileName;  // ���ñ�־λ����ʾ�����ļ���Ϣ����
				datamessage.SeqNum = nextseqnum;
				// ���ļ����ŵ������ֶ���
				for (int i = 0; i < realname.size(); i++)
					datamessage.data[i] = realname[i];
				datamessage.data[realname.size()] = '\0';
				datamessage.calculateChecksum();  // ����У���
			}
			else if (nextseqnum == totalPackets + 3 && remainingBytes > 0) {
				// ����ʣ�����ݱ���
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
				//���ص����ݱ��ķ���
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
			if (base == nextseqnum)//����������ʱ��
			{
				timer = clock();
			}
			{
			WaitForSingleObject(mutex, INFINITE);  // �ȴ�����ȡ������
			messageBuffer.push(datamessage);
			sendto(clientSocket, (char*)&datamessage, sizeof(datamessage), 0, (sockaddr*)&serverAddr, sizeof(SOCKADDR_IN));
			nextseqnum++;
			cout << "client����: SeqNum = " << datamessage.SeqNum << ", CheckNum = " << datamessage.checkNum <<", Data Length = " <<datamessage.length <<" bytes�����ݱ���" << endl;
			cout << "[���������������Ϣ��] �����ܴ�С=" << windowsize << endl;
			cout << "�ѷ��͵�δ�յ�ȷ�ϵı�������Ϊ" << (nextseqnum - base) << endl;
			if (nextseqnum - base) {
				cout << "�������к�Ϊ��";
			}
			queue<Packet> tempQueue = messageBuffer;  // ����һ����ʱ�������ڱ���
			while (!tempQueue.empty()) {
				Packet pkt = tempQueue.front();
				tempQueue.pop();
				cout << pkt.SeqNum << " ";
			}
			cout << endl << endl;
			ReleaseMutex(mutex);    // �ͷŻ�����
			}
		}
		if (clock() - timer > MAX_WAIT_TIME) {
			WaitForSingleObject(mutex, INFINITE);  // ��ȡ������
			// �ش�����δȷ�ϵı���
			cout << endl;
			if (!messageBuffer.empty()) {
				Packet resendMsg = messageBuffer.front();
				cout << "���� " << resendMsg.SeqNum <<" �ѳ�ʱ" << endl;
			}
			for (int i = 0; i < nextseqnum - base; i++) {
				Packet resendMsg = messageBuffer.front();
				sendto(clientSocket, (char*)&resendMsg, sizeof(resendMsg), 0, (sockaddr*)&serverAddr, sizeof(SOCKADDR_IN));
				cout << "�����ش�: SeqNum = " << resendMsg.SeqNum << "�����ݱ���" << endl;
				messageBuffer.push(resendMsg);  // ���������·������
				messageBuffer.pop();  // �Ӷ������Ƴ�
			}
			ReleaseMutex(mutex);  // �ͷŻ�����
			cout << endl;
			timer = clock();  // ���ü�ʱ��
		}
		//����������˳�
		if (finish == true) {
			break;
		}
	}

	CloseHandle(hThread);
	
	//���㴫��ʱ���������
	clock_t endtime = clock();
	double totalTime = (double)(endtime - starttime) / CLOCKS_PER_SEC;
	double throughput = ((float)fileSize * 8) / totalTime;

	cout << "\n�ܴ���ʱ��Ϊ: " << totalTime << "s" << endl;
	cout << "ƽ��������: " << std::fixed << std::setprecision(2) << throughput << " bit/s" << endl;
	delete[] fileBuffer;//�ͷ��ڴ�
	CloseHandle(mutex); // ��������
	return;
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
	buffer1.SeqNum = ++seq;
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

	//���ô��ڴ�С
	cout << "�����봰�ڴ�С��";
	cin >> windowsize;

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

	closesocket(clientSocket);
	WSACleanup();
	system("pause");
	return 0;
}