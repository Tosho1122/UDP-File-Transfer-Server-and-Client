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
#include <unistd.h>

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
std::string WorkingDirectory = "";
std::string FileName = "";
uint16_t sessionNumber = 0;
int RetryAmount = 3;


/*UDP Connection Variables*/
int serverPortNumber;
char buffer[1024];
struct sockaddr_in serverAddress;
struct sockaddr_in clientAddress;
struct sockaddr_in fileTransferAddress;
int socketFD;
int fileSocketFD;
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

/*Creates and sends an error packet*/
void sendErrorPacket(std::string ErrorMessage, struct sockaddr_in targetAddress)
{
  //Creating packet
  UDP_Packet errorpacket;
  errorpacket.operationCode = ERROR_OP_CODE;
  strcpy(errorpacket.errorMessage, ErrorMessage.c_str());

  printf("[Sending Error Message to Client due to %s error]...\n", ErrorMessage.c_str());
 
  char errorPayload[sizeof(errorpacket)];
  bzero(errorPayload, sizeof(errorpacket));
  memcpy(errorPayload, &errorpacket, sizeof(errorpacket));
  
  //Sending packet
  sendto(socketFD, errorPayload, sizeof(errorPayload), 0, (struct sockaddr*)&targetAddress, sizeof(targetAddress));
}

/*The initailization and binding of the server socket*/
void createAndBindSocket()
{
  socketFD = socket(AF_INET, SOCK_DGRAM, 0);
  if (socketFD < 0)
  {
    perror("[Socket Error]");
    exit(1);
  }

  memset(&serverAddress, '\0', sizeof(serverAddress));
  serverAddress.sin_family = AF_INET;
  serverAddress.sin_port = htons(serverPortNumber);
  serverAddress.sin_addr.s_addr =  htonl(INADDR_ANY);

  //Binding the sever socket
  int bindStatus;
  bindStatus = bind(socketFD, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
  if (bindStatus < 0) 
  {
    perror("[Binding Error]");
    exit(1);
  }
}

/*Sends an acknowledgment of connection to the client*/
void sendConnectionAcknowledgment()
{
  //Creating Session number 
  sessionNumber = randomRange(1,65535);
  printf("[Generated session ID: %d and Sending Acknowledgment]\n",sessionNumber);

  //Setting up the Acknowledgement packet
  UDP_Packet acknowledgementPacket;
  acknowledgementPacket.operationCode = ACK_OP_CODE;
  acknowledgementPacket.sessionNumber = sessionNumber;
  acknowledgementPacket.blockNumber = 0;
  acknowledgementPacket.segementNumber = 0;

  //Sending acknowledgement Packet
  char acknowledgementPayload[sizeof(acknowledgementPacket)];
  bzero(acknowledgementPayload, sizeof(acknowledgementPacket));
  memcpy(acknowledgementPayload, &acknowledgementPacket, sizeof(acknowledgementPacket));

  sendto(fileSocketFD, acknowledgementPayload, sizeof(acknowledgementPayload), 0, (struct sockaddr*)&clientAddress, sizeof(clientAddress)); 
}

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
  addressSize = sizeof(clientAddress);
  recvfrom(fileSocketFD, ackPayload, sizeof(ackPayload), 0, (struct sockaddr*)&clientAddress, &addressSize);

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

/*Uploading the file to client*/
void uploadFileToClient(UDP_Packet requestPacket)
{
  //Check if we have the correct session Id
  if(requestPacket.sessionNumber != sessionNumber)
  {
    printf("[Error Invalid Session number]\n");
    //Sending Error packet
    sendErrorPacket("Error Invalid Session number",clientAddress);
    return;
  }

  printf("[DOWNLOAD REQUEST RECIVED] %s\n",requestPacket.fileName);

  //opening file 
  WorkingDirectory = WorkingDirectory + "/";
  std:: string fileDirectory = WorkingDirectory + requestPacket.fileName;

  FILE *file = fopen(fileDirectory.c_str(), "r");

  if(file == NULL)
  {
    printf("[Error File %s not found]\n",requestPacket.fileName);
    sendErrorPacket("Error File not found",clientAddress);
    return;
  }

  int currentRetryAmount = 0;
    
  //initalizing packet 
  UDP_Packet filePacket;
  filePacket.operationCode = RRQ_OP_CODE;
  filePacket.segementNumber = 0;

  int size = 1024;
  int sendResult;
  char readbuffer[size];
  
  char filePayload[sizeof(filePacket)];
  //Set the seek to the begining
  // Sending the data
  while (fgets(readbuffer, size, file) != NULL)
  {
    //Writing 
    strcpy(filePacket.fileData,readbuffer);

    //Sending file Packet
    bzero(filePayload, sizeof(filePacket));
    memcpy(filePayload, &filePacket, sizeof(filePacket));

    //Sending packket and Waiting for acknowledgement
    while (1)
    {
      //Check if reach our max Retry amount
      if(currentRetryAmount == RetryAmount)
      {
        printf("[Packet retransmitted Too many times]\n");
        printf("[Going to next packet]\n");
        break;
      }
      //Send Packet
      sendResult =  sendto(fileSocketFD, filePayload, sizeof(filePayload), 0, (struct sockaddr*)&clientAddress, sizeof(clientAddress));

      //Wait for Acknowledgement 
      if(fileRecived(filePacket.segementNumber))
      {
        break;
      }

      currentRetryAmount ++;
      printf("[Resending packet %d]\n",currentRetryAmount);
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

  sendResult = sendto(fileSocketFD, filePayload, sizeof(filePayload), 0, (struct sockaddr*)&clientAddress, sizeof(clientAddress));
  if (sendResult == -1)
  {
    perror("[ERROR] sending data to the server.");
    exit(1);
  }

  fclose(file);
}

/*Dowloading a file from the client */
void downloadFile()
{
  printf("[Writing File]....\n");

  //Creating the file
  std::string fileName = "./" + FileName;
  FILE *fileCopy;
  
  fileCopy = fopen(fileName.c_str(), "w");

  if(fileCopy == NULL)
  {
    perror("[Error]\n");
    return;
  }

  socklen_t addressSize;
  int size = 1024;

  // Receiving the data and writing it into the file.
  while (1)
  {
    //Set up For reciving packet
    UDP_Packet currentPacket;
    char payload[sizeof(currentPacket)];
    bzero(payload, sizeof(currentPacket));
    addressSize = sizeof(clientAddress);

    //Reciving packet
    recvfrom(fileSocketFD, payload, sizeof(payload), 0, (struct sockaddr*)&clientAddress, &addressSize);
    memcpy(&currentPacket, payload, sizeof(currentPacket));

    //Checking for an Error
    if(currentPacket.operationCode == ERROR_OP_CODE)
    {
      printf("-[%s]",currentPacket.errorMessage);
      terminateProgram();
    }

    //Sending Acknowledgement of File 
    UDP_Packet ackpacket;
    ackpacket.operationCode = ACK_OP_CODE;
    ackpacket.segementNumber = currentPacket.segementNumber;
    char authPayload[sizeof(ackpacket)];
    bzero(authPayload, sizeof(ackpacket));
    memcpy(authPayload, &ackpacket, sizeof(ackpacket));

    //Sending the packet
    sendto(fileSocketFD, authPayload, sizeof(authPayload), 0, (struct sockaddr*)&clientAddress, sizeof(clientAddress));

    if (strcmp(currentPacket.fileData, "END") == 0)
    {
      break;
    }

    //Writing to file 
    fprintf(fileCopy, "%s", currentPacket.fileData);
  }

  fclose(fileCopy);
}

void provideUploadRequest(UDP_Packet requestPacket)
{
  //Check if we have the correct session Id
  if(requestPacket.sessionNumber != sessionNumber)
  {
    printf("[Error Invalid Session number]\n");
    //Sending Error packet
    sendErrorPacket("Error Invalid Session number",clientAddress);
    return;
  }

  printf("[UPLOAD REQUEST RECIVED]\n");
  printf("[Sending Download Acknowledgement]\n");
  //Sending the acknoldegement of Download
  UDP_Packet acknowledgementPacket;
  acknowledgementPacket.operationCode = ACK_OP_CODE;
  acknowledgementPacket.blockNumber = 1;

  //Sending acknowledgement Packet
  char acknowledgementPayload[sizeof(acknowledgementPacket)];
  bzero(acknowledgementPayload, sizeof(acknowledgementPacket));
  memcpy(acknowledgementPayload, &acknowledgementPacket, sizeof(acknowledgementPacket));

  sendto(fileSocketFD, acknowledgementPayload, sizeof(acknowledgementPayload), 0, (struct sockaddr*)&clientAddress, sizeof(clientAddress)); 

  FileName =  requestPacket.fileName;
  downloadFile();
}

void waitForRequestPacket()
{
  printf("[Waiting for request packet]....\n",sessionNumber);
  
  //Setting up Packet
  UDP_Packet requestPacket;
  char requestPayload[sizeof(requestPacket)];
  bzero(requestPayload, sizeof(requestPacket));
  
  //Waiting for request
  socklen_t addressSize;
  addressSize = sizeof(clientAddress);
  recvfrom(fileSocketFD, requestPayload, sizeof(requestPayload), 0, (struct sockaddr*)&clientAddress, &addressSize);

  //Offloading the incoming packet
  memcpy(&requestPacket, requestPayload, sizeof(requestPacket));
  
  //Checking what type of request packet
  if(requestPacket.operationCode == RRQ_OP_CODE )
  {
    uploadFileToClient(requestPacket);
  }
  else if(requestPacket.operationCode == WRQ_OP_CODE )
  {
   provideUploadRequest(requestPacket);
  }
  else
  {
    printf("[Error Invalid opp code]\n");
    //Sending Error packet
    sendErrorPacket("Error Invalid opp code",clientAddress);
    return;
  }
}

void setUpRequestPhase()
{
  //Swicthing to a new Socket and port
  //Generating random port number 
  int randomPort = randomRange(1030,60000);

  fileSocketFD = socket(AF_INET, SOCK_DGRAM, 0);
  if (socketFD < 0)
  {
    perror("[Socket Error]");
    exit(1);
  }

  // Set a timeout of 5 seconds for receiving data
  timeout.tv_sec = 5;
  timeout.tv_usec = 0;
  setsockopt(fileSocketFD, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  memset(&fileTransferAddress, '\0', sizeof(fileTransferAddress));
  fileTransferAddress.sin_family = AF_INET;
  fileTransferAddress.sin_port = htons(randomPort);
  fileTransferAddress.sin_addr.s_addr =  htonl(INADDR_ANY);

  //Binding the sever socket
  int bindStatus;
  bindStatus = bind(fileSocketFD, (struct sockaddr*)&fileTransferAddress, sizeof(fileTransferAddress));
  if (bindStatus < 0) 
  {
    perror("[Binding Error]");
    exit(1);
  }

  sendConnectionAcknowledgment();
  waitForRequestPacket();
}

void reciveAuthenticationFromClient()
{
  printf("\n[Listening For a Client]...\n");
  socklen_t addressSize;

  //Initaliazing the authentication packet 
  UDP_Packet authpacket;
  char authPayload[sizeof(authpacket)];
  bzero(authPayload, sizeof(authpacket));
  
  //Waiting for the clients response
  addressSize = sizeof(clientAddress);
  recvfrom(socketFD, authPayload, sizeof(authPayload), 0, (struct sockaddr*)&clientAddress, &addressSize);
  memcpy(&authpacket, authPayload, sizeof(authpacket));
  
  //Checking if authentication is correct
  if(authpacket.operationCode != AUTH_OP_CODE )
  {
    printf("[Authentication Failed -> Authentication code ]\n");
    //Sending Error packet
    sendErrorPacket("invalid Authentication code",clientAddress);
    return;
  }

  if(strcmp(authpacket.username,Username.c_str()) != 0)
  {
    printf("[Authentication Failed -> Invalid Username]\n");
    //Sending Error packet
    sendErrorPacket("Invalid Username",clientAddress);
    return;
  }

  if(strcmp(authpacket.password,Password.c_str()))
  {
    printf("[Authentication Failed -> Invalid Password]\n");
    //Sending Error packet
    sendErrorPacket("Invalid Password",clientAddress);
    return;
  }

  printf("[Authentication Complete. Welcome (%s)]\n",Username.c_str());

  //Moveing to request phase
  setUpRequestPhase();
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
  //Getting the Username and Password
  std::vector<std::string>  authenticationInput = splitString(argv[1],":");
  //Error Checking 
  if(authenticationInput.size() != 2)
  {
    incorrectInputError();
  }

  Username = authenticationInput[0];
  Password = authenticationInput[1];

  //Getting the PortNumber
  serverPortNumber = atoi(argv[2]);

  //Getting the working directory 
  WorkingDirectory = argv[3];
}

int main(int argc, char **argv)
{

  if (argc != 4)
  {
   incorrectInputError();
  }
  
  parseInput(argv);
  createAndBindSocket();

  while(1)
  {
    reciveAuthenticationFromClient();
  }

  terminateProgram();
  return 0;
}