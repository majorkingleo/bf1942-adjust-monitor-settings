#include <iostream>
#include <iomanip>
#include <string>
#include <map>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#define ADDR_LEN int
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#define ADDR_LEN socklen_t
#endif

namespace {

struct Options {
    std::string host;
    int port = 23000;
    int interval = 0;   // 0 means a single query
    bool csv = false;
};

const char * kUsage =
    "usage: bf1942_query <host> [--port N] [--interval SECONDS] [--csv]\n"
    "\n"
    "Queries a Battlefield 1942 server's GameSpy/ASE status port (UDP) and\n"
    "prints the live scoreboard: score, kills, deaths, ping and team per player.\n"
    "\n"
    "options:\n"
    "  --port N       query port (default 23000; see the server console for the\n"
    "                 actual 'GameSpy query port:' value)\n"
    "  --interval S   poll every S seconds instead of doing a single query\n"
    "  --csv          machine-readable CSV output (one row per player)\n"
    "  --help         print this help\n"
    "  --version      print the version\n";

std::string timestamp() {
    std::time_t t = std::time( nullptr );
    std::tm tmv{};
#ifdef _WIN32
    localtime_s( &tmv, &t );
#else
    localtime_r( &t, &tmv );
#endif
    char buf[32];
    std::strftime( buf, sizeof( buf ), "%Y-%m-%d %H:%M:%S", &tmv );
    return buf;
}

int to_int( const std::string & s ) {
    try { return std::stoi( s ); } catch( ... ) { return 0; }
}

std::string get_field( const std::map<std::string,std::string> & fields, const std::string & key ) {
    auto it = fields.find( key );
    return it == fields.end() ? std::string() : it->second;
}

std::string csv_escape( const std::string & s ) {
    if( s.find_first_of( ",\"\n" ) == std::string::npos )
        return s;
    std::string out = "\"";
    for( char c : s ) {
        if( c == '"' ) out += "\"\"";
        else out += c;
    }
    out += "\"";
    return out;
}

std::map<std::string,std::string> parse_fields( const std::string & response ) {
    std::map<std::string,std::string> fields;

    // The response is a backslash-delimited stream: \key\value\key\value\...
    // It may start with a single leading byte on some implementations, so find
    // the first backslash rather than assuming index 0.
    size_t i = response.find( '\\' );
    if( i == std::string::npos )
        return fields;
    ++i;   // skip the opening backslash

    while( i < response.size() ) {
        size_t key_end = response.find( '\\', i );
        std::string key;
        if( key_end == std::string::npos ) {
            key = response.substr( i );
            i = response.size();
        } else {
            key = response.substr( i, key_end - i );
            i = key_end + 1;
        }

        if( i >= response.size() ) {
            if( !key.empty() )
                fields[key] = "";
            break;
        }

        size_t val_end = response.find( '\\', i );
        std::string val;
        if( val_end == std::string::npos ) {
            val = response.substr( i );
            i = response.size();
        } else {
            val = response.substr( i, val_end - i );
            i = val_end + 1;
        }
        fields[key] = val;
    }
    return fields;
}

std::map<std::string,std::string> query( const std::string & host, int port ) {
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    const std::string port_str = std::to_string( port );
    addrinfo * result = nullptr;
    int rc = getaddrinfo( host.c_str(), port_str.c_str(), &hints, &result );
    if( rc != 0 )
        throw std::runtime_error( "cannot resolve " + host + ": " + gai_strerror( rc ) );

    addrinfo * ai = result;
    while( ai != nullptr && ai->ai_family != AF_INET )
        ai = ai->ai_next;
    if( ai == nullptr ) {
        freeaddrinfo( result );
        throw std::runtime_error( "no IPv4 address for " + host );
    }

    SOCKET sock = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    if( sock == INVALID_SOCKET ) {
        freeaddrinfo( result );
        throw std::runtime_error( "socket() failed" );
    }

#ifdef _WIN32
    DWORD timeout_ms = 3000;
    setsockopt( sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>( &timeout_ms ), sizeof( timeout_ms ) );
#else
    timeval timeout = { 3, 0 };
    setsockopt( sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof( timeout ) );
#endif

    const char request[] = "\\status\\";
    int sent = sendto( sock, request, static_cast<int>( strlen( request ) ), 0,
                       ai->ai_addr, static_cast<ADDR_LEN>( ai->ai_addrlen ) );
    if( sent == SOCKET_ERROR ) {
        closesocket( sock );
        freeaddrinfo( result );
        throw std::runtime_error( "sendto() failed" );
    }

    std::string response;
    char buf[4096];
    for( ;; ) {
        int n = recvfrom( sock, buf, sizeof( buf ), 0, nullptr, nullptr );
        if( n <= 0 )
            break;   // timeout, closed, or error
        response.append( buf, n );
        if( response.find( "\\queryid\\" ) != std::string::npos ||
            response.find( "\\final\\" ) != std::string::npos )
            break;
        if( response.size() > 64 * 1024 )
            break;
    }

    closesocket( sock );
    freeaddrinfo( result );

    if( response.empty() )
        throw std::runtime_error( "no response from " + host + ":" + port_str +
                                  " (query disabled or wrong port?)" );
    return parse_fields( response );
}

void print_snapshot( const std::map<std::string,std::string> & f, bool csv, bool with_time ) {
    const int numplayers = to_int( get_field( f, "numplayers" ) );
    const std::string hostname = get_field( f, "hostname" );
    const std::string mapname = get_field( f, "mapname" );
    const std::string gametype = get_field( f, "gametype" );
    const int maxplayers = to_int( get_field( f, "maxplayers" ) );
    const std::string ts = with_time ? timestamp() : std::string();

    // BF1942 spells the player name field "playername_<n>", and "numplayers"
    // can be smaller than the actual list, so scan until the first gap.
    const int cap = maxplayers > 0 ? maxplayers : 128;

    if( csv ) {
        const std::string tscol = with_time ? ts + "," : "";
        for( int i = 0; i < cap; ++i ) {
            const std::string name = get_field( f, "playername_" + std::to_string( i ) );
            if( name.empty() )
                break;
            std::cout << tscol
                      << csv_escape( hostname ) << ","
                      << csv_escape( mapname ) << ","
                      << csv_escape( gametype ) << ","
                      << numplayers << ","
                      << maxplayers << ","
                      << csv_escape( name ) << ","
                      << get_field( f, "score_" + std::to_string( i ) ) << ","
                      << get_field( f, "kills_" + std::to_string( i ) ) << ","
                      << get_field( f, "deaths_" + std::to_string( i ) ) << ","
                      << get_field( f, "ping_" + std::to_string( i ) ) << ","
                      << get_field( f, "team_" + std::to_string( i ) ) << "\n";
        }
        return;
    }

    if( with_time )
        std::cout << ts << "  ";
    std::cout << hostname << " | map=" << ( mapname.empty() ? "?" : mapname )
              << " | type=" << ( gametype.empty() ? "?" : gametype )
              << " | " << numplayers << "/" << maxplayers << " players\n";

    std::cout << std::left  << std::setw( 24 ) << "player"
              << std::right << std::setw( 7 )  << "score"
              << std::setw( 6 )  << "kills"
              << std::setw( 7 )  << "deaths"
              << std::setw( 6 )  << "ping"
              << std::setw( 5 )  << "team"
              << "\n";
    std::cout << std::string( 55, '-' ) << "\n";

    for( int i = 0; i < cap; ++i ) {
        const std::string name = get_field( f, "playername_" + std::to_string( i ) );
        if( name.empty() )
            break;
        std::cout << std::left  << std::setw( 24 ) << name
                  << std::right << std::setw( 7 )  << get_field( f, "score_" + std::to_string( i ) )
                  << std::setw( 6 )  << get_field( f, "kills_" + std::to_string( i ) )
                  << std::setw( 7 )  << get_field( f, "deaths_" + std::to_string( i ) )
                  << std::setw( 6 )  << get_field( f, "ping_" + std::to_string( i ) )
                  << std::setw( 5 )  << get_field( f, "team_" + std::to_string( i ) )
                  << "\n";
    }
    std::cout << "\n";
}

Options parse_args( int argc, char ** argv ) {
    Options o;
    for( int i = 1; i < argc; ++i ) {
        const std::string a = argv[i];
        if( a == "--help" || a == "-h" ) {
            std::cout << kUsage;
            std::exit( 0 );
        } else if( a == "--version" || a == "-v" ) {
            std::cout << "bf1942_query 1.0 (native BF1942 GameSpy/ASE query client)\n";
            std::exit( 0 );
        } else if( a == "--csv" ) {
            o.csv = true;
        } else if( a == "--port" ) {
            if( i + 1 >= argc )
                throw std::runtime_error( "--port needs a value" );
            o.port = std::stoi( argv[++i] );
        } else if( a == "--interval" ) {
            if( i + 1 >= argc )
                throw std::runtime_error( "--interval needs a value" );
            o.interval = std::stoi( argv[++i] );
        } else if( !a.empty() && a[0] == '-' ) {
            throw std::runtime_error( "unknown option " + a );
        } else if( o.host.empty() ) {
            o.host = a;
        } else {
            throw std::runtime_error( "unexpected argument " + a );
        }
    }
    return o;
}

} // namespace

int main( int argc, char ** argv )
{
    try {
        const Options opt = parse_args( argc, argv );
        if( opt.host.empty() ) {
            std::cerr << kUsage;
            return 1;
        }

#ifdef _WIN32
        WSADATA wsa;
        if( WSAStartup( MAKEWORD( 2, 2 ), &wsa ) != 0 )
            throw std::runtime_error( "WSAStartup() failed" );
        struct WsaGuard {
            ~WsaGuard() { WSACleanup(); }
        } wsa_guard;
#endif

        do {
            print_snapshot( query( opt.host, opt.port ), opt.csv, opt.interval > 0 );
            if( opt.interval > 0 )
                std::this_thread::sleep_for( std::chrono::seconds( opt.interval ) );
        } while( opt.interval > 0 );

    } catch( const std::exception & error ) {
        std::cerr << "error: " << error.what() << std::endl;
        return 1;
    }

    return 0;
}
