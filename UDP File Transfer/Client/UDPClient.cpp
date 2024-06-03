#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <vector>
#include <random>

/*OPERATION CODES*/
short AUTH_OP_CODE = 1;
short RRQ_OP_CODE = 2;
short WRQ_OP_CODE = 3;
short DATA_OP_CODE = 4;
short ACK_OP_CODE = 5;
short ERROR_OP_CODE = 6;

/*Application Varaiables*/
std::string Username = "";
std::string Password = "";
std::string RequestType = "";
std::string FileName = "";
uint16_t sessionNumber = 0;

/*UDP Connection Variables*/
std::string serverIP_Adress = "";
int serverPortNumber;
char buffer[1024];
int clientSocketFD;
struct sockaddr_in serverAddress;
struct sockaddr_in clientAddress;
struct timeval timeout;

/*The Packet struct*/
struct UDP_Packet
{
  short operationCode;
  uint16_t sessionNumber;

  char username[33];
  char password[33];

  char errorMessage[512];

  short blockNumber;
  short segementNumber;

  char fileName[255];
  char fileData[1024];
  int fileSize;
};
/*Generate random int from a to b inclusive*/
int randomRange(int Start,int End)
{
  std::random_device randomDevice; 
  std::mt19937 generatedSeed(randomDevice());
  std::uniform_int_distribution<> distrbution(Start, End); 

  return distrbution(generatedSeed);
}

void terminateProgram()
{
  printf("\n[EXITING]\n");
  exit(0);
}

void sendErrorPacket(std::string ErrorMessage, struct sockaddr_in targetAddress)
{
  UDP_Packet errorpacket;

  errorpacket.operationCode = ERROR_OP_CODE;
  strcpy(errorpacket.errorMessage, ErrorMessage.c_str());

  printf("[Sending Error Message to Sever due to %s error]...\n", ErrorMessage.c_str());
 
  char errorPayload[sizeof(errorpacket)];
  bzero(errorPayload, sizeof(errorpacket));
  memcpy(errorPayload, &errorpacket, sizeof(errorpacket));

  sendto(clientSocketFD, errorPayload, sizeof(errorPayload), 0, (struct sockaddr*)&targetAddress, sizeof(targetAddress));
}

/*The initailization and binding of the client socket*/
void createAndBindSocket()
{
  clientSocketFD = socket(AF_INET, SOCK_DGRAM, 0);

  // Set a timeout of 5 seconds for receiving data
  timeout.tv_sec = 5;
  timeout.tv_usec = 0;
  setsockopt(clientSocketFD, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  
  if (clientSocketFD < 0)
  {
    perror("[Socket Error]");
    exit(1);
  }

  //Generating random port number 
  int randomPort = randomRange(1030,60000);

  memset(&clientAddress, '\0', sizeof(clientAddress));
  clientAddress.sin_family = AF_INET;
  clientAddress.sin_port = htons(randomPort);
  clientAddress.sin_addr.s_addr =  htonl(INADDR_ANY);

  //Binding the sever socket
  int bindStatus;
  bindStatus = bind(clientSocketFD, (struct sockaddr*)&clientAddress, sizeof(clientAddress));
  if (bindStatus < 0) 
  {
    perror("[Binding Error]");
    exit(1);
  }
}

void createServerConectionSocket()
{
  clientSocketFD = socket(AF_INET, SOCK_DGRAM, 0);
  memset(&serverAddress, '\0', sizeof(serverAddress));
  serverAddress.sin_family = AF_INET;
  serverAddress.sin_port = htons(serverPortNumber);
  serverAddress.sin_addr.s_addr = inet_addr(serverIP_Adress.c_str());
}

void waitForAcknowledgementMessage()
{
  printf("[Waiting for Acknowledgement from the server]...\n");
  socklen_t addressSize;

  //Set up For reciving packet
  UDP_Packet currentPacket;
  char payload[sizeof(currentPacket)];
  bzero(payload, sizeof(currentPacket));
  addressSize = sizeof(serverAddress);

  //Reciving packet
  recvfrom(clientSocketFD, payload, sizeof(payload), 0, (struct sockaddr*)&serverAddress, &addressSize);
  memcpy(&currentPacket, payload, sizeof(currentPacket));

  //Checking if we have a error Code
  if(currentPacket.operationCode == ERROR_OP_CODE)
  {
    printf("[Error %s]\n",currentPacket.errorMessage);
    terminateProgram();
  }

  //Checking if we have an Acknowledgement
  if(currentPacket.operationCode == ACK_OP_CODE)
  {
    sessionNumber = currentPacket.sessionNumber;
    printf("[Acknowledgement Recived Session ID: %i]\n",sessionNumber);
  }
  else
  {
    printf("[Error Acknowledgement not recived ]\n");
    terminateProgram();
  }
}

/*Download from server Section*/
void downloadFile()
{
  printf("[Waiting for File]....\n");

  std::string fileName = "./" + FileName;

  socklen_t addressSize;
  int size = 1024;
  FILE *fileCopy;
  
  fileCopy = fopen(fileName.c_str(), "w");

  socklen_t addr_size;

  // Receiving the data and writing it into the file.
  while (1)
  {
    //Set up For reciving packet
    UDP_Packet currentPacket;
    char payload[sizeof(currentPacket)];
    bzero(payload, sizeof(currentPacket));
    addressSize = sizeof(serverAddress);

    //Reciving packet
    recvfrom(clientSocketFD, payload, sizeof(payload), 0, (struct sockaddr*)&serverAddress, &addressSize);
    memcpy(&currentPacket, payload, sizeof(currentPacket));

    //Checking for an Error
    if(currentPacket.operationCode == ERROR_OP_CODE)
    {
      printf("[%s]",currentPacket.errorMessage);
      terminateProgram();
    }
    
    return;

    /*Sending Acknowledgement of File*/ 
    UDP_Packet ackpacket;
    ackpacket.operationCode = ACK_OP_CODE;
    ackpacket.segementNumber = currentPacket.segementNumber;
    char authPayload[sizeof(ackpacket)];
    bzero(authPayload, sizeof(ackpacket));
    memcpy(authPayload, &ackpacket, sizeof(ackpacket));

    /*Sending the packet*/
    sendto(clientSocketFD, authPayload, sizeof(authPayload), 0, (struct sockaddr*)&serverAddress, sizeof(serverAddress));

    if (strcmp(currentPacket.fileData, "END") == 0)
    {
      break;
    }

    //Writing to file 
    fprintf(fileCopy, "%s", currentPacket.fileData);
  }

  
  fclose(fileCopy);
}

void sendReadRequest()
{
  printf("[Sending Read Request to the Server]...\n");

  //Setting up Packet
  short oppCode = RRQ_OP_CODE;
  UDP_Packet readReqPacket;
  readReqPacket.operationCode = oppCode;
  readReqPacket.sessionNumber = sessionNumber;
  strcpy(readReqPacket.fileName, FileName.c_str());

  //Setting up the payload to be sent 
  char readReqPayload[sizeof(readReqPacket)];
  bzero(readReqPayload, sizeof(readReqPacket));
  memcpy(readReqPayload, &readReqPacket, sizeof(readReqPacket));

  //Sending the packet
  sendto(clientSocketFD, readReqPayload, sizeof(readReqPayload), 0, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
  
  downloadFile();
}




/*Upload to server Section*/
bool downloadRequestAcknowledged()
{
  printf("[Waiting for Download Acknowledgement packet]....\n");
  
  //Setting up Packet
  UDP_Packet ackPacket;
  char ackPayload[sizeof(ackPacket)];
  bzero(ackPayload, sizeof(ackPacket));
  
  //Waiting for request
  socklen_t addressSize;
  recvfrom(clientSocketFD, ackPayload, sizeof(ackPayload), 0, (struct sockaddr*)&serverAddress, &addressSize);
  memcpy(&ackPacket, ackPayload, sizeof(ackPacket));


  //Checking if we recived an acknowledgemnt
  if(ackPacket.operationCode == ACK_OP_CODE)
  {
    return true;
  }
  else
  {
    return false;
  }
}
/* returns true if the client has recived a file */
/* returns true if the client has recived a file */
bool fileRecived(int segmentNumber)
{
  printf("[Waiting for File Acknowledgement packet]....\n");
  
  //Setting up Packet
  UDP_Packet ackPacket;
  char ackPayload[sizeof(ackPacket)];
  bzero(ackPayload, sizeof(ackPacket));
  
  //Waiting for request
  socklen_t addressSize;
  addressSize = sizeof(serverAddress);
  recvfrom(clientSocketFD, ackPayload, sizeof(ackPayload), 0, (struct sockaddr*)&clientAddress, &addressSize);

  //Offloading the incoming packet
  memcpy(&ackPacket, ackPayload, sizeof(ackPacket));
  
  //Checking what type of request packet
  if(ackPacket.operationCode == ERROR_OP_CODE )
  {
    printf("[%s]\n",ackPacket.errorMessage);
    return false;
  }
  else if(ackPacket.operationCode ==  ACK_OP_CODE)
  {
    if(ackPacket.segementNumber == segmentNumber)
    {
      return true;
    }
    else
    {
      return false;
    }
  }
  else
  {
    printf("[Error Invalid opp code]\n");
    //Sending Error packet
    sendErrorPacket("Error Invalid opp code",clientAddress);
    return false;
  }
}

void uploadFile()
{
  printf("[Uploading File]...\n");

  //opening file 
  std:: string fileDirectory = "./" + FileName;

  FILE *file = fopen(fileDirectory.c_str(), "r");

  if(file == NULL)
  {
    printf("[Error File %s not found]\n",FileName.c_str());
    sendErrorPacket("Error File not found",serverAddress);
    return;
  }
  
  //initalizing packet 
  UDP_Packet filePacket;
  filePacket.operationCode = WRQ_OP_CODE;
  filePacket.segementNumber = 0;
  

  int size = 1024;
  int sendResult;
  char readbuffer[size];
  
  char filePayload[sizeof(filePacket)];
  //Set the seek to the begining
  // Sending the data
  while (fgets(readbuffer, size, file) != NULL)
  {
    filePacket.segementNumber ++;
    //Writing 
    strcpy(filePacket.fileData,readbuffer);

    //Sending file Packet
    bzero(filePayload, sizeof(filePacket));
    memcpy(filePayload, &filePacket, sizeof(filePacket));

    //Sending packket and Waiting for acknowledgement
    while (1)
    {
      //Send Packet
      sendResult =  sendto(clientSocketFD, filePayload, sizeof(filePayload), 0, (struct sockaddr*)&serverAddress, sizeof(serverAddress));

      //Wait for Acknowledgement 
      if(fileRecived(filePacket.segementNumber))
      {
        break;
      }
      printf("[Resending packet]\n");
    }

    //Error checking 
    if (sendResult == -1)
    {
      perror("[ERROR] sending data to the Client.");
      exit(1);
    }

    //Zeroing  out the buffer 
    bzero(filePacket.fileData, size);
    bzero(readbuffer, size);
  }

  filePacket.segementNumber ++;
  strcpy(filePacket.fileData, "END");
  
  //Sending file Packet
  filePayload[sizeof(filePacket)];
  bzero(filePayload, sizeof(filePacket));
  memcpy(filePayload, &filePacket, sizeof(filePacket));

  
  printf("sending end [%s]\n",filePacket.fileData);

  sendResult =  sendto(clientSocketFD, filePayload, sizeof(filePayload), 0, (struct sockaddr*)&serverAddress, sizeof(serverAddress));

  if (sendResult == -1)
  {
    perror("[ERROR] sending data to the server.");
    exit(1);
  }

  fclose(file);
}

void sendDownloadRequest()
{
  printf("[Sending Download Request to the Server]...\n");

  //Setting up Packet
  short oppCode = WRQ_OP_CODE;
  UDP_Packet writeReqPacket;
  writeReqPacket.operationCode = oppCode;
  writeReqPacket.sessionNumber = sessionNumber;
  strcpy(writeReqPacket.fileName, FileName.c_str());

  //Setting up the payload to be sent 
  char writeReqPayload[sizeof(writeReqPacket)];
  bzero(writeReqPayload, sizeof(writeReqPacket));
  memcpy(writeReqPayload, &writeReqPacket, sizeof(writeReqPacket));

  //Sending the packet
  sendto(clientSocketFD, writeReqPayload, sizeof(writeReqPayload), 0, (struct sockaddr*)&serverAddress, sizeof(serverAddress));

  //Wait for download acknowledgement
  if(downloadRequestAcknowledged())
  {
    printf("[Download Request Acknowledged]\n");
    uploadFile();
  }
  else
  {
    printf("[Error Download Request not Acknowledged]....\n");
    terminateProgram();
  }
}




void requestPhase()
{
  //Determine if it's a Read Request or a write Request
  if(RequestType.compare("upload") == 0)
  {
    sendDownloadRequest();
  }
  else
  {
    sendReadRequest();
  }
}

void sendAuthenticationToServer()
{
  createAndBindSocket();
  printf("[Sending Authentication packet to the  Server]...\n");

  //Setting up Packet
  short oppCode = AUTH_OP_CODE;
  UDP_Packet authpacket;
  authpacket.operationCode = oppCode;
  strcpy(authpacket.username, Username.c_str());
  strcpy(authpacket.password, Password.c_str());
  char authPayload[sizeof(authpacket)];
  bzero(authPayload, sizeof(authpacket));
  memcpy(authPayload, &authpacket, sizeof(authpacket));

  //Sending the packet
  sendto(clientSocketFD, authPayload, sizeof(authPayload), 0, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
}


void incorrectInputError()
{
  printf("Correct Usage: <username:password@ip:port> <upload/download> <filename>\n");
  exit(0);
}
/*Splits a string into a vector using whatever delimeter you want*/
std::vector<std::string> splitString(const std::string & string,const std::string & delimeter)
{
  std::vector<std::string> directoryNames;
  int start = -1*delimeter.size();
  int end = -1*delimeter.size();

  do
  {
    start = end + delimeter.size();
    end = string.find(delimeter, start);

    directoryNames.push_back(string.substr(start, end - start));
  } while (end != -1);

  return directoryNames;
}


/*Getting Comandline Input*/
void parseInput(char**argv)
{
  std::vector<std::string>  originalInput = splitString(argv[1],"@");
  //Error Checking 
  if(originalInput.size() != 2)
  {
    incorrectInputError();
  }

  std::vector<std::string>  authenticationInput = splitString(originalInput[0],":");
  //Error Checking 
  if(authenticationInput.size() != 2)
  {
    incorrectInputError();
  }

  std::vector<std::string>  conectionInput = splitString(originalInput[1],":");
  //Error Checking 
  if(conectionInput.size() != 2)
  {
    incorrectInputError();
  }

  //Getting the Username and Password
  Username = authenticationInput[0];
  Password = authenticationInput[1];
  //Getting the UDP info
  serverIP_Adress = conectionInput[0];
  serverPortNumber = atoi(conectionInput[1].c_str());

  RequestType =  argv[2];
  //Checking if we have a valid RequestType
  bool isNotUpload = RequestType.compare("upload") != 0;
  bool isNotDownload = RequestType.compare("download") != 0;
  if(isNotUpload && isNotDownload)
  {
    printf("[Error Invlaid request type(%s). use(upload) or (download)]\n",RequestType.c_str());
    terminateProgram();
  }

  //Getting the file name  
  FileName = argv[3];
  // printf("User: %s\n",Username.c_str());
  // printf("Pass: %s\n",Password.c_str());
  // printf("ServerPortNumber: %d\n",serverPortNumber);
  // printf("ServerIP_Adress: %s\n",serverIP_Adress.c_str());
  // printf("Request Type: %s\n",RequestType.c_str());
  // printf("File name: %s\n",FileName.c_str());
}

int main(int argc, char **argv)
{
  if (argc != 4)
  {
    incorrectInputError();
  }

  parseInput(argv);

  createServerConectionSocket();
  sendAuthenticationToServer();
  waitForAcknowledgementMessage();
  requestPhase();
  terminateProgram();

  return 0;
}