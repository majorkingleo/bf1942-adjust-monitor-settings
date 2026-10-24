#include <iostream>
#include <iomanip>
#include <string>
#include <set>
#include <cstring>
#include <chrono>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

namespace {

struct Beacon {
    std::string name;
    std::string guid;
};

// The LAN announce (UDP 54915 -> 255.255.255.255) is a NUL-terminated server
// name starting at byte 1, followed by opaque binary data that happens to
// contain the server GUID as a "{...}" string.
bool parse_beacon( const char * data, size_t len, Beacon & out ) {
    if( len < 2 )
        return false;

    const size_t name_max = len - 1;
    size_t i = 0;
    while( i < name_max && data[1 + i] != '\0' )
        ++i;
    out.name.assign( data + 1, i );

    const char * g = static_cast<const char *>( memchr( data, '{', len ) );
    if( g != nullptr ) {
        const char * e = static_cast<const char *>( memchr( g, '}', len - static_cast<size_t>( g - data ) ) );
        if( e != nullptr )
            out.guid.assign( g, static_cast<size_t>( e - g ) + 1 );
    }
    return !out.name.empty();
}

const char * kUsage =
    "usage: bf1942_discover [--port N] [--timeout SECONDS] [--csv]\n"
    "\n"
    "Passively listens for Battlefield 1942 LAN server beacons (the UDP\n"
    "broadcasts the server sends to 255.255.255.255) and prints each server's\n"
    "name, GUID and address as it is discovered.\n"
    "\n"
    "options:\n"
    "  --port N       beacon port (default 54915)\n"
    "  --timeout S    stop after S seconds instead of listening forever\n"
    "  --csv          machine-readable CSV output (name,guid,address)\n"
    "  --help         print this help\n"
    "  --version      print the version\n";

std::string ip_of( const sockaddr_in & addr ) {
    char buf[INET_ADDRSTRLEN];
    if( inet_ntop( AF_INET, &addr.sin_addr, buf, sizeof( buf ) ) == nullptr )
        return "?";
    return buf;
}

} // namespace

int main( int argc, char ** argv )
{
    try {
        int port = 54915;
        int timeout = 0;   // 0 = forever
        bool csv = false;

        for( int i = 1; i < argc; ++i ) {
            const std::string a = argv[i];
            if( a == "--help" || a == "-h" ) {
                std::cout << kUsage;
                return 0;
            } else if( a == "--version" || a == "-v" ) {
                std::cout << "bf1942_discover 1.0 (native BF1942 LAN beacon listener)\n";
                return 0;
            } else if( a == "--csv" ) {
                csv = true;
            } else if( a == "--port" ) {
                if( i + 1 >= argc )
                    throw std::runtime_error( "--port needs a value" );
                port = std::stoi( argv[++i] );
            } else if( a == "--timeout" ) {
                if( i + 1 >= argc )
                    throw std::runtime_error( "--timeout needs a value" );
                timeout = std::stoi( argv[++i] );
            } else {
                throw std::runtime_error( "unknown argument " + a );
            }
        }

#ifdef _WIN32
        WSADATA wsa;
        if( WSAStartup( MAKEWORD( 2, 2 ), &wsa ) != 0 )
            throw std::runtime_error( "WSAStartup() failed" );
        struct WsaGuard {
            ~WsaGuard() { WSACleanup(); }
        } wsa_guard;
#endif

        SOCKET sock = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
        if( sock == INVALID_SOCKET )
            throw std::runtime_error( "socket() failed" );

#ifdef _WIN32
        const BOOL reuse = TRUE;
        setsockopt( sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>( &reuse ), sizeof( reuse ) );
#else
        const int reuse = 1;
        setsockopt( sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
#endif

        sockaddr_in bind_addr = {};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_addr.s_addr = htonl( INADDR_ANY );
        bind_addr.sin_port = htons( static_cast<unsigned short>( port ) );
        if( bind( sock, reinterpret_cast<sockaddr *>( &bind_addr ), sizeof( bind_addr ) ) == SOCKET_ERROR ) {
            closesocket( sock );
            throw std::runtime_error( "cannot bind UDP " + std::to_string( port ) + " (already in use?)" );
        }

        if( csv )
            std::cout << "name,guid,address\n";
        else
            std::cout << "listening on UDP " << port << " for BF1942 LAN beacons...\n";

        std::set<std::string> seen;   // guid (fallback: name@ip) already reported
        const auto start = std::chrono::steady_clock::now();

        for( ;; ) {
            if( timeout > 0 ) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - start ).count();
                if( elapsed >= timeout )
                    break;   // total listen time reached
            }

            // Wait in 1 s slices so the total deadline is honoured even while
            // beacons keep arriving (a server announces itself about once a
            // second, so a plain per-receive timeout would never elapse).
            fd_set fds;
            FD_ZERO( &fds );
            FD_SET( sock, &fds );
            timeval tv = {};
            timeval * ptv = nullptr;
            if( timeout > 0 ) {
                tv.tv_sec = 1;
                ptv = &tv;
            }
            const int sel = select( static_cast<int>( sock ) + 1, &fds, nullptr, nullptr, ptv );
            if( sel <= 0 )
                continue;

            char buf[1500];
            sockaddr_in peer = {};
#ifdef _WIN32
            int peer_len = sizeof( peer );
#else
            socklen_t peer_len = sizeof( peer );
#endif
            int n = recvfrom( sock, buf, sizeof( buf ), 0,
                              reinterpret_cast<sockaddr *>( &peer ), &peer_len );
            if( n <= 0 )
                continue;

            Beacon b;
            if( !parse_beacon( buf, static_cast<size_t>( n ), b ) )
                continue;

            const std::string addr = ip_of( peer );
            const std::string key = !b.guid.empty() ? b.guid : ( b.name + "@" + addr );
            if( !seen.insert( key ).second )
                continue;

            if( csv ) {
                std::cout << b.name << "," << b.guid << "," << addr << "\n";
            } else {
                std::cout << "server " << b.name << "  guid " << b.guid
                          << "  at " << addr << "\n";
            }
            std::cout.flush();
        }

        closesocket( sock );

    } catch( const std::exception & error ) {
        std::cerr << "error: " << error.what() << std::endl;
        return 1;
    }

    return 0;
}
