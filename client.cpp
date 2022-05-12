/*
Importacion de bibliotecas necesarias
*/
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <atomic>
#include <ifaddrs.h>
#include "register.pb.h" //Importacion del archivo de protocolo compilado por protobuf
#define DEFAULT_CLIENT_NAME "grupo3"
#define BUFFER_SIZE  2048 
#define MAX_USERNAME_LENGTH  30 //
#define DEFAULT_SERVER_PORT 3000
#define DEFAULT_SERVER_IP "127.0.0.1"
#define LOCAL_IP "127.0.0.1"
#define DISCONNECT "disconnect"

using namespace std;

string statusList[3] = {"Activo", "Ocupado", "Inactivo"};

atomic<int> salida(0);
int Socket = 0;
int srvrPort;
string srvrIp;
string clientName;

pthread_t sendMessageThread; //pThread for message sending
pthread_t getMessageThread; //pThread for message receiver

/*
  Menu options print  (1-7)
*/
void printMenu() {
    cout << "1) Mostrar Menu" << endl;
    cout << "2) Difundir mensaje publico" << endl;
    cout << "3) Enviar mensaje privado/directo" << endl;
    cout << "4) Cambiar estatus" << endl;
    cout << "5) Visualizar lista de usuarios" << endl;
    cout << "6) Visualizar info. de usuario" << endl;
    cout << "7) Finalizar" << endl;
    cout << endl << "Ingrese opcion deseada (1-7): ";
}

void *sendMessageThreadFn(void *arg) {
    int opt;
    char outMessageBuffer[BUFFER_SIZE + MAX_USERNAME_LENGTH]; //
    printMenu(); // Print Menu
    for (;;) {
        opt = 1;
        if (salida.load()) {
            break;
        }
        Register messageSentPayload;

        bool sendMessage = false;
        try {
            cin >> opt; 
            cin.ignore();
        } catch (exception &e) {
            cout << endl << "Opcion invalida, ingrese una opcion valida (1-7)" << endl;
            continue;
        }
        switch (opt) {
            case 1: { // MOSTRAR MENU
                break;
            };
            case 2: { // Mensaje publico o broadcast
                messageSentPayload.set_flag(Register_Option::Register_Option_SEND_MESSAGE);

                string msgBody;
                cout << "Ingrese mensaje publico: ";
                getline(cin, msgBody);
                if ((msgBody.empty())) {
                    continue;
                }
                messageSentPayload.set_message(msgBody);
                sendMessage = true;
                cout << endl;
                break;
            };
            case 3: { // Mensaje privado/directo
                messageSentPayload.set_flag(Register_Option::Register_Option_DIRECT_MESSAGE);

                string receiverUsername;
                string msgBody;
                cout << "Ingrese usuario de recipiente: ";
                getline(cin, receiverUsername);

                cout << endl << "Ingrese Mensaje: " << endl;
                getline(cin, msgBody);

                messageSentPayload.set_message(msgBody);
                messageSentPayload.set_extra(receiverUsername);
                if (msgBody.empty() && receiverUsername.empty()) {
                    continue;
                }
                sendMessage = true;
                break;
            };
            case 4: { // Cambiar estado personal
                messageSentPayload.set_flag(Register_Option::Register_Option_STATUS_CHANGE);
                cout << "Seleccione estado: ";
                int optionIndex;
                int i;
                for (i = 0; i < 3; i++) {
                    cout << i << ") " << statusList[i] << endl;
                }
                try {
                    cin >> optionIndex;
                } catch (exception &e) {
                    cout << endl << "ERROR: Ingrese opcion valida" << endl;
                    continue;
                }
                if (optionIndex < 0 || optionIndex > 3) {
                    cout << "ERROR: Opcion invalida!" << endl;
                    continue;
                }
                messageSentPayload.set_message(statusList[optionIndex]);
                messageSentPayload.set_extra(statusList[optionIndex]);
                sendMessage = true;
                break;

            };
            case 5: { // Desplegar lista de usuarios y estados
                messageSentPayload.set_flag(Register_Option::Register_Option_CONNECTED_USERS);
                messageSentPayload.set_message("Obtener Usuarios:");
                sendMessage = true;
                break;

            };
            case 6: { // Desplegar informacion de usuario
                messageSentPayload.set_flag(Register_Option::Register_Option_USER_INFORMATION);
                string usernameToCheck;
                cout << "Ingrese nombre de usuario a verificar: ";
                getline(cin, usernameToCheck);
                if (usernameToCheck.empty()) {
                    cout << "EROR: No puede ingresar un usuario vacio";
                    continue;
                }
                if (strcmp(usernameToCheck.c_str(), clientName.c_str()) == 0) {
                    cout << "ERROR: se esta verificando a usted mismo....";
                    continue;
                }

                messageSentPayload.set_extra(usernameToCheck);
                messageSentPayload.set_message(usernameToCheck);
                sendMessage = true;
                break;

            };
            case 7: {  // Salida del programa
                salida = 1;
                messageSentPayload.set_flag(Register_Option::Register_Option_SEND_MESSAGE);
                messageSentPayload.set_message(DISCONNECT);
                sendMessage = true;
                break;
            };
            default: {
                cout << "Opcion invalida! Ingrese opcion (1-7)" << endl;
                break;

            };
        }
        if (sendMessage) {
            messageSentPayload.set_sender(clientName);
            messageSentPayload.set_ip(LOCAL_IP);

            string outMessageStr;
            messageSentPayload.SerializeToString(&outMessageStr);
            char outMessageBuffer[BUFFER_SIZE + MAX_USERNAME_LENGTH];
            strcpy(outMessageBuffer, outMessageStr.c_str());
            send(Socket, outMessageBuffer, strlen(outMessageBuffer), 0);
            bzero(outMessageBuffer, BUFFER_SIZE + MAX_USERNAME_LENGTH);
        }
    }
    cout << "Saliendo del programa" << endl;
    return nullptr;
}

void *getMessageThreadFn(void *arg) {

    char rawMessage[BUFFER_SIZE];
    for (;;) {
        if (salida.load()) {
            break;
        }
        int socketEntrante = recv(Socket, rawMessage, BUFFER_SIZE, 0);
        if (socketEntrante > 0) {
            Register messageBroadcast;
            messageBroadcast.ParseFromString(rawMessage);
            switch (messageBroadcast.code()) {
                case 200: {
                    cout << messageBroadcast.sender() << ": " << messageBroadcast.message() << endl;
                    break;
                };
                case 400:
                case 401: {
                    cout << messageBroadcast.sender() << ": " << messageBroadcast.message() << endl;
                    cout << "Ha sido desconectado del servidor " << endl;
                    break;
                }
                default: {
                    if (messageBroadcast.flag() == Register_Option::Register_Option_SEND_MESSAGE) {
                        cout << messageBroadcast.sender() << ": " << messageBroadcast.message() << endl;
                    } else {
                        cout << "ERROR: Hay una problema con la conexion" << endl;
                    }
                    break;
                }
            }
        } else if (socketEntrante == 0) {
            break; //error or closed message
        }
        memset(rawMessage, 0, sizeof(rawMessage));
    }
    cout << "Ha sido desconectado del servidor" << endl;
    return nullptr;
}

int main(int argc, char **argv) {

    if (strcmp(argv[1], "-h") == 0) {
        cout << "Params: <PORT> <UNAME> <IP>" << endl;
        return 0;
    }
    //set default flags
    if (argv[1]) {
        srvrPort = stoi(argv[1]);
    } else {
        srvrPort = DEFAULT_SERVER_PORT;
    }

    if (argv[2]) {
        clientName = argv[2];
    } else {
        clientName = DEFAULT_CLIENT_NAME;
    }

    if (argv[3]) {
        srvrIp = argv[3];
    } else {
        srvrIp = DEFAULT_SERVER_IP;
    }

    if (srvrPort < 0) {
        cout << "ERROR: Puerto Invalido" << endl;
        return 1;
    }
    if (strlen(srvrIp.c_str()) < 8) {
        cout << "ERROR: Direccion IP con formato erroneo" << endl;
        return 1;
    }
    if (strlen(clientName.c_str()) > MAX_USERNAME_LENGTH || strlen(clientName.c_str()) < 4) {
        cout << "ERROR: El nombre de usuario debe tener entre 4 y 30 caracteres" << endl;
        return 1;
    }


    struct sockaddr_in serverConfigs;
    Socket = socket(AF_INET, SOCK_STREAM, 0);
    serverConfigs.sin_family = AF_INET;
    serverConfigs.sin_addr.s_addr = inet_addr(srvrIp.c_str());
    serverConfigs.sin_port = htons(srvrPort);


    if (connect(Socket, (struct sockaddr *) &serverConfigs, sizeof(serverConfigs)) == -1) {
        cout << "ERROR: No se pudo establecer conexion con el servidor" << endl;
        return 1;
    }
    cout << "Conexion con Socket: OK" << endl;


    Register dataPayload;
    dataPayload.set_sender(clientName);
    dataPayload.set_ip(LOCAL_IP);
    dataPayload.set_flag(Register_Option::Register_Option_USER_LOGIN);


    string msgBinary;
    dataPayload.SerializeToString(&msgBinary);
    char wBuffer[BUFFER_SIZE + MAX_USERNAME_LENGTH];
    strcpy(wBuffer, msgBinary.c_str());

    send(Socket, wBuffer, strlen(wBuffer), 0);

    cout << "Conexion con el servidor: OK" << endl;



    int senderThreadCreateFlag = pthread_create(&sendMessageThread, nullptr, &sendMessageThreadFn, nullptr);
    int getThreadCreateFlag = pthread_create(&getMessageThread, nullptr, &getMessageThreadFn, nullptr);

    if (senderThreadCreateFlag != 0 || getThreadCreateFlag != 0) {
        cout << "ERROR: No se pudieron crear los pThreads" << endl;
        return 1;
    }

    while (!salida.load()) {}
    pthread_join(sendMessageThread, nullptr);
    pthread_join(getMessageThread, nullptr);
    cout << "pThreads terminados" << endl;
    close(Socket);
    cout << "Conexion con el servidor finalizada" << endl;

    return 0;
}
