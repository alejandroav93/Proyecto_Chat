/*
Importacion de bibliotecas necesarias
*/
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>
#include <ctime>
#include "register.pb.h" //Importacion del archivo de protocolo compilado por protobuf
#include <atomic> 

#define MAX_CLIENT_CT 100
#define BUFFER_SIZE 2048
#define DEFAULT_SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT 3000
#define MAX_USERNAME_LENGTH  30
#define DISCONNECT "disconnect"

using namespace std;

/* Estructura de los clientes que se conectaran */
class Client {
public:
    Client();

    Client(sockaddr_in _address, int _socket) {
        address = _address;
        socket = _socket;
        name = "";
        status = 0;
        lastActionTimestamp = time(0);
        serverReady = false;
    }


    void nombreUsuario(string _name) {
        name = _name;
        serverReady = true;
    }

    void estado(int _status) {
        status = _status;
    }

    void updateTimestamp() {
        lastActionTimestamp = time(0);
    }

    sockaddr_in direccionCliente() {
        return address;
    }

    int obtenerSocket() {
        return socket;
    }

    string nombre() {
        return name;
    }

    bool isReady() {
        return serverReady;
    }

    int obtenerEstado() {
        return status;
    }

private:
    struct sockaddr_in address;
    int socket;
    string name;
    int status;
    int lastActionTimestamp;
    bool serverReady;
};

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
vector <pthread_t> threadRegistry;
string statusList[3] = {"Activo", "Ocupado", "Inactivo"};
int srvrPort;

map<string, Client *> clientRegistry = {};

void pushMessageToClient(Register messageRegister, Client *recieverDataPtr) {
    cout << "Mensaje Enviado" << endl;
    Client recieverData = *recieverDataPtr;

    string messageBuffer;
    messageRegister.SerializeToString(&messageBuffer);

    pthread_mutex_lock(&clients_mutex);
    if (clientRegistry.count(recieverData.nombre()) > 0) {
        cout << "Cliente encontrado" << endl;
        if (write(recieverData.obtenerSocket(), messageBuffer.c_str(), messageBuffer.length()) < 0) {
            cout << "No se pudo conectar al socket del usuario: " << recieverData.nombre() << ". Removiendo de lista de usuarios"
                 << endl;
            clientRegistry.erase(recieverData.nombre());
        } else {
            cout << "Mensaje enviado" << endl;
        }
    } else {
        cout << "No se pudo encontrar a " << recieverData.nombre() << endl;
    }
    pthread_mutex_unlock(&clients_mutex);

}

void broadCastMessage(Register messageRegister, Client *senderPtr) {
    cout << "Mensaje publico" << endl;
    Client sender = *senderPtr;
    map<string, Client *>::iterator it = clientRegistry.begin();

    for (auto const &it:clientRegistry) {
        pushMessageToClient(messageRegister, it.second);
    }
}

void routeMessage(char *encodedMessagePtr, Client *senderPtr) {
    Client sender = *senderPtr;

    string encodedMessage(encodedMessagePtr);
    Register inmessageRegister;
    Register outmessageRegister;
    Client *destinationClient = senderPtr;

    inmessageRegister.ParseFromString(encodedMessage);
    string outputMessage;

    switch (inmessageRegister.flag()) {
        case Register_Option::Register_Option_CONNECTED_USERS: {
            cout << "Lista de usuarios" << endl;
            map<string, Client *>::iterator it = clientRegistry.begin();
            for (auto const &it:clientRegistry) {
                Client *currentClientPtr = it.second;
                Client currentClient = *currentClientPtr;
                if (currentClient.isReady() && strcmp(currentClient.nombre().c_str(), sender.nombre().c_str()) != 0) {
                    outputMessage = outputMessage + "\n" + currentClient.nombre() + ": " +
                                    statusList[currentClient.obtenerEstado()] +
                                    ".";
                }
            }
            if (outputMessage.empty()) {
                outputMessage = "Solo se encuentra usted en el server...";
            }
            outmessageRegister.set_sender("server");
            break;

        };
        case Register_Option::Register_Option_USER_INFORMATION: {
            string wantedUserName = inmessageRegister.extra();
            if (clientRegistry.count(wantedUserName) > 0) {
                Client wantedClientData = *clientRegistry[wantedUserName];
                outputMessage = "Usuario: " + wantedClientData.nombre() +
                                ". Estatus: " + statusList[wantedClientData.obtenerEstado()];
            } else {
                outputMessage = "Usuario no existe";
            }
            break;
        };
        case Register_Option::Register_Option_DIRECT_MESSAGE: {

            string recieverUname = inmessageRegister.extra();

            outmessageRegister.set_sender(inmessageRegister.sender());

            outputMessage = "No se pudo enviar un mensaje a " + recieverUname + ". No se pudo encontrar el usuario.";

            if (clientRegistry.count(recieverUname) > 0) {
                Client *wantedClientDataPtr = clientRegistry[recieverUname];
                Client wantedClientData = *wantedClientDataPtr;
                if (wantedClientData.isReady()) {
                    outputMessage = sender.nombre() + "<prv>: " + inmessageRegister.message();
                    destinationClient = wantedClientDataPtr;
                }
            }
            break;

        };
        case Register_Option::Register_Option_STATUS_CHANGE: {

            string requestedClientStatus = inmessageRegister.extra();
            string currentClientStatus = statusList[sender.obtenerEstado()];
            outputMessage = "Este es su estado existente...";

            if (strcmp(requestedClientStatus.c_str(), currentClientStatus.c_str()) != 0) {
                int i;
                for (i = 0; i < 3; i++) {
                    if (strcmp(requestedClientStatus.c_str(), statusList[i].c_str()) == 0) {
                        outputMessage = "Actualizo su estado de: " + requestedClientStatus + " a " + statusList[i];
                        sender.estado(i);
                    }
                }
            }
            outmessageRegister.set_sender("server");
            break;

        };
        case Register_Option::Register_Option_SEND_MESSAGE: {
            broadCastMessage(inmessageRegister, senderPtr);
            break;
        };
        default: {
            cout << "ERROR: Mensaje invalido" << endl;
            break;
        }
    }

    outmessageRegister.set_code(200);
    outmessageRegister.set_message(outputMessage);
    pushMessageToClient(outmessageRegister, destinationClient);
}


void kickClient(Client *clientDataPtr) {
    Client clientData = *clientDataPtr;

    Register kickmessageRegister;
    kickmessageRegister.set_code(401);
    kickmessageRegister.set_sender("server");
    kickmessageRegister.set_message("Ha sido removido del servidor");

    pushMessageToClient(kickmessageRegister, clientDataPtr);
    clientRegistry.erase(clientData.nombre());
}

void *userThreadFn(void *arg) {
    char inMessageBuffer[BUFFER_SIZE + MAX_USERNAME_LENGTH];
    int *incomingConnectionPID = (int *) arg;
    cout << "Conexion con socket de usuario: OK" << endl;
    pthread_mutex_lock(&clients_mutex);


    map<string, Client *>::iterator it;

    it = clientRegistry.find(to_string(*incomingConnectionPID));
    Client *clientDataPtr = it->second;
    Client clientData = *clientDataPtr;

    pthread_mutex_unlock(&clients_mutex);

    bool exitFlag = false;

    if (recv(clientData.obtenerSocket(), inMessageBuffer, BUFFER_SIZE + MAX_USERNAME_LENGTH, 0) <= 0) {
        cout << "No se pudo establecer conexion, desconectando..." << endl;
        exitFlag = true;
    }
    cout << "CLIENT HANDSHAKE OK" << endl;

    string inMessageStr(inMessageBuffer); /*
    mensaje entrante*/
    Register register_Register;
    register_Register.ParseFromString(inMessageStr);

    string incomingClientName = register_Register.sender();

    cout << "Mensaje: OK" << endl;
    if (incomingClientName.length() < 4 || incomingClientName.length() > MAX_USERNAME_LENGTH) {
        cout << "ERROR: Usuario no valido";
        exitFlag = true;
    }
    if (strcmp(incomingClientName.c_str(), "server") == 0) {
        cout << "ERROR: Usuario no valid";
        exitFlag = true;
    }
    if (clientRegistry.count(incomingClientName) > 0 && !exitFlag) {
        cout << "ERROR: Usuario con ese nombre ya existe";
        exitFlag = true;
    } else {

        //remove original reference from map, start new reference with username
        clientData.nombreUsuario(incomingClientName);
        clientRegistry[clientData.nombre()] = &clientData;
        clientRegistry.erase(to_string(*incomingConnectionPID));

        //create message
        cout << clientData.nombre() << " Se ha unido al servidor" << endl;

        clientDataPtr = &clientData;

        Register newUsermessageRegister;
        newUsermessageRegister.set_message(incomingClientName + " Se ha unido al servidor");
        newUsermessageRegister.set_sender("server");
        newUsermessageRegister.set_flag(Register_Option::Register_Option_SEND_MESSAGE);
        newUsermessageRegister.set_code(200);

        broadCastMessage(newUsermessageRegister, clientDataPtr);

        Register reigstrationConfirmMessage;
        reigstrationConfirmMessage.set_message("Registro de usuario: OK");
        reigstrationConfirmMessage.set_sender("server");
        cout << "Registro de usuario: OK" << endl;
        reigstrationConfirmMessage.set_flag(Register_Option::Register_Option_DIRECT_MESSAGE);
        reigstrationConfirmMessage.set_code(200);

        pushMessageToClient(reigstrationConfirmMessage, clientDataPtr);
    }
    cout << "Cliente principal iniciado" << endl;
    for (;;) {
        if (exitFlag) {
            break;
        }
        bzero(inMessageBuffer, BUFFER_SIZE); // borra el buffer de los mensajes al enviar
        int incomingData = recv(clientData.obtenerSocket(), inMessageBuffer, BUFFER_SIZE, 0);
        cout << "incoming data: " << incomingData << endl;
        if (incomingData >= 0) {
            if (strlen(inMessageBuffer) > 0) {
                routeMessage(inMessageBuffer, clientDataPtr);
                cout << clientData.nombre() << " Recibio un mensaje" << endl;
            } else if (incomingData == 0 || strcmp(inMessageBuffer, DISCONNECT) == 0) {
                cout << clientData.nombre() << " Ha abandonado el chat" << endl;

                Register userLeftTheChatRegister;
                userLeftTheChatRegister.set_message(clientData.nombre() + " Ha abandonado el servidor");
                userLeftTheChatRegister.set_sender("server");
                userLeftTheChatRegister.set_flag(Register_Option::Register_Option_SEND_MESSAGE);
                userLeftTheChatRegister.set_code(200);
                broadCastMessage(userLeftTheChatRegister, clientDataPtr);
                clientRegistry.erase(clientData.nombre());

                exitFlag = true;
            } else {
                cout << "ERROR: Finalice la ejecucion" << endl;
                kickClient(clientDataPtr);
                exitFlag = true;
            }
        }
        bzero(inMessageBuffer, BUFFER_SIZE);

    } /* Finalizacion de pthreads de usuario */
    kickClient(clientDataPtr);
    close(clientData.obtenerSocket());
    pthread_detach(pthread_self());

    return nullptr;
}

int main(int argc, char **argv) {
    if (strcmp(argv[1], "-h") == 0) {
        cout << "Params: <PORT>" << endl;
        return 0;
    }
    if (argv[1]) {
        srvrPort = atoi(argv[1]);
    } else {
        srvrPort = DEFAULT_SERVER_PORT;
    }

    int option = 1;
    int connSocket = 0, incomingConnectionPID = 0;
    struct sockaddr_in serverConfigs;


    /* Socket settings */
    connSocket = socket(AF_INET, SOCK_STREAM, 0); /* IP protocol family.  */
    serverConfigs.sin_family = AF_INET; /* IP protocol family.  */
    serverConfigs.sin_addr.s_addr = inet_addr(DEFAULT_SERVER_IP); /* IP protocol family.  */
    serverConfigs.sin_port = htons(srvrPort); /*IP configure with endian byte*/


    if (setsockopt(connSocket, SOL_SOCKET, (SO_REUSEPORT | SO_REUSEADDR), (char *) &option, sizeof(option)) < 0) {
        cout << "ERROR: No se pudo crear una instancia con los parametros" << endl;
        return 1;
    }

    if (bind(connSocket, (struct sockaddr *) &serverConfigs, sizeof(serverConfigs)) < 0) {
        cout << "ERROR: No se pudo conectar al socket" << endl;
        return 1;
    }

    if (listen(connSocket, 10) < 0) {
        cout << "ERROR: No se pudo conectar al socket" << endl;
        return 1;
    }
    cout << "Inicio de server: OK" << endl;

    struct sockaddr_in clientSockAddr;
    pthread_t userThread;

    for (;;) {
        socklen_t clilen = sizeof(clientSockAddr) + MAX_USERNAME_LENGTH;
        incomingConnectionPID = accept(connSocket, (struct sockaddr *) &clientSockAddr, &clilen);

        int clientCount = clientRegistry.size();
        if (clientCount < MAX_CLIENT_CT) {
            cout << "Conexion hecha " << to_string(incomingConnectionPID) << endl;
            Client *newClientPtr = new Client(clientSockAddr, incomingConnectionPID);
            clientRegistry.insert(make_pair(to_string(incomingConnectionPID), newClientPtr));
            pthread_create(&userThread, NULL, &userThreadFn, (void *) &incomingConnectionPID);
            threadRegistry.push_back(userThread);
        }
         else {
            close(incomingConnectionPID);
            continue;
        }
    }
    // Finalizacion del programa del server
    for (pthread_t uthread: threadRegistry) {
        pthread_join(uthread, nullptr);
    }
    cout << "Saliendo de servidor" << endl;
    return 0;
}
