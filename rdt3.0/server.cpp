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

//ʵ��server����������
bool initiateConnection(SOCKET serverSocket, SOCKADDR_IN clientAddr)
{
	int AddrLen = sizeof(clientAddr);
	Packet buffer1, buffer2, buffer3;
	int resendCount = 0;

	while (true)
	{
		//���յ�һ�����ֵ���Ϣ
		int recvByte = recvfrom(serverSocket, (char*)&buffer1, sizeof(buffer1), 0, (sockaddr*)&clientAddr, &AddrLen);
		if (recvByte > 0)
		{
			//�ɹ��յ���Ϣ�����SYN�������
			if (!(buffer1.flag & SYN) || !buffer1.check())
			{
				cout << "server���յ�һ�����ֳɹ���У��ʹ���" << endl;
				return false;
			}
			cout << "server���յ�һ�����ֳɹ�" << endl;

			// ׼�������͵ڶ���������Ӧ����SYN, ACK��־����
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
				cout << "server���͵ڶ�������ʧ��" << endl;
				return false;
			}
			cout << "server���͵ڶ������֣�"
				<< "SrcPort: " << buffer2.SrcPort << ", "
				<< "DestPort: " << buffer2.DestPort << ", "
				<< "SeqNum: " << buffer2.SeqNum << ", "
				<< "AckNum: " << (buffer2.flag & ACK ? to_string(buffer2.AckNum) : "N/A") << ", "
				<< "Flag: " << "[SYN: " << (buffer2.flag & SYN ? "1" : "0")
				<< "] [ACK: " << (buffer2.flag & ACK ? "1" : "0") << "], "
				<< "CheckNum: " << buffer2.checkNum << endl;


			//���յ��������ֵ���Ϣ
			while (true)
			{
				int recvByte = recvfrom(serverSocket, (char*)&buffer3, sizeof(buffer3), 0, (sockaddr*)&clientAddr, &AddrLen);
				if (recvByte > 0)
				{
					//�ɹ��յ���Ϣ�����ACK��У��͡�ack
					if ((buffer3.flag & ACK) && buffer3.check() && (buffer3.AckNum == buffer2.SeqNum + 1))
					{
						seq++;
						cout << "server���յ��������ֳɹ�" << endl;
						cout << "server���ӳɹ���" << endl;
						return true;
					}
					else
					{
						cout << "server���յ��������ֳɹ���У��ʧ��" << endl;
						return false;
					}
				}

				// ����ڶ������ֳ�ʱ�������ش�
				if (clock() - start_time > MAX_WAIT_TIME)
				{
					if (++resendCount > MAX_SEND_TIMES) {
						cout << "server���͵ڶ������ֳ�ʱ�ش��ѵ�������������ʧ��" << endl;
						return false;
					}
					cout << "server���͵ڶ������֣���" << resendCount << "�γ�ʱ�������ش�......" << endl;
					int sendByte = sendto(serverSocket, (char*)&buffer2, sizeof(buffer2), 0, (sockaddr*)&clientAddr, AddrLen);
					if (sendByte > 0) {
						cout << "server���͵ڶ��������ش��ɹ�" << endl;
					}
					else {
						cout << "server�ڶ��������ش�ʧ��" << endl;
					}
					start_time = clock();  // ���¿�ʼ��ʱ��ʱ
				}
			}
		}
	}
	return false;
}

// ʵ�ֵ������ݰ��Ľ�����ȷ��
bool receivePacket(Packet& recvMsg, SOCKET serverSocket, SOCKADDR_IN clientAddr)
{
	int AddrLen = sizeof(clientAddr);
	while (1)
	{
		int recvByte = recvfrom(serverSocket, (char*)&recvMsg, sizeof(recvMsg), 0, (sockaddr*)&clientAddr, &AddrLen);//����
		if (recvByte > 0)
		{
			//����Ƿ��յ������ݰ���Ч�������кŷ���Ԥ��
			if (recvMsg.check() && (recvMsg.SeqNum == seq + 1))
			{
				//��������������ݰ�����������ACK
				Packet replyMessage;
				replyMessage.SrcPort = ServerPORT;
				replyMessage.DestPort = RouterPORT;
				replyMessage.flag |= ACK;
				replyMessage.SeqNum = seq++;
				replyMessage.AckNum = recvMsg.SeqNum;
				replyMessage.calculateChecksum();
				sendto(serverSocket, (char*)&replyMessage, sizeof(replyMessage), 0, (sockaddr*)&clientAddr, sizeof(SOCKADDR_IN));//����
				cout << "server�յ���SeqNum = " << recvMsg.SeqNum <<", CheckNum = "<<recvMsg.checkNum << ", Data Length ="<<recvMsg.length <<" bytes �����ݱ���" << endl;
				cout << "server���ͣ�SeqNum = " << replyMessage.SeqNum << "��AckNum = " << replyMessage.AckNum << " ��ACK����" << endl;
				return true;
			}

			//������ظ������ݰ������кŲ�����Ԥ�ڣ������������ݰ���ֻ����ACK
			else if (recvMsg.check() && (recvMsg.SeqNum != seq + 1))
			{
				Packet replyMessage;
				replyMessage.SrcPort = ServerPORT;
				replyMessage.DestPort = RouterPORT;
				replyMessage.flag |= ACK;
				replyMessage.SeqNum = seq;
				replyMessage.AckNum = recvMsg.SeqNum;
				replyMessage.calculateChecksum();
				sendto(serverSocket, (char*)&replyMessage, sizeof(replyMessage), 0, (sockaddr*)&clientAddr, sizeof(SOCKADDR_IN));
				cout << "[�ظ����ձ���] server�յ� SeqNum = " << recvMsg.SeqNum << " �����ݱ��ģ������� AckNum = " << replyMessage.AckNum << " ��ACK����" << endl;
			}
		}
		else if (recvByte == 0)
		{
			return false;
		}
	}
	return true;
}

//ʵ���ļ�����
void receiveFile(SOCKET serverSocket, SOCKADDR_IN clientAddr)
{
	int AddrLen = sizeof(clientAddr);
	Packet nameMessage;
	unsigned int fileSize;
	char fileName[50] = { 0 };

	while (1)
	{
		int recvByte = recvfrom(serverSocket, (char*)&nameMessage, sizeof(nameMessage), 0, (sockaddr*)&clientAddr, &AddrLen);
		if (recvByte > 0)
		{
			// ����ɹ����յ��ļ������ļ���С����Ϣ��������ACKȷ�ϲ�����
			if (nameMessage.check() && (nameMessage.SeqNum ==seq + 1) && (nameMessage.flag & FileName))
			{
				// ��ȡ�ļ������ļ���С
				fileSize = nameMessage.length;
				for (int i = 0; nameMessage.data[i]; i++)
					fileName[i] = nameMessage.data[i];
				cout << "\n�����ļ�����" << fileName << "���ļ���С��" << fileSize <<"�ֽ�" << endl << endl;
				//���� ACK ȷ�ϱ���
				Packet replyMessage;
				replyMessage.SrcPort = ServerPORT;
				replyMessage.DestPort = RouterPORT;
				replyMessage.flag |= ACK;
				replyMessage.SeqNum = seq++;
				replyMessage.AckNum = nameMessage.SeqNum;
				replyMessage.calculateChecksum();
				sendto(serverSocket, (char*)&replyMessage, sizeof(replyMessage), 0, (sockaddr*)&clientAddr, sizeof(SOCKADDR_IN));
				cout << "server�յ���SeqNum = " << nameMessage.SeqNum << ", CheckNum = "<< nameMessage.checkNum<< ", Data Length =" << nameMessage.length << " �����ݱ���" << endl;
				cout << "server���ͣ�SeqNum = " << replyMessage.SeqNum << ", AckNum = " << replyMessage.AckNum << " ��ACK����" << endl;
				cout << "�ļ������ļ���С��Ϣ���ճɹ�����ʼ�����ļ�����" << endl << endl;
				break;
			}

			// ������ظ����յ����ݰ������·���ACK
			else if (nameMessage.check() && (nameMessage.SeqNum != seq + 1) && (nameMessage.flag & FileName))
			{
				Packet replyMessage;
				replyMessage.SrcPort = ServerPORT;
				replyMessage.DestPort = RouterPORT;
				replyMessage.flag |= ACK;
				replyMessage.SeqNum =seq++;
				replyMessage.AckNum = nameMessage.SeqNum;
				replyMessage.calculateChecksum();
				sendto(serverSocket, (char*)&replyMessage, sizeof(replyMessage), 0, (sockaddr*)&clientAddr, sizeof(SOCKADDR_IN));
				cout << "[�ظ����ձ��Ķ�]server�յ� SeqNum = " << nameMessage.SeqNum << " �����ݱ��ģ������� AckNum = " << replyMessage.AckNum << " ��ACK����" << endl;
			}
		}
	}


	// �����ļ���С���ļ���ֳɶ�����ݰ�
	int totalPackets = fileSize / Max_Msg_Size; // �����������ݰ�����
	int remainingBytes = fileSize % Max_Msg_Size; // ʣ�����ݲ���
	BYTE* fileBuffer = new BYTE[fileSize];

	// �������������ݰ����洢���ļ�������
	for (int i = 0; i < totalPackets; i++)
	{
		Packet dataMsg;
		if (receivePacket(dataMsg, serverSocket, clientAddr))
		{
			cout << "���յ��� " << i + 1 << " �����ݱ���" << endl << endl;
		}
		else
		{
			cout << "���յ� " << i + 1 << " �����ݱ���ʧ��" << endl << endl;
			return;
		}

		// �����ݴ����ļ�����
		for (int j = 0; j < Max_Msg_Size; j++)
		{
			fileBuffer[i * Max_Msg_Size + j] = dataMsg.data[j];
		}
	}

	// ����ʣ������ݰ�
	if (remainingBytes > 0)
	{
		Packet dataMsg;
		if (receivePacket(dataMsg, serverSocket, clientAddr))
		{
			cout << "���յ��� " << totalPackets + 1 << " �����ݱ���" << endl << endl;
		}
		else
		{
			cout << "���յ� " << totalPackets + 1 << " �����ݱ���ʧ��" << endl << endl;
			return;
		}

		// ��ʣ�����ݴ����ļ�����
		for (int j = 0; j < remainingBytes; j++)
		{
			fileBuffer[totalPackets * Max_Msg_Size + j] = dataMsg.data[j];
		}
	}

	//д���ļ�
	cout << "\n�ļ�����ɹ�����ʼд���ļ�" << endl;
	FILE* outputfile;
	errno_t err = fopen_s(&outputfile, fileName, "wb");
	if (fileBuffer != 0)
	{
		fwrite(fileBuffer, fileSize, 1, outputfile);
		fclose(outputfile);
	}
	cout << "�ļ�д��ɹ�" << endl << endl;
	delete[] fileBuffer;//�ͷ��ڴ�
}

//ʵ��server���Ĵλ���
bool terminateConnection(SOCKET serverSocket, SOCKADDR_IN clientAddr)
{
	int AddrLen = sizeof(clientAddr);
	Packet buffer1,buffer2,buffer3,buffer4;

	while (1)
	{
		// �����һ�λ��֣��ͻ��˷���FIN����
		int recvByte = recvfrom(serverSocket, (char*)&buffer1, sizeof(buffer1), 0, (sockaddr*)&clientAddr, &AddrLen);
		if (recvByte == 0)
		{
			cout << "��һ�λ��ֽ���ʧ�ܣ��˳�" << endl;
			return false;
		}

		else if (recvByte > 0)
		{
			//���FIN��ACK�������
			if (!(buffer1.flag && FIN) || !(buffer1.flag && ACK) || !buffer1.check())
			{
				cout << "��һ�λ��ֽ��ճɹ�������֤ʧ��" << endl;
				return false;
			}
			cout << "server���յ�һ�λ��ֳɹ�" << endl;

			// ���͵ڶ��λ��֣���������ӦACKȷ��
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
				cout << "server���͵ڶ��λ���ʧ��" << endl;
				return false;
			}
			cout << "server���͵ڶ��λ��֣�"
				<< "SrcPort: " << buffer2.SrcPort << ", "
				<< "DestPort: " << buffer2.DestPort << ", "
				<< "SeqNum: " << buffer2.SeqNum << ", "
				<< "AckNum: " << (buffer2.flag & ACK ? to_string(buffer2.AckNum) : "N/A") << ", "
				<< "Flag: " 
				<<"[ACK:" << (buffer2.flag & ACK ? "1" : "0") << "], "
				<< "CheckNum: " << buffer2.checkNum << endl;
			break;
		}
	}
	// ���͵����λ��֣�����������FIN����
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
		cout << "server���͵����λ���ʧ��" << endl;
		return false;
	}
	cout << "server���͵����λ��֣�"
		<< "SrcPort: " << buffer3.SrcPort << ", "
		<< "DestPort: " << buffer3.DestPort << ", "
		<< "SeqNum: " << buffer3.SeqNum << ", "
		<< "AckNum: " << (buffer3.flag & ACK ? to_string(buffer3.AckNum) : "N/A") << ", "
		<< "Flag: " 
		<<"[ACK:" << (buffer3.flag & ACK ? "1" : "0")
		<< "] [FIN: " << (buffer3.flag & FIN ? "1" : "0") << "], "
		<< "CheckNum : " << buffer3.checkNum << endl;
	int resendCount = 0;

	// ���յ��Ĵλ��֣��ͻ��˻�ӦACKȷ��
	while (1)
	{
		int recvByte = recvfrom(serverSocket, (char*)&buffer4, sizeof(buffer4), 0, (sockaddr*)&clientAddr, &AddrLen);
		if (recvByte == 0)
		{
			cout << "server���յ��Ĵλ���ʧ��" << endl;
			return false;
		}
		else if (recvByte > 0)
		{
			//�ɹ��յ���Ϣ�����У��͡�ACK��ack
			if ((buffer4.flag && ACK) && buffer4.check() && (buffer4.AckNum == buffer3.SeqNum + 1))
			{
				cout << "server���յ��Ĵλ��ֳɹ�" << endl;
				break;
			}
			else
			{
				cout << "server���յ��Ĵλ��ֳɹ������ʧ��" << endl;
				return false;
			}
		}

		// ��������λ��ֳ�ʱ��������ش�
		if (clock() - t3 > MAX_WAIT_TIME)
		{
			cout << "server���͵����λ��֣���" << ++resendCount << "�γ�ʱ�������ش�......" << endl;
			int sendByte = sendto(serverSocket, (char*)&buffer3, sizeof(buffer3), 0, (sockaddr*)&clientAddr, AddrLen);
			t3 = clock();  // ���³�ʱ��ʱ
			if (sendByte > 0) {
				cout << "server���͵����λ����ش��ɹ�" << endl;
			}
			else {
				cout << "server�����λ����ش�ʧ��" << endl;
			}
			if (++resendCount > MAX_SEND_TIMES) {
				cout << "server�����λ��ֳ�ʱ�ش��ѵ�������������ʧ��" << endl;
				return false;
			}
		}
	}
	cout << "\nserver�ر����ӳɹ���" << endl;
	return true;
}

int main()
{
	//��ʼ��Winsock
	WSADATA wsaDataStruct;
	int result = WSAStartup(MAKEWORD(2, 2), &wsaDataStruct);
	if (result != 0) {
		cout << "Winsock�����ʼ��ʧ�ܣ�������룺" << result << endl;
		return -1;
	}
	cout << "Winsock�����ʼ���ɹ�" << endl;

	//����UDP�׽���
	SOCKET serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (serverSocket == INVALID_SOCKET)
	{
		cerr << "�׽��ִ���ʧ�ܣ�������룺" << WSAGetLastError() << endl;
		return -1;
	}

	// �����׽���Ϊ������ģʽ
	unsigned long mode = 1;
	if (ioctlsocket(serverSocket, FIONBIO, &mode) != NO_ERROR)
	{
		cerr << "�޷����׽�������Ϊ������ģʽ��������룺" << WSAGetLastError() << endl;
		closesocket(serverSocket);
		return -1;
	}
	cout << "�׽��ִ����ɹ�, ����Ϊ������ģʽ" << endl;

	//��ʼ����������ַ
	SOCKADDR_IN serverAddr;
	serverAddr.sin_family = AF_INET;
	int result3 = inet_pton(AF_INET, "127.0.0.1", &(serverAddr.sin_addr));
	serverAddr.sin_port = htons(ServerPORT);

	//���׽���
	int tem = bind(serverSocket, (LPSOCKADDR)&serverAddr, sizeof(serverAddr));
	if (tem == SOCKET_ERROR)
	{
		cout << "�׽��ְ�ʧ�ܣ�������룺" << WSAGetLastError() << endl;
		return -1;
	}
	cout << "�׽��ְ󶨳ɹ���������׼����������" << endl << endl;

	//��ʼ��·������ַ
	SOCKADDR_IN clientAddr;
	clientAddr.sin_family = AF_INET;
	int result4 = inet_pton(AF_INET, "127.0.0.1", &(clientAddr.sin_addr));
	clientAddr.sin_port = htons(RouterPORT);

	//��������
	bool isConn = initiateConnection(serverSocket, clientAddr);
	if (isConn == 0) {
		cerr << "��������ͻ��˵����ӽ���ʧ�ܣ��˳�����" << endl;
		return -1;
	}

	//�����ļ�
	cout << endl;
	cout << "׼�������ļ�..." << endl;
	receiveFile(serverSocket, clientAddr);

	//�ر�����
	cout << "������׼���Ͽ�����..." << endl;
	bool breaked = terminateConnection(serverSocket, clientAddr);
	if (!breaked) {
		cerr << "�������Ͽ�����ʧ�ܣ��˳�����" << endl;
		return -1;
	}

	// �����׽���
	closesocket(serverSocket);
	WSACleanup();
	system("pause");
	return 0;
}