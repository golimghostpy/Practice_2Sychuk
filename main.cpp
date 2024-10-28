#include "libs.h"
#include "structures.h"
#define _BSD_SOURCE 1
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <atomic>

#define PORT 7432

int tuplesLim;
mutex mainMuter;
atomic<int> cntThreads(1);
const int MAX_CLIENTS = 50;

StringList take_section(StringList&, unsigned int, unsigned int);
void check_active(const string&, const StringList&);
void make_active(const string&, const StringList&);
void make_inactive(const string&, const StringList&);
string create_db();
StringList split(const string&, const string&);
string remove_extra(string&);
void write_in_csv(const string&, StringList);
string insert_into(const string&, StringList);
bool check_filter_delete(StringList&, StringList&, const string&);
string low_id(const string&, int);
string delete_from(const string&, StringList);
bool check_filter_select(const string&, const string&, int);
IntList cnt_rows(StringMatrix&);
string select_from(const string&, StringList);
SQLRequest get_com (const string&);
string comp_request(const string&, string);
void serve_client(int, const string&);
void start_server(const string&);
int main();

StringList take_section(StringList& source, unsigned int frontInd, unsigned int backInd){ // взятие части списка
    StringList out;
    unsigned int currInd = frontInd;
    while (currInd != backInd){
        out.push_back(source.find(currInd)->data);
        ++currInd;
    }
    return out;
}

void check_active(const string& genPath, const StringList& tables){ // проверка, используется ли сейчас таблица
    while (true){
        bool isFree = true;
        for (auto i = tables.first; i != nullptr; i = i->next){
            string path = genPath + i->data + "/" + i->data + "_lock.txt";
            ifstream checkActive(path);
            string line;
            checkActive >> line;
            checkActive.close();
            if (line == "1"){
                isFree = false;
            }
        }

        if (isFree) {break;}
    }
}

void make_active(const string& genPath, const StringList& tables){ // занять таблицу
    for (auto i = tables.first; i != nullptr; i = i->next){
        string path = genPath + i->data + "/" + i->data + "_lock.txt";
        ofstream makeActive(path);
        makeActive << 1;
        makeActive.close();
    }
}

void make_inactive(const string& genPath, const StringList& tables){ // освободить таблицу
    for (auto i = tables.first; i != nullptr; i = i->next){
        string path = genPath + i->data + "/" + i->data + "_lock.txt";
        ofstream makeInactive(path);
        makeInactive << 0;
        makeInactive.close();
    }
}

string create_db(){ // создание базы данных, если ее нет
    ifstream inFile("schema.json");
    nlohmann::json schema;
    inFile >> schema;

    tuplesLim = (int)schema["tuples_limit"];
    string name = schema["name"];
    if (filesystem::is_directory(name)){
        return (string)name;
    }
    filesystem::path basePath = name;

    for (auto i : schema["structure"].items()){
        filesystem::path dirPath = basePath / i.key();
        filesystem::create_directories(dirPath);

        ofstream outfile(dirPath / "1.csv");
        outfile << (i.key() + "_pk;");
        for (auto column = 0; column < i.value().size(); ++column){
            if (column == i.value().size() - 1){
                outfile << (string)i.value()[column] << endl;
            }else{
                outfile << (string)i.value()[column] << ";";
            }
        }
        outfile.close();

        ofstream lockfile(dirPath / (i.key() + "_lock.txt")); // добавлЯяем файл блокировки
        lockfile << "0";
        lockfile.close();

        ofstream keys(dirPath / (i.key() + "_pk_sequence.txt")); // добавляем счетчик id
        keys << "1";
        keys.close();
    }
    return (string)name;
}

StringList split(const string& str, const string& delimiter) { // разбиение строки в список
    StringList result;
    string currentPart;
    int delimiterLength = delimiter.size();

    for (auto i = 0; i < str.size(); ++i) {
        int j = 0;
        while (j < delimiterLength && i + j < str.size() && str[i + j] == delimiter[j]) {
            ++j;
        }

        if (j == delimiterLength) {
            if (currentPart != "") {
                result.push_back(currentPart);
                currentPart = "";
            }
            i += delimiterLength - 1;
        } else {
            currentPart += str[i];
        }
    }

    if (!currentPart.empty()) {
        result.push_back(currentPart);
    }

    return result;
}

string remove_extra(string& removeFrom){ // удаление лишних символов
    string newStr;
    for (auto i: removeFrom){
        if (i == '(' || i == '\'' || i == ')' || i == ',' || i == ' '){
            continue;
        }
        else {newStr += i;}
    }
    return newStr;
}

void write_in_csv(const string& path, StringList text){ // запись в csv файл
    ofstream out(path, ios_base::app);
    for (int i = 0; i < text.listSize; ++i){
        if(i != text.listSize - 1){
            out << text.find(i)->data << ";";
        }
        else {
            out << text.find(i)->data << endl;
        }
    }
}

string insert_into(const string& schemaName, StringList command){ // вставка строки в бд
    string table = command.find(2)->data; // получаем название таблицы

    StringList tables;
    tables.push_back(table);
    check_active(schemaName + "/", tables);
    make_active(schemaName + "/", tables);

    ifstream headerRead(schemaName + '/' + table + '/' + "1.csv"); // получение заголовка таблицы
    string header;
    headerRead >> header;
    headerRead.close();

     // обновление id
    StringList data;
    ifstream pkRead(schemaName + '/' + table + '/' + table + "_pk_sequence.txt");
    string idStr;
    getline(pkRead, idStr);
    pkRead.close();
    data.push_back(idStr);
    int newID = stoi(idStr) + 1;

    for (int i = 4; i < command.listSize; ++i){
        data.push_back(remove_extra(command.find(i)->data)); // чтение вставляемых данных
    }

    if (split(header, ";").listSize != data.listSize){
        make_inactive(schemaName + "/", tables);
        tables.clear();
        data.clear();
        return "Wrong count of arguments\n";
    }

    ofstream pkWrite(schemaName + '/' + table + '/' + table + "_pk_sequence.txt");
    pkWrite << to_string(newID);
    pkWrite.close();

     // поиск свободного места
    string path;
    int currFile = 1;
    do{
        path = schemaName + '/' + table + '/';
        ifstream check(path + to_string(currFile) + ".csv");
        if (check.bad()){
            break;
        }
        int cntLines = -1;
        string line;
        while(check >> line){
            ++cntLines;
        }

        if (cntLines <= tuplesLim){ // если переполнение лимитов, то создаем новый файл
            path += to_string(currFile) + ".csv";

            if (currFile > 1){
                ofstream headerWrite(path);
                headerWrite << header << endl;
                headerWrite.close();
            }

            write_in_csv(path, data);
            break;
        }
        ++currFile;
    }while(true);

    make_inactive(schemaName + "/", tables);
    tables.clear();
    data.clear();
    return "Inserted successfully\n";
}

bool check_filter_delete(StringList& header, StringList& text, const string& filter){ // проверка фильтра для удаления
    StringList orSplited = split(filter, " or ");
    for (Node<string>* i = orSplited.first; i != nullptr; i = i->next){ // слабый приоритет or
        StringList andSplited = split(i->data, " and ");
        bool isAnd = true;
        for (Node<string>* j = andSplited.first; j != nullptr; j = j->next){ // сильный приоритет and
            StringList expression = split(j->data, " ");
            string colName1 = split(expression.find(0)->data, ".").find(1)->data; // первая колонка
            int colIndex1 = header.index_word(colName1);
            if (expression.find(2)->data[0] == '\''){ // если сравнение со строкой
                if (text.find(colIndex1)->data != remove_extra(expression.find(2)->data)){
                    isAnd = false;
                    break;
                }
            }
            else { // если сравнение двух элементов таблицы
                string colName2 = split(expression.find(2)->data, ".").find(1)->data;
                int colIndex2 = header.index_word(colName2);
                if (text.find(colIndex1)->data != text.find(colIndex2)->data){
                    isAnd = false;
                    break;
                }
            }
        }
        if (isAnd){orSplited.clear(); return true;}
        andSplited.clear();
    }
    orSplited.clear();
    return false;
}

string low_id(const string& command, int lowOn){ // уменьшить id после удаления элементов
    StringList splited = split(command, ";");
    int id = stoi(splited.find(0)->data);
    id -= lowOn;
    splited.find(0)->data = to_string(id);
    string newCommand = splited.join(';');
    splited.clear();
    return newCommand;
}

string delete_from(const string& schemaName, StringList command){ // основная функция удаление
    if (command.listSize < 3)
    {
        return "Wrong count of arguments\n";
    }

    StringList tables; // получение таблиц
    tables.push_back(command.find(2)->data);
    check_active(schemaName + "/", tables);
    make_active(schemaName + "/", tables);

    string path = schemaName + '/' + command.find(2)->data + '/';
    int currentFile = 1;
    if (command.word_find("WHERE") == command.last){ // если есть фильтр, то пересоздаем таблицу
        while (remove((path + to_string(currentFile) + ".csv").c_str())){
            ++currentFile;
        }

        ifstream inFile("schema.json");
        nlohmann::json schema;
        inFile >> schema;
        StringList columns;
        columns.push_back(command.find(2)->data + "_pk");
        for (auto i: schema["structure"][command.find(2)->data]){
            columns.push_back(i);
        }

        ofstream newFirst(path + "1.csv");
        newFirst.close();

        write_in_csv(path + "1.csv", columns);

        ofstream updateId(path + command.find(2)->data + "_pk_seqquence.txt");
        updateId << "1";
        updateId.close();
        tables.clear();
        columns.clear();
        return "Deleted successfully\n";
    }

    StringList filter = take_section(command, 4, command.listSize); // получение фильтра
    string toSplit = filter.join(' ');
    int diffId = 0;
    do{ // проверяем каждую строчку на фильтр
        ifstream readFile(path + to_string(currentFile) + ".csv");
        if (!readFile.is_open()){
            break;
        }
        string strHeader;
        readFile >> strHeader;
        StringList header = split(strHeader, ";");
        string line;
        StringList save;
        while(readFile >> line){
            StringList data = split(line, ";");
            if (!check_filter_delete(header, data, toSplit)){
                string temp = low_id(line, diffId);
                save.push_back(temp); // сохраняем, если подойдет
            }
            else {
                ++diffId; // разница в id возрастает
            }
        }
        readFile.close();
        ofstream writeFile(path + to_string(currentFile) + ".csv");
        writeFile << strHeader << endl;
        for (Node<string>* i = save.first; i != nullptr; i = i->next){ // перезапись файла
            writeFile << i->data << endl;
        }
        writeFile.close();
        ++currentFile;
        header.clear();
        save.clear();
    }while(true);

     // изменение max id
    ifstream pkRead(schemaName + '/' + command.find(2)->data + '/' + command.find(2)->data + "_pk_sequence.txt");
    string idStr;
    getline(pkRead, idStr);
    pkRead.close();
    int newID = stoi(idStr) - diffId;
    ofstream pkWrite(schemaName + '/' + command.find(2)->data + '/' + command.find(2)->data + "_pk_sequence.txt");
    pkWrite << newID;
    pkWrite.close();

    make_inactive(schemaName + "/", tables);
    tables.clear();
    filter.clear();
    return "Deleted successfully\n";
}

 // проверка условия для select
bool check_filter_select(const string& schemaName, const string& filter, int currStr){
    Node<string>* orSplited = split(filter, " OR ").first;
    while(orSplited != nullptr){
        Node<string>* andSplited = split(orSplited->data, " AND ").first;
        bool isAnd = true;
        while(andSplited != nullptr){
            StringList eqlSplited = split(andSplited->data, " = ");
            StringList leftSplited = split(eqlSplited.first->data, ".");
            string leftTab = leftSplited.first->data, leftCol = leftSplited.first->next->data;

            int currFile = 1;
            int currLine = 0;
            string leftHeader;
            string leftLine;
            while (true){
                ifstream leftRead(schemaName + "/" + leftTab + "/" + to_string(currFile) + ".csv");

                if (!leftRead.is_open()){
                    break;
                }

                leftRead >> leftHeader;
                while(leftRead >> leftLine && currLine != currStr){
                    ++currLine;
                }

                if (currLine == currStr) break;
                ++currFile;
            }

            StringList splitedLeftHeader = split(leftHeader, ";");
            int leftColIndex = splitedLeftHeader.index_word(leftCol);
            string leftValue = split(leftLine, ";").find(leftColIndex)->data;


            if ((eqlSplited.first->next->data)[0] == '\''){
                string rightValue = remove_extra(eqlSplited.first->next->data);

                if (leftValue != rightValue){
                    isAnd = false;
                    break;
                }
            }
            else {
                StringList rightSplited = split(eqlSplited.first->next->data, ".");
                string rightTab = rightSplited.first->data, rightCol = remove_extra(rightSplited.first->next->data);

                currFile = 1;
                currLine = 0;
                string rightHeader;
                string rightLine;
                while (true){
                    ifstream rightRead(schemaName + "/" + rightTab + "/" + to_string(currFile) + ".csv");

                    if (!rightRead.is_open()){
                        break;
                    }

                    rightRead >> rightHeader;
                    while(rightRead >> rightLine && currLine != currStr){
                        ++currLine;
                    }

                    if (currLine == currStr) break;
                    ++currFile;
                }

                StringList splitedRightHeader = split(rightHeader, ";");
                int rightColIndex = splitedRightHeader.index_word(rightCol);
                string rightValue = split(rightLine, ";").find(rightColIndex)->data;

                if (leftValue != rightValue){
                    isAnd = false;
                    break;
                }
            }

            andSplited = andSplited->next;
        }
        if (isAnd){
            return true;
        }
        orSplited = orSplited->next;
    }
    return false;
}

IntList cnt_rows(StringMatrix& matrix){ // подсчет количества рядов в каждом столбце
    IntList eachCol;
    for (auto i = matrix.firstCol; i != nullptr; i = i->nextCol){
        int cntRow = 0;
        for (auto j = i->nextRow; j != nullptr; j = j->nextRow){
            ++cntRow;
        }
        eachCol.push_back(cntRow);
    }
    return eachCol;
}

string select_from(const string& schemaName, StringList command){ // функция получения выборки
    string genPath = schemaName + '/';

    int whereIndex = command.index_word("WHERE");
    whereIndex = whereIndex > 0 ? whereIndex : command.listSize;

     // получаем таблицы
    StringList tables = take_section(command, command.index_word("FROM") + 1, whereIndex);
    for (auto i = tables.first; i != nullptr; i = i->next){
        i->data = remove_extra(i->data);
    }

    check_active(genPath, tables);
    make_active(genPath, tables);

     // получаем колонки
    StringList columns = take_section(command, command.index_word("SELECT") + 1, command.index_word("FROM"));
    for (auto i = columns.first; i != nullptr; i = i->next){
        i->data = remove_extra(i->data);
    }

    StringMatrix toOut;
    int currTable = 0;
    int currCol = 0;
    if (command.word_find("WHERE") == command.last){ // если нет фильтра
        int totalCnt = 1;
        IntList strInTable;
        for (auto i = tables.first; i != nullptr; i = i->next){
            int currFile = 1;
            int cntLines = 0;
            string path = genPath + i->data + '/';

             // подсчет строк в каждом файле таблиц
            do{
                ifstream check(path + to_string(currFile) + ".csv");
                if (!check.is_open()){
                    break;
                }
                string line;
                while(check >> line){
                    ++cntLines;
                }
                ++currFile;
            }while(true);
            strInTable.push_back(--cntLines);
            totalCnt *= cntLines; // получаем число повторений для crossjoin
        }

         // заполнение матрицы для конечного вывода
        for (auto i = tables.first; i != nullptr; i = i->next){
            string path = genPath + i->data + '/';
            if (strInTable.find(currTable)->data != 0){
                totalCnt /= strInTable.find(currTable)->data;
            }
            for (auto j = columns.first; j != nullptr; j = j->next){
                StringList tabNCol = split(j->data, ".");
                if (tabNCol.find(0)->data != i->data){
                    continue;
                }

                ifstream forHead(path + "1.csv");
                string strHeader;
                forHead >> strHeader;
                forHead.close();
                StringList header = split(strHeader, ";");
                int takenId = header.index_word(tabNCol.find(1)->data);
                toOut.push_right(tables.find(currTable)->data + "." + tabNCol.find(1)->data);
                int currFile = 1;
                do{
                    ifstream readFile(path + to_string(currFile) + ".csv");
                    if (!readFile.is_open()){
                        break;
                    }
                    string line;

                    readFile >> line;
                    while(readFile >> line){
                        StringList splited = split(line, ";");
                        for (int k = 0; k < totalCnt; ++k){
                            toOut.push_down(splited.find(takenId)->data, currCol);
                        }
                        splited.clear();
                    }
                    readFile.close();
                    if (currTable != 0){
                        MatrixNode* currHead = toOut.lastCol;
                        int cntrCurr = 0;
                        for (auto k = currHead->nextRow; k != nullptr; k = k->nextRow){
                            ++cntrCurr;
                        }

                        int cntrFirst = 0;
                        for (auto k = toOut.firstCol; k != nullptr; k = k->nextRow){
                            ++cntrFirst;
                        }

                        if (cntrCurr == 0){++currFile; continue;}

                         // дублирование столбца вниз нужное количество раз
                        for (auto m = 0; m < (cntrFirst / cntrCurr) - 1; ++m){
                            MatrixNode* currRow = currHead->nextRow;
                            for (int k = 0; k < cntrCurr; ++k){
                                toOut.push_down(currRow->data, currCol);
                                currRow = currRow->nextRow;
                            }
                        }
                    }
                    ++currFile;
                }while(true);
                ++currCol;
            }
            ++currTable;
        }

          // проверка наличия каких-то данных в полученной матрице
        IntList eachCol = cnt_rows(toOut);
        for (auto i = eachCol.first; i != nullptr; i = i->next)
        {
            if (i->data == 0){
                string toReturn;
                toReturn += string("-") * 40 + "\n";
                for (auto j = toOut.firstCol; j != nullptr; j = j->nextCol){
                    toReturn += j->data + " ";
                }
                toReturn += "\n" + string("-") * 40 + "\n";
                return toReturn;
            }
        }

        string toReturn = toOut.print();
        make_inactive(genPath, tables);
        tables.clear();
        columns.clear();
        strInTable.clear();
        toOut.clear();
        eachCol.clear();
        return toReturn;
    }
     // получение фильтра
    string filter = take_section(command, command.index_word("WHERE") + 1, command.listSize).join(' ');

     // аналогичный проход по файлам с проверкой условий
    for (auto i = tables.first; i != nullptr; i = i->next){
        string path = genPath + i->data + "/";
        for (auto j = columns.first; j != nullptr; j = j->next){
            StringList tabNCol = split(j->data, ".");
            if (tabNCol.find(0)->data != i->data){
                continue;
            }

            ifstream forHead(path + "1.csv");
            string strHeader;
            forHead >> strHeader;
            forHead.close();
            StringList header = split(strHeader, ";");
            int takenId = header.index_word(tabNCol.find(1)->data);
            int currFile = 1;
            int currStr = 0;
            toOut.push_right(j->data);
            do{
                ifstream readFile(path + to_string(currFile) + ".csv");
                if (!readFile.is_open()){
                    break;
                }
                string line;

                readFile >> line;
                while(readFile >> line){
                    StringList splited = split(line, ";");
                    if (check_filter_select(schemaName, filter, currStr)){
                        toOut.push_down(splited.find(takenId)->data, currCol);
                    }
                    ++currStr;
                }
                readFile.close();
                ++currFile;
            }while(true);
            ++currCol;
        }
    }

    IntList cntInEach = cnt_rows(toOut);
    for (auto i = cntInEach.first; i != nullptr; i = i->next){
        if (i->data == 0){
            string toReturn;
            toReturn += string("-") * 40 + "\n";
            for (auto j = toOut.firstCol; j != nullptr; j = j->nextCol){
                toReturn += j->data + " ";
            }
            toReturn += "\n" + string("-") * 40 + "\n";
            return toReturn;
        }
    }

    int total = 1;
    for (auto i = cntInEach.first->next; i != nullptr; i = i->next){
        total *= i->data;
    }

    // повторяем каждую строчку нужное кол-во раз
    StringMatrix temp;
    currCol = 0;
    for (auto i = toOut.firstCol; i != nullptr; i = i->nextCol){
        temp.push_right(i->data);
        for (auto j = i->nextRow; j != nullptr; j = j->nextRow){
            for (int k = 0; k < total; ++k){
                temp.push_down(j->data, currCol);
            }
        }
        total /= cntInEach.find(currCol)->data;
        ++currCol;
    }

    // повторяем каждый блок нужное кол-во раз
    toOut.clear();
    StringMatrix finalOut;
    cntInEach = cnt_rows(temp);
    currCol = 0;
    for (auto i = temp.firstCol; i != nullptr; i = i->nextCol){
        finalOut.push_right(i->data);
        for (int k = 0; k < (cntInEach.find(0)->data / cntInEach.find(currCol)->data); ++k){
            for (auto j = i->nextRow; j != nullptr; j = j->nextRow){
                finalOut.push_down(j->data, currCol);
            }
        }
        ++currCol;
    }


    string toReturn = finalOut.print();
    temp.clear();
    finalOut.clear();
    cntInEach.clear();
    make_inactive(genPath, tables);
    tables.clear();
    columns.clear();
    return toReturn;
}

SQLRequest get_com (const string& command){ // выбор токена
    if (command == "SELECT") {return SQLRequest::SELECT;}
    if (command == "INSERT") {return SQLRequest::INSERT;}
    if (command == "DELETE") {return SQLRequest::DELETE;}
    return SQLRequest::UNKNOWN;
}

string comp_request(const string& schemaName, string request){
    StringList splited = split(request, " "); // делим запрос
    SQLRequest choice = get_com(splited.find(0)->data); // полчаем токен
    switch (choice){ // в зависимости от токена вызываем нужную функцию
    case SQLRequest::SELECT: return select_from(schemaName, splited);
    case SQLRequest::INSERT: return insert_into(schemaName, splited);
    case SQLRequest::DELETE: return delete_from(schemaName, splited);
    case SQLRequest::UNKNOWN: return "Wrong command!";
    }
}

void serve_client(int servSocket, const string& schemaName){
    ++cntThreads; // увеличиваем количество клиентов на сервере

    while (true) { // начинаем слушать запросы
        Array client(1024);
        memset(client.get(), 0, client.size); // очищаем буфер

        ssize_t bytesRead = recv(servSocket, client.get(), client.size - 1, 0); // получаем запрос
        if (bytesRead <= 0) { // либо клиент отключился, либо произошла ошибкаа при передаче данных
            cout << "Error or client was disconnected" << endl;
            break;
        }

        {
            lock_guard<mutex> lock(mainMuter); // ограничиваем доступ, чтобы не было ошибок в выводе
            client.get()[bytesRead] = '\0';
            cout << "Request taken: " << client.get() << endl;
        }

        string answer = "Server message:\n";
        string request = client.get();
        string partRes = comp_request(schemaName, request); // отправляем запрос на выполнение
        answer += partRes; // формируем ответ

        send(servSocket, answer.c_str(), answer.size(), 0); // отправляем ответ
    }

    close(servSocket); // закрываем сокет для клиента
    --cntThreads; // уменьшаем количество клиентов
}

void start_server(const string& schemaName) {
    int serverSocket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};

    // создание сокета
    if ((serverSocket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        cerr << "Error of create socket" << endl;
        return;
    }

    // настройка параметров сокета
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        cerr << "Error of setting parameters of socket" << endl;
        return;
    }

    address.sin_family = AF_INET; // IPv4
    address.sin_addr.s_addr = inet_addr("127.0.0.1"); // установка IP
    address.sin_port = htons(PORT); // уановка порта

    // привязкаа сокета к адресу
    if (bind(serverSocket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        cerr << "Error of binding" << endl;
        return;
    }

    // прослушивание подключений
    if (listen(serverSocket, 1) < 0) {
        cerr << "Error of socket listening" << endl;
        return;
    }

    cout << "Server started" << endl;

    while (true){ // начинаем прослушивание
        sockaddr_in clientAddress;
        socklen_t clientSize = sizeof(clientAddress);
        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddress, &clientSize); // принимаем подключение клиента
        if(clientSocket < 0){
            cout << "Error to connect client" << endl;
            continue;
        }

        if(cntThreads <= MAX_CLIENTS){ // если на сервере есть место
            char* clientIP = inet_ntoa(clientAddress.sin_addr); // получаем IP клиента
            cout << "Client[" << clientIP << "] was connected" << endl; // выводим клиента, который подключился
            thread(serve_client, clientSocket, schemaName).detach(); // выводим клиента в другой поток
            // и отключаем отслеживание

            string answer = "Successfully connected to the server";
            send(clientSocket, answer.c_str(), answer.size(), 0); // отправляем клиенту успешное подключение
        }
        else{
            string answer = "A lot of clients now, try it later";
            send(clientSocket, answer.c_str(), answer.size(), 0);
            close(clientSocket);
        }
    }

    close(serverSocket); // закрываем сервер
}

int main()
{
    string schemaName = create_db(); // создаем бд из json, если ее не было
    start_server(schemaName); // запускаем сервер
    return 0;
}
