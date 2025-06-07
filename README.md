# Program własnego serwera działający na porcie 8080

1. Program napisany w c++, dla systemu Linux
2. Polecenie kompilacji: g++ start.cpp -o start
3. Polecenie startu programu: ./start

Po uruchomieniu programu, sprawdzane jest czy istnieje katalog www,
jeśli nie to program go utworzy. W tym katalogu umieść swoją stronę.

## Opis działania programu:
Serwer jest zbudowany w sposób modułowy, gdzie każda główna funkcja odpowiada za określony fragment logiki.

1. Funkcja main() - Serce serwera:

- Inicjalizacja:
Definiuje podstawowe stałe, takie jak www_root (katalog główny dla plików WWW) i log_file_name (nazwa pliku logu).
Wywołuje log_message() do zapisania informacji o starcie serwera.
Sprawdzanie/Tworzenie katalogu www: To ważny krok dodany ostatnio. Używa funkcji systemowych stat() (do sprawdzenia, czy katalog istnieje i czy jest katalogiem) oraz mkdir() (do utworzenia katalogu z uprawnieniami 0755, jeśli nie istnieje).     Jeśli katalog nie istnieje i nie da się go utworzyć, lub jeśli ścieżka www_root istnieje, ale nie jest katalogiem, serwer kończy pracę z błędem.

1.2. Konfiguracja gniazda (socket):
socket(AF_INET, SOCK_STREAM, 0): Tworzy gniazdo sieciowe. AF_INET oznacza użycie protokołu IPv4, SOCK_STREAM oznacza gniazdo strumieniowe (dla TCP).
setsockopt(...): Ustawia opcje gniazda. SO_REUSEADDR i SO_REUSEPORT pozwalają na natychmiastowe ponowne użycie adresu i portu po zamknięciu serwera, co jest przydatne podczas dewelopmentu.
address.sin_family = AF_INET; address.sin_addr.s_addr = INADDR_ANY; address.sin_port = htons(8080);: Konfiguruje strukturę adresu serwera. INADDR_ANY oznacza, że serwer będzie nasłuchiwał na wszystkich dostępnych interfejsach sieciowych maszyny, a htons(8080) ustawia port nasłuchu na 8080 (konwertując go na sieciowy porządek bajtów).
bind(...): Przypisuje utworzone gniazdo do skonfigurowanego adresu i portu.
listen(...): Przełącza gniazdo w tryb nasłuchu, gotowe do akceptowania przychodzących połączeń. 10 to tzw. "backlog" – maksymalna liczba połączeń oczekujących w kolejce.

1.3. Główna pętla serwera (while (true)):
accept(...): To blokująca funkcja, która czeka na nowe połączenie od klienta. Gdy połączenie nadejdzie, accept tworzy nowe gniazdo (new_socket_fd) dedykowane do komunikacji z tym konkretnym klientem i wypełnia strukturę client_address_struct informacjami o adresie klienta.
handle_connection(...): Jeśli accept zakończy się sukcesem, wywoływana jest ta funkcja, aby obsłużyć żądanie od połączonego klienta. Przekazywane jest do niej gniazdo klienta, informacje o jego adresie oraz ścieżka do www_root i nazwa pliku logu.

1.4. Zamykanie (teoretyczne): Linijki log_message("Server shutting down...", ...) i close(server_fd) są w obecnej formie nieosiągalne, ponieważ pętla while(true) jest nieskończona. W bardziej rozbudowanym serwerze istniałby mechanizm do eleganckiego zamknięcia serwera (np. po otrzymaniu sygnału).

2. Funkcja handle_connection() - Obsługa pojedynczego klienta:

2.1 Pobranie IP klienta: inet_ntop(...) konwertuje adres IP klienta z postaci binarnej na tekstową.
Odczyt żądania:
read(client_socket, buffer, ...): Odczytuje dane wysłane przez klienta (żądanie HTTP) do bufora.
Obsługuje błędy odczytu lub sytuację, gdy klient rozłączył się przed wysłaniem danych.
2.2 Wstępna walidacja i parsowanie żądania:
Sprawdza, czy pierwsza linia żądania (np. GET /index.html HTTP/1.1) nie jest za długa i czy zawiera znak końca linii (\r\n).
std::istringstream request_stream(request_str): Używa strumienia do łatwiejszego wyodrębnienia metody (np. GET), żądanego URI (np. /index.html) i wersji HTTP.
Loguje pełną pierwszą linię żądania.
Sprawdza, czy URI nie przekracza MAX_URI_LENGTH.
Sprawdza, czy parsowanie się powiodło i czy wszystkie trzy komponenty (metoda, URI, wersja) zostały odczytane. Jeśli nie, wysyła odpowiedź 400 Bad Request.
Walidacja znaków w URI: Iteruje po znakach w requested_uri i sprawdza, czy są to dozwolone znaki (alfanumeryczne, -, _, ., /). Jeśli nie, wysyła 400 Bad Request.
2.3 Obsługa metody HTTP:
Obecnie obsługuje tylko metodę GET. Dla innych metod wysyła 501 Not Implemented.
2.4 Konstrukcja ścieżki do pliku:
Jeśli requested_uri to / lub jest puste, domyślnie ustawia ścieżkę na www_root + "/index.html".
W przeciwnym razie:
Ochrona przed Path Traversal: Sprawdza, czy requested_uri zawiera ... Jeśli tak, wysyła 403 Forbidden.
Łączy www_root z requested_uri tworząc pełną ścieżkę do pliku.
2.5 Serwowanie pliku:
std::ifstream requested_file_stream(...): Otwiera żądany plik w trybie binarnym (std::ios::binary) i ustawia wskaźnik na koniec pliku (std::ios::ate), aby łatwo uzyskać jego rozmiar.
Jeśli pliku nie da się otworzyć, wysyła 404 Not Found.
Jeśli plik jest otwarty:
requested_file_stream.tellg(): Pobiera rozmiar pliku.
requested_file_stream.seekg(0, std::ios::beg): Przesuwa wskaźnik z powrotem na początek pliku.
std::vector<char> file_buffer(...): Tworzy bufor (wektor znaków) o rozmiarze pliku.
requested_file_stream.read(...): Wczytuje zawartość pliku do bufora.
Jeśli odczyt się powiedzie (lub plik jest pusty), wywołuje get_mime_type() aby określić typ zawartości i send_file_response() aby wysłać plik do klienta z kodem 200 OK.
Jeśli odczyt się nie powiedzie, wysyła 500 Internal Server Error.
requested_file_stream.close(): Zamyka strumień pliku.
2.6Zamykanie gniazda klienta: close(client_socket) zamyka połączenie z klientem po obsłużeniu jego żądania.

3. Funkcja log_message() - Rejestrowanie zdarzeń:

3.1 Pobiera aktualny czas systemowy za pomocą std::chrono.
3.2 Formatuje czas do postaci YYYY-MM-DD HH:MM:SS za pomocą std::put_time.
3.3Tworzy pełną wiadomość logu, łącząc znacznik czasu z przekazaną wiadomością.
3.4 Jeśli is_error jest true, wypisuje wiadomość na std::cerr (standardowe wyjście błędów), w przeciwnym razie na std::cout (standardowe wyjście).
3.5 Otwiera plik logu (log_file_name) w trybie dopisywania (std::ios::app).
3.6 Zapisuje pełną wiadomość logu do pliku.
3.7Zamyka plik logu.
3.8Obsługuje błąd, jeśli nie uda się otworzyć pliku logu (wypisuje wtedy krytyczny błąd tylko na std::cerr).

4. Funkcje send_response() i send_file_response() - Wysyłanie odpowiedzi HTTP:

4.1 Obie funkcje budują odpowiedź HTTP jako string.
4.2 send_response(): Służy do wysyłania prostych odpowiedzi tekstowych (np. stron błędów).
Składa nagłówki: HTTP/1.1 <status_code>, Content-Type, Content-Length, Connection: close. Connection: close informuje klienta, że serwer zamknie połączenie po wysłaniu odpowiedzi, co upraszcza zarządzanie połączeniami w tym prostym serwerze.
Dołącza treść (body) odpowiedzi.
4.3 send_file_response(): Służy do wysyłania zawartości plików.
Składa nagłówki podobnie jak send_response(), używając rozmiaru file_data dla Content-Length.
Najpierw wysyła same nagłówki.
Następnie, jeśli file_data nie jest puste, wysyła zawartość pliku (dane binarne z wektora).
4.4 Obie funkcje używają write() do wysłania danych przez gniazdo. Sprawdzają, czy write() nie zwrócił błędu i czy wszystkie dane zostały wysłane, logując ewentualne problemy.

5. Funkcja get_mime_type() - Określanie typu zawartości:

5.1 Na podstawie rozszerzenia pliku (np. .html, .css, .jpg) zwraca odpowiedni typ MIME (np. text/html, text/css, image/jpeg).
5.2 Najpierw znajduje ostatnią kropkę w nazwie pliku, aby wyodrębnić rozszerzenie.
5.3 Konwertuje rozszerzenie na małe litery, aby dopasowanie było niezależne od wielkości liter.
5.4 Używa serii instrukcji if do porównania rozszerzenia ze znanymi typami.
5.5 Jeśli rozszerzenie nie pasuje do żadnego znanego typu, zwraca domyślny typ application/octet-stream (co oznacza "dowolne dane binarne").

Program wykorzystuje standardowe biblioteki C++ oraz funkcje systemowe POSIX (głównie dla operacji sieciowych i plikowych), co czyni go przenośnym między systemami typu Unix (jak Linux). Każdy "moduł" (funkcja) ma jasno zdefiniowaną odpowiedzialność, co przyczynia się do czytelności i łatwości w utrzymaniu kodu.
