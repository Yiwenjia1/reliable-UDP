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

	//������ʵ�ֵķ���
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
	unsigned short* ptr = (unsigned short*)this;// ÿ�δ��������ֽڣ�16λ��
	// ���� Packet ��������� 16 λ���ݣ�Packet �����С���� 2��ÿ��ѭ�����������ֽڣ�
	for (int i = 0; i < sizeof(*this) / 2; i++)
	{
		sum += *ptr++;
		// ��� sum �ĸ�16λ�н�λ���򽫸�16λ�������� 1
		if (sum & 0xFFFF0000)
		{
			sum &= 0xFFFF;
			sum++;
		}
	}
	// ��� sum �ĵ�16λΪ 0xFFFF�����ʾУ�����ȷ
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

//ʵ��client����������
bool initiateConnection(SOCKET clientSocket, SOCKADDR_IN serverAddr)
{
	int AddrLen = sizeof(serverAddr);
	Packet buffer1, buffer2, buffer3;

	//���͵�һ�����ֵ���Ϣ��SYN��Ч)
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

		//���client���͵�һ�����ֳ�ʱ�����·��Ͳ����¼�ʱ
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

	//���͵��������ֵ���Ϣ��ACK��Ч��
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
		<<"[ACK:" << (buffer3.flag & ACK ? "1" : "0") << "], "
		<< "CheckNum: " << buffer3.checkNum << endl;
	cout << "client���ӳɹ���" << endl;
	cout << endl;
	return true;
}

//ʵ�ֵ������ķ���
bool sendPacket(Packet& sendMsg, SOCKET clientSocket, SOCKADDR_IN serverAddr, bool FileFlag = false)
{
	sendto(clientSocket, (char*)&sendMsg, sizeof(sendMsg), 0, (sockaddr*)&serverAddr, sizeof(SOCKADDR_IN));
	if (FileFlag)
	{
		cout << "client���ͣ�"
			<< "SeqNum: " << sendMsg.SeqNum << ", "
			<< "CheckNum: " << sendMsg.checkNum << ", "
			<< "Flag: "
			<<"[FileName:" << (sendMsg.flag & FileName? "1":"0")<< "], "
			<< "Data Length: " << sendMsg.length << " bytes" << endl;
	}
	else
	{
		cout << "client���ͣ�"
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
			// ����յ���ȷ��ACK������֤У��ͺ�ACK��
			if ((recvMsg.flag & ACK) && (recvMsg.AckNum == sendMsg.SeqNum))
			{
				cout << "client�յ���AckNum = " << recvMsg.AckNum << " ��ACK";
				return true;
			}
		}
		// �����ʱ�������·���
		if (clock() - msgStart > MAX_WAIT_TIME)
		{
			cout << "[WARN]" << "seq = " << sendMsg.SeqNum << "�ı��ģ���" << ++resendCount << "�γ�ʱ�������ش�......" << endl;
			int sendByte = sendto(clientSocket, (char*)&sendMsg, sizeof(sendMsg), 0, (sockaddr*)&serverAddr, sizeof(SOCKADDR_IN));
			msgStart = clock();
			if (sendByte > 0) {
				cout << "�����ش��ɹ�" << endl;
				break;
			}
			else {
				cout << "�����ش�ʧ��" << endl;
			}
		}
		if (resendCount == MAX_SEND_TIMES)
		{
			cout << "��ʱ�ش��ѵ�������������ʧ��" << endl;
			return false;
		}
	}
	return true;
}

//ʵ���ļ�����
void sendFile(string filename, SOCKADDR_IN serverAddr, SOCKET clientSocket)
{

	clock_t starttime = clock();
	string realname = filename;
	filename ="D:\\3.1\\���������\\Lab3\\lab3-1\\�����ļ�\\"+ filename;
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

	// �����ļ������ļ���С�ı��ģ�����FileName��־λ
	Packet nameMessage;
	nameMessage.SrcPort = ClientPORT;
	nameMessage.DestPort = RouterPORT;
	nameMessage.length = fileSize;
	nameMessage.flag |= FileName;
	nameMessage.SeqNum = ++seq;

	// �����ļ������������򣬲���ӽ�����
	for (int i = 0; i < realname.size(); i++)
		nameMessage.data[i] = realname[i];
	nameMessage.data[realname.size()] = '\0';
	nameMessage.calculateChecksum();
	if (!sendPacket(nameMessage, clientSocket, serverAddr,true))
	{
		cout << "�����ļ����ʹ�С�ı���ʧ��" << endl;
		return;
	}
	cout << endl;
	cout << "�ļ����ʹ�С���ķ��ͳɹ�" << endl << endl;

	int totalPackets = fileSize / Max_Msg_Size;// �����������ݰ�����
	int remainingBytes = fileSize % Max_Msg_Size;// ʣ�����ݲ���
	
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
			cout << "��" << i + 1 << "�����ݱ��ķ���ʧ��" << endl;
			return;
		}
		cout << endl;
		cout << "��" << i + 1 << "�����ݱ��ķ��ͳɹ�" << endl << endl;
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
			cout << "��" << totalPackets + 1 << "�����ݱ��ķ���ʧ��" << endl;
			return;
		}
		cout << endl;
		cout << "��" << totalPackets + 1 << "�����ݱ��ķ��ͳɹ�" << endl << endl;
	}

	//���㴫��ʱ���������
	clock_t endtime = clock();
	double totalTime = (double)(endtime - starttime) / CLOCKS_PER_SEC;
	double throughput = ((float)fileSize * 8) / totalTime;

	cout << "\n�ܴ���ʱ��Ϊ: " << totalTime << "s" << endl;
	cout << "ƽ��������: "<<std::fixed << std::setprecision(2) << throughput << " bit/s" << endl;
	delete[] fileBuffer;//�ͷ��ڴ�
	return;
}

//ʵ��client���Ĵλ���
bool  terminateConnection(SOCKET clientSocket, SOCKADDR_IN serverAddr)
{
	int AddrLen = sizeof(serverAddr);
	Packet buffer1,buffer2,buffer3,buffer4;

	//���͵�һ�λ��֣�FIN��ACK��Ч��
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
		<<"[ACK:" << (buffer1.flag & ACK ? "1" : "0")
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
		//���client���͵�һ�λ��ֳ�ʱ�����·��Ͳ����¼�ʱ
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

	//���͵��Ĵλ��ֵ���Ϣ��ACK��Ч��
	buffer4.SrcPort = ClientPORT;
	buffer4.DestPort = RouterPORT;
	buffer4.flag |= ACK;
	buffer4.SeqNum = ++seq;
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
		<<"[ACK:" << (buffer4.flag & ACK ? "1" : "0") << "], "
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

	//����UDP�׽���
	SOCKET clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (clientSocket == INVALID_SOCKET)
	{
		cerr << "�׽��ִ���ʧ�ܣ�������룺" << WSAGetLastError() << endl;
		return -1;
	}

	// �����׽���Ϊ������ģʽ,���ڼ�鳬ʱ
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

	//���׽���
	bind(clientSocket, (LPSOCKADDR)&clientAddr, sizeof(clientAddr));

	//��������
	bool connected = initiateConnection(clientSocket, serverAddr);
	if (!connected) {
		cerr << "�ͻ��˽�������ʧ�ܣ��˳�����" << endl;
		return -1;
	}

	//�����ļ�
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

	// �����׽���
	closesocket(clientSocket);
	WSACleanup();
	system("pause");
	return 0;
}