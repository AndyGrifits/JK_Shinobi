// JK_Shinobi Steam link: play over the internet through Steam - no VPN, no router setup.
//
//   JK_Shinobi_Steam.exe host   - starts the game on the Create Game screen and lets Steam friends
//                                  who run "join" into it
//   JK_Shinobi_Steam.exe join   - finds a Steam friend who is hosting, starts the game connected to him
//
// Steam (logged in) must run on both PCs; the two of you must be Steam friends. It runs under
// Valve's test app 480 ("Spacewar" in your Steam status) and uses Steam's P2P messages, which go
// straight between the PCs when the routers allow it and through Valve's relays when they don't
// (DS-Lite, carrier NAT...).
//
// The game itself is the stock OpenJK and doesn't know: on the host this program stands in for
// each friend as a local UDP socket talking to the game's server (127.0.0.1:29070); on a joining
// PC it is the "server" the game connects to (127.0.0.1:29170), and every packet in either
// direction is carried over Steam.
//
// Built from tools/steamlink/build.sh against the Steamworks SDK headers (only the flat C API is
// used): Windows with MinGW (steam_api64.dll), macOS arm64 with zig (libsteam_api.dylib).

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef int socklen_t;
#define GAME_EXE "openjk.x86_64.exe"
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char **environ;
typedef int SOCKET;
typedef unsigned long DWORD;
typedef unsigned long u_long;
typedef unsigned short u_short;
#define INVALID_SOCKET  ( -1 )
#define MAX_PATH        PATH_MAX
#define closesocket     close
#define _stricmp        strcasecmp
#define GAME_EXE "openjk.arm64.app/Contents/MacOS/openjk.arm64"
static DWORD GetTickCount( void ) {
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (DWORD)( ts.tv_sec * 1000 + ts.tv_nsec / 1000000 );
}
static void Sleep( int ms ) { usleep( ms * 1000 ); }
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "steam/steam_api.h"
#include "steam/steam_api_flat.h"

static const uint32 kAppId = 480;              // Valve's Spacewar test app
static const char  *kPresenceKey = "jks";      // our rich presence: "host" while hosting
static const int    kGamePort = 29070;         // the game's server (net_port)
static const int    kJoinPort = 29170;         // what a joining game connects to (this program)
static const int    kChannel = 0;
static const int    kSendFlags = k_nSteamNetworkingSend_UnreliableNoNagle | k_nSteamNetworkingSend_AutoRestartBrokenSession;

static ISteamNetworkingMessages *g_msgs;
static ISteamFriends            *g_friends;
static HSteamPipe                g_pipe;
#ifdef _WIN32
static PROCESS_INFORMATION       g_game;
#else
static pid_t                     g_game;
#endif
static char                      g_dir[MAX_PATH];

static void Log( const char *fmt, ... ) {
	char    buf[1024];
	va_list ap;
	time_t  t = time( NULL );
	struct tm *lt = localtime( &t );
	va_start( ap, fmt );
	vsnprintf( buf, sizeof( buf ), fmt, ap );
	va_end( ap );
	printf( "[%02d:%02d:%02d] %s\n", lt->tm_hour, lt->tm_min, lt->tm_sec, buf );
	fflush( stdout );
}

static void Identity( SteamNetworkingIdentity *id, uint64 steamId ) {
	memset( id, 0, sizeof( *id ) );
	SteamAPI_SteamNetworkingIdentity_SetSteamID64( id, steamId );
}

// ---------------------------------------------------------------- the game process

#ifdef _WIN32
static bool StartGame( const char *extra ) {
	char cmd[2048];
	STARTUPINFOA si;
	snprintf( cmd, sizeof( cmd ),
		"\"%s\\" GAME_EXE "\" +set fs_game shinobi +set logfile 2 +set r_fullscreen 0 +set r_mode -2 +set r_noborder 1 "
		"+exec shinobi_binds.cfg %s", g_dir, extra );
	memset( &si, 0, sizeof( si ) );
	si.cb = sizeof( si );
	if ( !CreateProcessA( NULL, cmd, NULL, NULL, FALSE, 0, NULL, g_dir, &si, &g_game ) ) {
		Log( "Could not start the game (" GAME_EXE " next to this program?) - error %lu", GetLastError() );
		return false;
	}
	return true;
}

static bool GameRunning( void ) {
	return g_game.hProcess && WaitForSingleObject( g_game.hProcess, 0 ) == WAIT_TIMEOUT;
}
#else
static bool StartGame( const char *extra ) { // extra: "+cmd arg arg +cmd ..." split on spaces
	static char  words[2048];
	char        *argv[96], *w;
	int          argc = 0, rc;
	const char  *fixed[] = { GAME_EXE, "+set", "fs_game", "shinobi", "+set", "logfile", "2", "+set", "r_fullscreen", "1",
	                         "+set", "r_mode", "-2", "+exec", "shinobi_binds.cfg" };
	for ( size_t k = 0; k < sizeof( fixed ) / sizeof( fixed[0] ); k++ )
		argv[argc++] = (char *)fixed[k];
	snprintf( words, sizeof( words ), "%s", extra );
	for ( w = strtok( words, " " ); w && argc < 95; w = strtok( NULL, " " ) )
		argv[argc++] = w;
	argv[argc] = NULL;
	// the game's own console output stays out of this window (it is in qconsole.log)
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init( &fa );
	posix_spawn_file_actions_addopen( &fa, 1, "/dev/null", O_WRONLY, 0 );
	posix_spawn_file_actions_addopen( &fa, 2, "/dev/null", O_WRONLY, 0 );
	rc = posix_spawn( &g_game, GAME_EXE, &fa, NULL, argv, environ );
	posix_spawn_file_actions_destroy( &fa );
	if ( rc != 0 ) {
		Log( "Could not start the game (%s next to this program?) - %s", GAME_EXE, strerror( rc ) );
		g_game = 0;
		return false;
	}
	return true;
}

static bool GameRunning( void ) {
	int st;
	return g_game > 0 && waitpid( g_game, &st, WNOHANG ) == 0;
}
#endif

// ---------------------------------------------------------------- Steam callbacks

typedef void ( *SessionFn )( SteamNetworkingIdentity *who );

static void PumpCallbacks( SessionFn onRequest ) {
	CallbackMsg_t msg;
	SteamAPI_ManualDispatch_RunFrame( g_pipe );
	while ( SteamAPI_ManualDispatch_GetNextCallback( g_pipe, &msg ) ) {
		if ( msg.m_iCallback == SteamNetworkingMessagesSessionRequest_t::k_iCallback ) {
			SteamNetworkingMessagesSessionRequest_t *req = (SteamNetworkingMessagesSessionRequest_t *)msg.m_pubParam;
			if ( onRequest )
				onRequest( &req->m_identityRemote );
		}
		else if ( msg.m_iCallback == SteamNetworkingMessagesSessionFailed_t::k_iCallback ) {
			SteamNetworkingMessagesSessionFailed_t *f = (SteamNetworkingMessagesSessionFailed_t *)msg.m_pubParam;
			Log( "Steam: the link to %s failed (%s) - Steam retries on its own",
				SteamAPI_ISteamFriends_GetFriendPersonaName( g_friends, SteamAPI_SteamNetworkingIdentity_GetSteamID64( &f->m_info.m_identityRemote ) ),
				f->m_info.m_szEndDebug );
		}
		SteamAPI_ManualDispatch_FreeLastCallback( g_pipe );
	}
}

static void DescribeLink( uint64 peer ) {
	SteamNetworkingIdentity          id;
	SteamNetConnectionInfo_t         info;
	SteamNetConnectionRealTimeStatus_t st;
	Identity( &id, peer );
	if ( SteamAPI_ISteamNetworkingMessages_GetSessionConnectionInfo( g_msgs, id, &info, &st ) == k_ESteamNetworkingConnectionState_Connected )
		Log( "Link to %s: ping %d ms, %s", SteamAPI_ISteamFriends_GetFriendPersonaName( g_friends, peer ), st.m_nPing,
			( info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Relayed ) ? "through Steam's relays" : "direct" );
}

// ---------------------------------------------------------------- sockets

static SOCKET UdpSocket( int port ) { // bound to 127.0.0.1 (port 0: any), non-blocking
	SOCKET      s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	sockaddr_in a;
	u_long      nb = 1;
	if ( s == INVALID_SOCKET )
		return s;
	memset( &a, 0, sizeof( a ) );
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
	a.sin_port = htons( (u_short)port );
	if ( bind( s, (sockaddr *)&a, sizeof( a ) ) != 0 ) {
		closesocket( s );
		return INVALID_SOCKET;
	}
#ifdef _WIN32
	ioctlsocket( s, FIONBIO, &nb );
#else
	(void)nb;
	fcntl( s, F_SETFL, fcntl( s, F_GETFL, 0 ) | O_NONBLOCK );
#endif
	return s;
}

static sockaddr_in Loopback( int port ) {
	sockaddr_in a;
	memset( &a, 0, sizeof( a ) );
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
	a.sin_port = htons( (u_short)port );
	return a;
}

// ---------------------------------------------------------------- host

struct Peer {
	uint64 steamId;
	SOCKET sock;   // this friend's stand-in towards the game's server
	DWORD  lastHeard;
	bool   described;
};
static Peer g_peers[32];
static int  g_numPeers;

static void HostAccept( SteamNetworkingIdentity *who ) {
	const uint64 id = SteamAPI_SteamNetworkingIdentity_GetSteamID64( who );
	const int    rel = id ? SteamAPI_ISteamFriends_GetFriendRelationship( g_friends, id ) : 0;
	if ( rel != k_EFriendRelationshipFriend ) {
		Log( "Ignored a link request from someone who is not your Steam friend (%llu)", (unsigned long long)id );
		return;
	}
	SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser( g_msgs, *who );
	Log( "%s is joining", SteamAPI_ISteamFriends_GetFriendPersonaName( g_friends, id ) );
}

static Peer *HostPeer( uint64 id ) {
	int i;
	for ( i = 0; i < g_numPeers; i++ )
		if ( g_peers[i].steamId == id )
			return &g_peers[i];
	if ( g_numPeers >= (int)( sizeof( g_peers ) / sizeof( g_peers[0] ) ) )
		return NULL;
	g_peers[g_numPeers].steamId = id;
	g_peers[g_numPeers].sock = UdpSocket( 0 );
	g_peers[g_numPeers].lastHeard = GetTickCount();
	g_peers[g_numPeers].described = false;
	if ( g_peers[g_numPeers].sock == INVALID_SOCKET )
		return NULL;
	return &g_peers[g_numPeers++];
}

// is the game's server up? ("getinfo" to it; its answer names the map) - friends only see us
// hosting once it is, so a joining game never knocks on an empty door
static SOCKET g_probe = INVALID_SOCKET;
static bool   g_serverUp;
static char   g_mapName[64];

static void ProbeServer( const sockaddr_in &server ) {
	static const char ask[] = "\xff\xff\xff\xffgetinfo jks";
	char              buf[4096];
	sockaddr_in       from;
	socklen_t         fl = sizeof( from );
	int               len;
	bool              answered = false;
	while ( ( len = recvfrom( g_probe, buf, sizeof( buf ) - 1, 0, (sockaddr *)&from, &fl ) ) > 0 ) {
		buf[len] = 0;
		if ( len > 4 && !strncmp( buf + 4, "infoResponse", 12 ) ) {
			const char *m = strstr( buf, "\\mapname\\" );
			answered = true;
			g_mapName[0] = 0;
			if ( m ) {
				int k = 0;
				m += 9;
				while ( *m && *m != '\\' && k < (int)sizeof( g_mapName ) - 1 )
					g_mapName[k++] = *m++;
				g_mapName[k] = 0;
			}
		}
		fl = sizeof( from );
	}
	if ( answered != g_serverUp ) {
		g_serverUp = answered;
		if ( answered ) {
			char status[128];
			snprintf( status, sizeof( status ), "JK_Shinobi - hosting %s", g_mapName );
			SteamAPI_ISteamFriends_SetRichPresence( g_friends, kPresenceKey, "host" );
			SteamAPI_ISteamFriends_SetRichPresence( g_friends, "status", status );
			Log( "Your game is up (%s) - friends can join now with JK_Shinobi_SteamJoin.", g_mapName );
		}
		else {
			SteamAPI_ISteamFriends_SetRichPresence( g_friends, kPresenceKey, "" );
			SteamAPI_ISteamFriends_SetRichPresence( g_friends, "status", "JK_Shinobi - setting up a game" );
			Log( "No game running (pick a map and press Start)." );
		}
	}
	sendto( g_probe, ask, sizeof( ask ) - 1, 0, (const sockaddr *)&server, sizeof( server ) );
}

static int RunHost( void ) {
	const sockaddr_in server = Loopback( kGamePort );
	DWORD             nextInfo = 0, nextProbe = 0;
	g_probe = UdpSocket( 0 );
	SteamAPI_ISteamFriends_SetRichPresence( g_friends, "status", "JK_Shinobi - setting up a game" );
	if ( !StartGame( "+set g_forcePowerDisable 0 +set g_maxForceRank 7 +set bot_minplayers 0 +set sv_maxclients 8 +wait 30 +ui_openmenu createserver" ) )
		return 1;
	Log( "Hosting. Pick the map in the game and press Start - then your Steam friends join with JK_Shinobi_SteamJoin." );
	Log( "Keep this window open while you play (it closes with the game)." );
	while ( GameRunning() ) {
		if ( GetTickCount() >= nextProbe ) {
			nextProbe = GetTickCount() + 2000;
			ProbeServer( server );
		}
		SteamNetworkingMessage_t *in[64];
		char                      buf[16384];
		int                       n, i;
		PumpCallbacks( HostAccept );
		// friends -> the game's server
		while ( ( n = SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel( g_msgs, kChannel, in, 64 ) ) > 0 ) {
			for ( i = 0; i < n; i++ ) {
				Peer *p = HostPeer( SteamAPI_SteamNetworkingIdentity_GetSteamID64( &in[i]->m_identityPeer ) );
				if ( p ) {
					p->lastHeard = GetTickCount();
					sendto( p->sock, (const char *)in[i]->m_pData, in[i]->m_cbSize, 0, (const sockaddr *)&server, sizeof( server ) );
				}
				SteamAPI_SteamNetworkingMessage_t_Release( in[i] );
			}
		}
		// the game's server -> friends
		for ( i = 0; i < g_numPeers; i++ ) {
			sockaddr_in from;
			socklen_t   fl = sizeof( from );
			int         len;
			while ( ( len = recvfrom( g_peers[i].sock, buf, sizeof( buf ), 0, (sockaddr *)&from, &fl ) ) > 0 ) {
				SteamNetworkingIdentity id;
				Identity( &id, g_peers[i].steamId );
				SteamAPI_ISteamNetworkingMessages_SendMessageToUser( g_msgs, id, buf, (uint32)len, kSendFlags, kChannel );
				fl = sizeof( from );
			}
		}
		if ( GetTickCount() >= nextInfo ) { // once a friend is in: how the link goes
			nextInfo = GetTickCount() + 3000;
			for ( i = 0; i < g_numPeers; i++ )
				if ( !g_peers[i].described ) {
					g_peers[i].described = true;
					DescribeLink( g_peers[i].steamId );
				}
		}
		Sleep( 1 );
	}
	SteamAPI_ISteamFriends_ClearRichPresence( g_friends );
	Log( "The game ended." );
	return 0;
}

// ---------------------------------------------------------------- join

static uint64 g_hostId;

static void JoinAccept( SteamNetworkingIdentity *who ) {
	if ( SteamAPI_SteamNetworkingIdentity_GetSteamID64( who ) == g_hostId )
		SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser( g_msgs, *who );
}

// friends running app 480 who say they host (asks Steam for their rich presence, waits a little)
static int FindHosts( uint64 *out, int max ) {
	const int count = SteamAPI_ISteamFriends_GetFriendCount( g_friends, k_EFriendFlagImmediate );
	uint64    playing[256];
	int       np = 0, nh = 0, i;
	DWORD     until;
	for ( i = 0; i < count && np < 256; i++ ) {
		const uint64     f = SteamAPI_ISteamFriends_GetFriendByIndex( g_friends, i, k_EFriendFlagImmediate );
		FriendGameInfo_t gi;
		if ( SteamAPI_ISteamFriends_GetFriendGamePlayed( g_friends, f, &gi ) && gi.m_gameID.AppID() == kAppId ) {
			playing[np++] = f;
			SteamAPI_ISteamFriends_RequestFriendRichPresence( g_friends, f );
		}
	}
	until = GetTickCount() + 2500;
	while ( GetTickCount() < until ) {
		PumpCallbacks( NULL );
		Sleep( 20 );
	}
	for ( i = 0; i < np && nh < max; i++ ) {
		const char *v = SteamAPI_ISteamFriends_GetFriendRichPresence( g_friends, playing[i], kPresenceKey );
		if ( v && !strcmp( v, "host" ) )
			out[nh++] = playing[i];
	}
	return nh;
}

static int RunJoin( void ) {
	uint64      hosts[16];
	int         n = 0, port, i;
	SOCKET      local = INVALID_SOCKET;
	sockaddr_in game;
	bool        haveGame = false, described = false;
	DWORD       started;
	char        connect[64];

	SteamAPI_ISteamFriends_SetRichPresence( g_friends, "status", "JK_Shinobi - joining" );
	Log( "Looking for a Steam friend who is hosting..." );
	for ( ;; ) {
		n = FindHosts( hosts, 16 );
		if ( n > 0 )
			break;
		Log( "Nobody is hosting yet (your friend starts JK_Shinobi_SteamHost) - looking again..." );
	}
	if ( n == 1 )
		g_hostId = hosts[0];
	else {
		char line[32];
		for ( i = 0; i < n; i++ )
			printf( "  %d. %s\n", i + 1, SteamAPI_ISteamFriends_GetFriendPersonaName( g_friends, hosts[i] ) );
		printf( "Whose game? " );
		fflush( stdout );
		i = fgets( line, sizeof( line ), stdin ) ? atoi( line ) : 1;
		g_hostId = hosts[( i >= 1 && i <= n ) ? i - 1 : 0];
	}
	Log( "Joining %s", SteamAPI_ISteamFriends_GetFriendPersonaName( g_friends, g_hostId ) );
	for ( port = kJoinPort; port < kJoinPort + 20 && local == INVALID_SOCKET; port++ )
		local = UdpSocket( port );
	if ( local == INVALID_SOCKET ) {
		Log( "Could not open a local port for the game" );
		return 1;
	}
	snprintf( connect, sizeof( connect ), "+connect 127.0.0.1:%d", port - 1 );
	if ( !StartGame( connect ) )
		return 1;
	started = GetTickCount();
	Log( "The game is starting and connects through Steam - keep this window open while you play." );
	while ( GameRunning() ) {
		SteamNetworkingMessage_t *in[64];
		char                      buf[16384];
		int                       len;
		socklen_t                 fl;
		sockaddr_in               from;
		PumpCallbacks( JoinAccept );
		// the game -> the host
		fl = sizeof( from );
		while ( ( len = recvfrom( local, buf, sizeof( buf ), 0, (sockaddr *)&from, &fl ) ) > 0 ) {
			SteamNetworkingIdentity id;
			game = from;
			haveGame = true;
			Identity( &id, g_hostId );
			SteamAPI_ISteamNetworkingMessages_SendMessageToUser( g_msgs, id, buf, (uint32)len, kSendFlags, kChannel );
			fl = sizeof( from );
		}
		// the host -> the game
		while ( ( len = SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel( g_msgs, kChannel, in, 64 ) ) > 0 ) {
			for ( i = 0; i < len; i++ ) {
				if ( haveGame && SteamAPI_SteamNetworkingIdentity_GetSteamID64( &in[i]->m_identityPeer ) == g_hostId )
					sendto( local, (const char *)in[i]->m_pData, in[i]->m_cbSize, 0, (const sockaddr *)&game, sizeof( game ) );
				SteamAPI_SteamNetworkingMessage_t_Release( in[i] );
			}
		}
		if ( !described && GetTickCount() - started > 5000 ) {
			described = true;
			DescribeLink( g_hostId );
		}
		Sleep( 1 );
	}
	SteamAPI_ISteamFriends_ClearRichPresence( g_friends );
	Log( "The game ended." );
	return 0;
}

// ---------------------------------------------------------------- main

int main( int argc, char **argv ) {
	SteamErrMsg err;
	char       *slash;
	int         mode = 0, rc;

#ifdef _WIN32
	WSADATA wsa;
	GetModuleFileNameA( NULL, g_dir, sizeof( g_dir ) );
	if ( ( slash = strrchr( g_dir, '\\' ) ) != NULL )
		*slash = 0;
	SetCurrentDirectoryA( g_dir );
	SetConsoleTitleA( "JK_Shinobi - Steam link" );
#else
	{
		char     exe[PATH_MAX];
		uint32_t sz = sizeof( exe );
		if ( _NSGetExecutablePath( exe, &sz ) != 0 || !realpath( exe, g_dir ) )
			snprintf( g_dir, sizeof( g_dir ), "%s", argv[0] );
		if ( ( slash = strrchr( g_dir, '/' ) ) != NULL )
			*slash = 0;
		if ( chdir( g_dir ) != 0 )
			Log( "Could not enter %s", g_dir );
		signal( SIGPIPE, SIG_IGN );
	}
#endif

	if ( argc > 1 && !_stricmp( argv[1], "host" ) )
		mode = 1;
	else if ( argc > 1 && !_stricmp( argv[1], "join" ) )
		mode = 2;
	else {
		char line[16];
		printf( "JK_Shinobi over Steam\n  1. Host a game\n  2. Join a friend's game\nChoose 1 or 2: " );
		fflush( stdout );
		mode = ( fgets( line, sizeof( line ), stdin ) && line[0] == '1' ) ? 1 : 2;
	}

	{ // run as Valve's test app (Spacewar): the file next to us and the variable, both
		FILE *f = fopen( "steam_appid.txt", "w" );
		if ( f ) {
			fprintf( f, "%u", kAppId );
			fclose( f );
		}
#ifdef _WIN32
		SetEnvironmentVariableA( "SteamAppId", "480" );
		SetEnvironmentVariableA( "SteamGameId", "480" );
#else
		setenv( "SteamAppId", "480", 1 );
		setenv( "SteamGameId", "480", 1 );
#endif
	}
	if ( SteamAPI_InitFlat( &err ) != k_ESteamAPIInitResult_OK ) {
		Log( "Steam is not available: %s", err );
		Log( "Start Steam and log in, then run this again." );
		printf( "Press Enter to close." );
		getchar();
		return 1;
	}
	SteamAPI_ManualDispatch_Init();
	g_pipe = SteamAPI_GetHSteamPipe();
	g_msgs = SteamAPI_SteamNetworkingMessages_SteamAPI_v002();
	g_friends = SteamAPI_SteamFriends_v018();
	SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess( SteamAPI_SteamNetworkingUtils_SteamAPI_v004() );
#ifdef _WIN32
	WSAStartup( MAKEWORD( 2, 2 ), &wsa );
#endif
	Log( "Steam: logged in as %s", SteamAPI_ISteamFriends_GetPersonaName( g_friends ) );

	rc = mode == 1 ? RunHost() : RunJoin();

	SteamAPI_Shutdown();
#ifdef _WIN32
	WSACleanup();
#endif
	return rc;
}
