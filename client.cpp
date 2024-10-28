#include <iostream>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 7432

using namespace std;

struct Array {
    char* data;
    int size;
    int capacity;

    Array(int initialCapacity = 10) : size(0), capacity(initialCapacity){
        data = new char[capacity];
    }

    ~Array() {
        delete[] data;
    }

    char* get(){
        return data;
    }
};

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        cerr << "Error to create socket" << endl;
        return -1;
    }

    // настройка адреса сервера
    serv_addr.sin_family = AF_INET; // IPv4
    serv_addr.sin_port = htons(PORT); // установка порта

    // преобразование ip в бинарный формат
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        cerr << "Incorrect address" << endl;
        return -1;
    }

    // подключение к серверу
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        cerr << "Connection error" << endl;
        return -1;
    }

    char buffer[1024]; // буфер для получаемого сообзения
    int connectResp = read(sock, buffer, 1024); // ACK на подключение
    cout << "Server message: " << buffer << endl;

    while (true)
    {
        string request;
        cout << "Enter request: ";
        getline(cin, request);

        if (request == "end")
        {
            break;
        }

        send(sock, request.c_str(), request.size(), 0); // отправляем запрос на сервер
        Array response(1024);
        memset(response.data, 0, response.size); // очищаем область памяти массива

        ssize_t byteResp = recv(sock, response.get(), response.size - 1, 0); // получение ответа
        if (byteResp > 0)
        {
            response.get()[byteResp] = '\0';
            cout << response.get();
        }
        else // в случае, если возникла ошибка заккрываем сокет
        {
            cout << "Getting packages error" << endl;
            close(sock);
            cout << "You have been forcibly disconnected" << endl;
            return 0;
        }
    }

    close(sock);
    cout << "You were successfully disconnected"; // отключаемся от сервера
    return 0;
}
