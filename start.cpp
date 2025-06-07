#include <iostream>
#include <cstring> // Dla memset, strerror
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm> // Dla std::transform
#include <cctype>    // Dla std::tolower
#include <chrono>    // Dla std::chrono
#include <iomanip>   // Dla std::put_time
#include <cerrno>    // Dla errno

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> // Dla inet_ntop
#include <unistd.h>    // Dla read, write, close

// --- Funkcje pomocnicze ---

// Funkcja do logowania wiadomości
void log_message(const std::string& message, const std::string& log_file_name, bool is_error = false) {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::tm time_info{};
#if defined(_MSC_VER) || defined(__MINGW32__) // Dla kompilatorów MSVC i MinGW
    localtime_s(&time_info, &in_time_t);
#else // Dla systemów POSIX (Linux, macOS)
    localtime_r(&in_time_t, &time_info);
#endif

    std::ostringstream timestamp_ss;
    timestamp_ss << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S");

    std::string full_log_message = timestamp_ss.str() + " - " + message;

    if (is_error) {
        std::cerr << full_log_message << std::endl;
    } else {
        std::cout << full_log_message << std::endl;
    }

    std::ofstream log_file_stream(log_file_name, std::ios::app);
    if (log_file_stream.is_open()) {
        log_file_stream << full_log_message << std::endl;
        log_file_stream.close();
    } else {
        // Logowanie błędu otwarcia pliku logu tylko do cerr, aby uniknąć pętli
        std::cerr << timestamp_ss.str() << " - Critical Error: Unable to open log file: " << log_file_name << std::endl;
    }
}

// Funkcja pomocnicza do wysyłania odpowiedzi HTTP
void send_response(int socket_fd, const std::string& status_code, const std::string& content_type, const std::string& body, const std::string& log_file_name) {
    std::ostringstream response_stream;
    response_stream << "HTTP/1.1 " << status_code << "\r\n";
    response_stream << "Content-Type: " << content_type << "\r\n";
    response_stream << "Content-Length: " << body.length() << "\r\n";
    response_stream << "Connection: close\r\n"; // Informujemy klienta, że zamkniemy połączenie
    response_stream << "\r\n";
    response_stream << body;
    std::string response_str = response_stream.str();
    
    ssize_t bytes_sent = write(socket_fd, response_str.c_str(), response_str.length());
    if (bytes_sent < 0) {
        log_message("Error writing response to socket: " + std::string(strerror(errno)), log_file_name, true);
    } else if (static_cast<size_t>(bytes_sent) < response_str.length()) {
        log_message("Warning: Partial write for response. Sent " + std::to_string(bytes_sent) + "/" + std::to_string(response_str.length()), log_file_name, true);
    }
}

// Funkcja pomocnicza do wysyłania pliku jako odpowiedzi HTTP
void send_file_response(int socket_fd, const std::string& status_code, const std::string& content_type, const std::vector<char>& file_data, const std::string& log_file_name) {
    std::ostringstream response_stream;
    response_stream << "HTTP/1.1 " << status_code << "\r\n";
    response_stream << "Content-Type: " << content_type << "\r\n";
    response_stream << "Content-Length: " << file_data.size() << "\r\n";
    response_stream << "Connection: close\r\n";
    response_stream << "\r\n";
    std::string header_str = response_stream.str();

    ssize_t bytes_sent = write(socket_fd, header_str.c_str(), header_str.length());
    if (bytes_sent < 0) {
        log_message("Error writing file response headers to socket: " + std::string(strerror(errno)), log_file_name, true);
        return; 
    } else if (static_cast<size_t>(bytes_sent) < header_str.length()) {
        log_message("Warning: Partial write for file response headers. Sent " + std::to_string(bytes_sent) + "/" + std::to_string(header_str.length()), log_file_name, true);
    }

    if (!file_data.empty()) {
        bytes_sent = write(socket_fd, file_data.data(), file_data.size());
        if (bytes_sent < 0) {
            log_message("Error writing file data to socket: " + std::string(strerror(errno)), log_file_name, true);
        } else if (static_cast<size_t>(bytes_sent) < file_data.size()) {
            log_message("Warning: Partial write for file data. Sent " + std::to_string(bytes_sent) + "/" + std::to_string(file_data.size()), log_file_name, true);
        }
    }
}

// Prosta funkcja do określania typu MIME na podstawie rozszerzenia pliku
std::string get_mime_type(const std::string& file_path) {
    std::string ext;
    size_t dot_pos = file_path.rfind('.');
    if (dot_pos != std::string::npos && dot_pos < file_path.length() - 1) {
        ext = file_path.substr(dot_pos);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    }

    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".txt") return "text/plain";
    if (ext == ".css") return "text/css";
    if (ext == ".js") return "application/javascript";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    if (ext == ".gif") return "image/gif";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".ico") return "image/x-icon";
    return "application/octet-stream"; // Domyślny typ
}

// Funkcja obsługująca pojedyncze połączenie klienta
void handle_connection(int client_socket, const struct sockaddr_in& client_address_struct, 
                       const std::string& www_root, const std::string& log_file_name) {
    // Podstawowe ustawienia bezpieczeństwa / limity
    const size_t MAX_URI_LENGTH = 2048;
    const size_t MAX_REQUEST_LINE_LENGTH = 4000; // Długość pierwszej linii żądania
    
    char client_ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_address_struct.sin_addr, client_ip_str, INET_ADDRSTRLEN);

    char buffer[4096]; // Bufor na żądanie HTTP
    memset(buffer, 0, sizeof(buffer));
    ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1); // -1 dla null terminatora, choć nie jest ściśle potrzebny przy użyciu std::string(buffer, bytes_read)

    if (bytes_read <= 0) {
        if (bytes_read < 0) {
            log_message("IP: " + std::string(client_ip_str) + " - Error reading from socket: " + std::string(strerror(errno)), log_file_name, true);
        } else { // bytes_read == 0
            log_message("IP: " + std::string(client_ip_str) + " - Client disconnected before sending data.", log_file_name);
        }
        close(client_socket);
        return;
    }

    // Sprawdzenie, czy pierwsza linia żądania nie jest za długa
    std::string first_line_check(buffer, bytes_read); // Użyj bytes_read do konstruktora string
    size_t first_crlf = first_line_check.find("\r\n");
    if (first_crlf == std::string::npos || first_crlf > MAX_REQUEST_LINE_LENGTH) {
        log_message("IP: " + std::string(client_ip_str) + " - Request line too long or malformed.", log_file_name, true);
        send_response(client_socket, "414 URI Too Long", "text/html", "<html><body><h1>414 URI Too Long</h1><p>The request line is too long.</p></body></html>", log_file_name);
        close(client_socket);
        return;
    }

    std::string request_str(buffer, bytes_read); 
    std::istringstream request_stream(request_str);
    std::string method, requested_uri, http_version;

    request_stream >> method >> requested_uri >> http_version;

    // Logowanie informacji o żądaniu
    std::string short_request_line = first_line_check.substr(0, first_crlf);
    log_message("IP: " + std::string(client_ip_str) + " - Request: \"" + short_request_line + "\"", log_file_name);

    if (requested_uri.length() > MAX_URI_LENGTH) {
        log_message("IP: " + std::string(client_ip_str) + " - URI too long: " + requested_uri, log_file_name, true);
        send_response(client_socket, "414 URI Too Long", "text/html", "<html><body><h1>414 URI Too Long</h1></body></html>", log_file_name);
        close(client_socket);
        return;
    }

    if (request_stream.fail() || method.empty() || requested_uri.empty() || http_version.empty()) {
        log_message("IP: " + std::string(client_ip_str) + " - Malformed request line: \"" + short_request_line + "\"", log_file_name, true);
        send_response(client_socket, "400 Bad Request", "text/html", "<html><body><h1>400 Bad Request</h1><p>Your browser sent a request that this server could not understand.</p></body></html>", log_file_name);
        close(client_socket);
        return;
    }

    // Prosta walidacja znaków w URI (przykład, można rozbudować)
    // Dozwolone: a-z, A-Z, 0-9, -, _, ., /
    for (char c : requested_uri) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.' && c != '/') {
            log_message("IP: " + std::string(client_ip_str) + " - Invalid character in URI: " + requested_uri, log_file_name, true);
            send_response(client_socket, "400 Bad Request", "text/html", "<html><body><h1>400 Bad Request</h1><p>Invalid characters in URI.</p></body></html>", log_file_name);
            close(client_socket);
            return;
        }
    }


    if (method != "GET") {
        log_message("IP: " + std::string(client_ip_str) + " - Method not implemented: " + method, log_file_name);
        send_response(client_socket, "501 Not Implemented", "text/html", "<html><body><h1>501 Not Implemented</h1><p>Only GET method is supported.</p></body></html>", log_file_name);
    } else {
        std::string file_path = www_root;
        if (requested_uri == "/" || requested_uri.empty()) {
            file_path += "/index.html"; 
        } else {
            // Prosta sanityzacja - zapobieganie path traversal
            if (requested_uri.find("..") != std::string::npos) {
                log_message("IP: " + std::string(client_ip_str) + " - Path traversal attempt: " + requested_uri, log_file_name, true);
                send_response(client_socket, "403 Forbidden", "text/html", "<html><body><h1>403 Forbidden</h1><p>Path traversal attempt detected.</p></body></html>", log_file_name);
                close(client_socket); // Zamknij gniazdo natychmiast po wykryciu próby ataku
                return;
            }
            // Upewnij się, że ścieżka jest poprawnie sklejona
            if (requested_uri[0] == '/') {
                file_path += requested_uri;
            } else {
                file_path += "/" + requested_uri;
            }
        }

        log_message("IP: " + std::string(client_ip_str) + " - Attempting to serve file: " + file_path, log_file_name);
        std::ifstream requested_file_stream(file_path, std::ios::binary | std::ios::ate);
        if (!requested_file_stream.is_open()) {
            log_message("IP: " + std::string(client_ip_str) + " - File not found (404): " + file_path, log_file_name, true);
            std::string body_404 = "<html><body><h1>404 Not Found</h1><p>The requested file " + requested_uri + " was not found on this server.</p></body></html>";
            send_response(client_socket, "404 Not Found", "text/html", body_404, log_file_name);
        } else {
            std::streamsize file_size = requested_file_stream.tellg();
            requested_file_stream.seekg(0, std::ios::beg);

            std::vector<char> file_buffer(static_cast<size_t>(file_size)); // Upewnij się, że file_size nie jest ujemny
            if (file_size > 0 && requested_file_stream.read(file_buffer.data(), file_size)) {
                std::string mime_type = get_mime_type(file_path);
                log_message("IP: " + std::string(client_ip_str) + " - Serving file: " + file_path + " (" + mime_type + ", " + std::to_string(file_size) + " bytes)", log_file_name);
                send_file_response(client_socket, "200 OK", mime_type, file_buffer, log_file_name);
            } else if (file_size == 0) { // Obsługa pustych plików
                 std::string mime_type = get_mime_type(file_path);
                 log_message("IP: " + std::string(client_ip_str) + " - Serving empty file: " + file_path + " (" + mime_type + ", 0 bytes)", log_file_name);
                 send_file_response(client_socket, "200 OK", mime_type, file_buffer, log_file_name); // file_buffer będzie pusty
            }else {
                log_message("IP: " + std::string(client_ip_str) + " - Could not read file (500): " + file_path + " (read error or negative size)", log_file_name, true);
                send_response(client_socket, "500 Internal Server Error", "text/html", "<html><body><h1>500 Internal Server Error</h1><p>Could not read file: " + requested_uri + "</p></body></html>", log_file_name);
            }
            requested_file_stream.close();
        }
    }
    close(client_socket); // Zamknij gniazdo klienta po obsłużeniu żądania
}


// --- Główna funkcja serwera ---
int main() {
    int server_fd;
    struct sockaddr_in address;
    const std::string www_root = "www"; // Katalog główny dla plików WWW
    const std::string log_file_name = "server.log"; // Nazwa pliku logu

    log_message("Server starting...", log_file_name);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        // Użyj perror dla błędów krytycznych przy starcie, zanim log_message będzie w pełni operacyjne
        // lub jeśli log_message samo w sobie zawiedzie.
        perror("FATAL: socket creation failed"); 
        return 1;
    }
    log_message("Socket created successfully.", log_file_name);

    // Umożliwienie ponownego użycia adresu (przydatne przy szybkim restarcie serwera)
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("FATAL: setsockopt failed");
        close(server_fd);
        return 1;
    }
    log_message("Socket options set (SO_REUSEADDR, SO_REUSEPORT).", log_file_name);

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Nasłuchuj na wszystkich interfejsach
    address.sin_port = htons(8080);       // Port serwera

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("FATAL: bind failed");
        close(server_fd);
        return 1;
    }
    log_message("Socket bound to port 8080.", log_file_name);

    if (listen(server_fd, 10) < 0) { // 10 to backlog - maksymalna długość kolejki oczekujących połączeń
        perror("FATAL: listen failed");
        close(server_fd);
        return 1;
    }
    log_message("Server listening on port 8080...", log_file_name);
    log_message("Serving files from directory: ./" + www_root, log_file_name);

    while (true) {
        struct sockaddr_in client_address_struct;
        socklen_t client_addr_len = sizeof(client_address_struct);
        int new_socket_fd = accept(server_fd, (struct sockaddr*)&client_address_struct, &client_addr_len);
        
        if (new_socket_fd < 0) {
            // Logujemy błąd accept, ale serwer kontynuuje pracę
            log_message("Error: accept failed: " + std::string(strerror(errno)), log_file_name, true);
            continue; // Kontynuuj, aby obsłużyć inne połączenia, zamiast zamykać serwer
        }
        
        handle_connection(new_socket_fd, client_address_struct, www_root, log_file_name);
        // Gniazdo klienta jest zamykane w handle_connection
    }

    log_message("Server shutting down...", log_file_name); // Teoretycznie nieosiągalne w obecnej pętli
    close(server_fd); // Zamknij gniazdo serwera przy wyjściu (teoretycznie)
    return 0;
}

