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

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        cerr << "Incorrect address" << endl;
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        cerr << "Connection error" << endl;
        return -1;
    }

    char buffer[1024];
    int connectResp = read(sock, buffer, 1024);
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

        send(sock, request.c_str(), request.size(), 0);
        Array response(1024);
        memset(response.data, 0, response.size);

        ssize_t byteResp = recv(sock, response.get(), response.size - 1, 0);
        if (byteResp > 0)
        {
            response.get()[byteResp] = '\0';
            cout << response.get();
        }
        else
        {
            cout << "Getting packages error" << endl;
            close(sock);
            cout << "You have been forcibly disconnected" << endl;
            return 0;
        }
    }
// INSERT INTO food VALUES ('beef', '1000')
    close(sock);
    cout << "You were successfully disconnected";
    return 0;
}
