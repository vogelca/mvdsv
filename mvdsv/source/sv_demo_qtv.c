/*
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the included (GNU.txt) GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

    $Id: sv_demo_qtv.c,v 1.5 2007/05/18 10:20:50 qqshka Exp $
*/

//	sv_demo_qtv.c - misc QTV's code

#include "qwsvdef.h"

cvar_t	qtv_streamport		= {"qtv_streamport",	"0"};
cvar_t	qtv_maxstreams		= {"qtv_maxstreams",	"1"};
cvar_t	qtv_password		= {"qtv_password",		""};

static mvddest_t *SV_InitStream (int socket1)
{
	mvddest_t *dst;

	dst = (mvddest_t *) Q_malloc (sizeof(mvddest_t));

	dst->desttype = DEST_STREAM;
	dst->socket = socket1;
	dst->maxcachesize = 0x8000;	//is this too small?
	dst->cache = (char *) Q_malloc(dst->maxcachesize);

	SV_BroadcastPrintf (PRINT_CHAT, "Smile, you're on QTV!\n");

	return dst;
}

static void SV_MVD_InitPendingStream (int socket1, char *ip)
{
	mvdpendingdest_t *dst;
	unsigned int i;
	dst = (mvdpendingdest_t*) Q_malloc(sizeof(mvdpendingdest_t));
	dst->socket = socket1;

	strlcpy(dst->challenge, ip, sizeof(dst->challenge));
	for (i = strlen(dst->challenge); i < sizeof(dst->challenge)-1; i++)
		dst->challenge[i] = rand()%(127-33) + 33;	//generate a random challenge

	dst->nextdest = demo.pendingdest;
	demo.pendingdest = dst;
}

static int MVD_StreamStartListening (int port)
{
	int sock;

	struct sockaddr_in	address;
	//	int fromlen;

	unsigned long nonblocking = true;

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons((short)port);



	if ((sock = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
	{
		Sys_Error ("MVD_StreamStartListening: socket: (%i): %s\n", qerrno, strerror(qerrno));
	}

	if (ioctlsocket (sock, FIONBIO, &nonblocking) == -1)
	{
		closesocket(sock);
		Sys_Error ("MVD_StreamStartListening: ioctl FIONBIO: (%i): %s\n", qerrno, strerror(qerrno));
	}

	if( bind (sock, (struct sockaddr *)&address, sizeof(address)) == -1)
	{
		closesocket(sock);
		return INVALID_SOCKET;
	}

	listen(sock, 2);

	return sock;
}

void SV_MVDStream_Poll (void)
{
	static int listensocket=INVALID_SOCKET;
	static int listenport;

	int client;
	netadr_t na;
	struct sockaddr_qstorage addr;
	socklen_t addrlen;
	int count;
	qbool wanted;
	mvddest_t *dest;
	char *ip;
	unsigned long _true = true;

	if (!sv.state || !(int)qtv_streamport.value)
		wanted = false;
	else if (listenport && (int)qtv_streamport.value != listenport)	//easy way to switch... disable for a frame. :)
	{
		listenport = (int)qtv_streamport.value;
		wanted = false;
	}
	else
	{
		listenport = (int)qtv_streamport.value;
		wanted = true;
	}

	if (wanted && listensocket==INVALID_SOCKET)
	{
		listensocket = MVD_StreamStartListening(listenport);
/*		if (listensocket==INVALID_SOCKET && qtv_streamport.modified) @@@
		{
			Con_Printf("Cannot open TCP port %i for QTV\n", listenport);
			qtv_streamport.modified = false;
		}
*/
	}
	else if (!wanted && listensocket!=INVALID_SOCKET)
	{
		closesocket(listensocket);
		listensocket = INVALID_SOCKET;
		return;
	}
	if (listensocket==INVALID_SOCKET)
		return;

	addrlen = sizeof(addr);
	client = accept (listensocket, (struct sockaddr *)&addr, &addrlen);

	if (client == INVALID_SOCKET)
		return;

	if (ioctlsocket (client, FIONBIO, &_true) == SOCKET_ERROR) {
		Con_Printf ("SV_MVDStream_Poll: ioctl FIONBIO: (%i): %s\n", qerrno, strerror (qerrno));
		closesocket(client);
		return;
	}

	if ((int)qtv_maxstreams.value > 0)
	{
		count = 0;
		for (dest = demo.dest; dest; dest = dest->nextdest)
		{
			if (dest->desttype == DEST_STREAM)
			{
				count++;
			}
		}

		if (count >= (int)qtv_maxstreams.value)
		{	//sorry
			char *goawaymessage = "QTVSV 1\nERROR: This server enforces a limit on the number of proxies connected at any one time. Please try again later\n\n";

			send(client, goawaymessage, strlen(goawaymessage), 0);
			closesocket(client);
			return;
		}
	}

	SockadrToNetadr(&addr, &na);
	ip = NET_AdrToString(na);
	Con_Printf("MVD streaming client connected from %s\n", ip);

	SV_MVD_InitPendingStream(client, ip);
}

void SV_MVD_RunPendingConnections (void)
{
	unsigned short ushort_result;
	char *e;
	int len;
	mvdpendingdest_t *p;
	mvdpendingdest_t *np;

	if (!demo.pendingdest)
		return;

	while (demo.pendingdest && demo.pendingdest->error)
	{
		np = demo.pendingdest->nextdest;

		if (demo.pendingdest->socket != -1)
			closesocket(demo.pendingdest->socket);
		Q_free(demo.pendingdest);
		demo.pendingdest = np;
	}

	for (p = demo.pendingdest; p && p->nextdest; p = p->nextdest)
	{
		if (p->nextdest->error)
		{
			np = p->nextdest->nextdest;
			if (p->nextdest->socket != -1)
				closesocket(p->nextdest->socket);
			Q_free(p->nextdest);
			p->nextdest = np;
		}
	}

	for (p = demo.pendingdest; p; p = p->nextdest)
	{
		if (p->outsize && !p->error)
		{
			len = send(p->socket, p->outbuffer, p->outsize, 0);

			if (len == 0) //client died
			{
//				p->error = true;
				// man says: The calls return the number of characters sent, or -1 if an error occurred.   
				// so 0 is legal or what?
			}
			else if (len > 0)	//we put some data through
			{	//move up the buffer
				p->outsize -= len;
				memmove(p->outbuffer, p->outbuffer+len, p->outsize );
			}
			else
			{ //error of some kind. would block or something
				if (qerrno != EWOULDBLOCK && qerrno != EAGAIN)
					p->error = true;
			}
		}

		if (!p->error)
		{
			len = recv(p->socket, p->inbuffer + p->insize, sizeof(p->inbuffer) - p->insize - 1, 0);

			if (len > 0)
			{ //fixme: cope with extra \rs
				char *end;
				p->insize += len;
				p->inbuffer[p->insize] = 0;

				for (end = p->inbuffer; ; end++)
				{
					if (*end == '\0')
					{
						end = NULL;
						break;	//not enough data
					}

					if (end[0] == '\n')
					{
						if (end[1] == '\n')
						{
							end[1] = '\0';
							break;
						}
					}
				}
				if (end)
				{ //we found the end of the header
					char *start, *lineend;
					int versiontouse = 0;
					int raw = 0;
					char password[256] = "";
					typedef enum {
						QTVAM_NONE,
						QTVAM_PLAIN,
						QTVAM_CCITT,
						QTVAM_MD4,
						QTVAM_MD5,
					} authmethod_t;
					authmethod_t authmethod = QTVAM_NONE;
					start = p->inbuffer;

					lineend = strchr(start, '\n');
					if (!lineend)
					{
//						char *e;
//						e = "This is a QTV server.";
//						send(p->socket, e, strlen(e), 0);

						p->error = true;
						continue;
					}
					*lineend = '\0';
					COM_ParseToken(start, NULL);
					start = lineend+1;
					if (strcmp(com_token, "QTV"))
					{ //it's an error if it's not qtv.
						p->error = true;
						lineend = strchr(start, '\n');
						continue;
					}

					for(;;)
					{
						lineend = strchr(start, '\n');
						if (!lineend)
							break;
						*lineend = '\0';
						start = COM_ParseToken(start, NULL);
						if (*start == ':')
						{
//VERSION: a list of the different qtv protocols supported. Multiple versions can be specified. The first is assumed to be the prefered version.
//RAW: if non-zero, send only a raw mvd with no additional markup anywhere (for telnet use). Doesn't work with challenge-based auth, so will only be accepted when proxy passwords are not required.
//AUTH: specifies an auth method, the exact specs varies based on the method
//		PLAIN: the password is sent as a PASSWORD line
//		MD4: the server responds with an "AUTH: MD4\n" line as well as a "CHALLENGE: somerandomchallengestring\n" line, the client sends a new 'initial' request with CHALLENGE: MD4\nRESPONSE: hexbasedmd4checksumhere\n"
//		MD5: same as md4
//		CCITT: same as md4, but using the CRC stuff common to all quake engines.
//		if the supported/allowed auth methods don't match, the connection is silently dropped.
//SOURCE: which stream to play from, DEFAULT is special. Without qualifiers, it's assumed to be a tcp address.
//COMPRESSION: Suggests a compression method (multiple are allowed). You'll get a COMPRESSION response, and compression will begin with the binary data.

							start = start+1;
							Con_Printf("qtv, got (%s) (%s)\n", com_token, start);
							if (!strcmp(com_token, "VERSION"))
							{
								start = COM_ParseToken(start, NULL);
								if (atoi(com_token) == 1)
									versiontouse = 1;
							}
							else if (!strcmp(com_token, "RAW"))
							{
								start = COM_ParseToken(start, NULL);
								raw = atoi(com_token);
							}
							else if (!strcmp(com_token, "PASSWORD"))
							{
								start = COM_ParseToken(start, NULL);
								strlcpy(password, com_token, sizeof(password));
							}
							else if (!strcmp(com_token, "AUTH"))
							{
								authmethod_t thisauth;
								start = COM_ParseToken(start, NULL);
								if (!strcmp(com_token, "NONE"))
									thisauth = QTVAM_PLAIN;
								else if (!strcmp(com_token, "PLAIN"))
									thisauth = QTVAM_PLAIN;
								else if (!strcmp(com_token, "CCIT"))
									thisauth = QTVAM_CCITT;
								else if (!strcmp(com_token, "MD4"))
									thisauth = QTVAM_MD4;
//								else if (!strcmp(com_token, "MD5"))
//									thisauth = QTVAM_MD5;
								else
								{
									thisauth = QTVAM_NONE;
									Con_DPrintf("qtv: received unrecognised auth method (%s)\n", com_token);
								}

								if (authmethod < thisauth)
									authmethod = thisauth;
							}
							else if (!strcmp(com_token, "SOURCE"))
							{
								//servers don't support source, and ignore it.
								//source is only useful for qtv proxy servers.
							}
							else if (!strcmp(com_token, "COMPRESSION"))
							{
								//compression not supported yet
							}
							else
							{
								//not recognised.
							}
						}
						start = lineend+1;
					}

					len = (end - p->inbuffer)+2;
					p->insize -= len;
					memmove(p->inbuffer, p->inbuffer + len, p->insize);
					p->inbuffer[p->insize] = 0;

					e = NULL;
					if (p->hasauthed)
					{
					}
					else if (!*qtv_password.string)
						p->hasauthed = true; //no password, no need to auth.
					else if (*password)
					{
						switch (authmethod)
						{
						case QTVAM_NONE:
							e = ("QTVSV 1\n"
								 "PERROR: You need to provide a common auth method.\n\n");
							break;
						case QTVAM_PLAIN:
							p->hasauthed = !strcmp(qtv_password.string, password);
							break;
						case QTVAM_CCITT:
							CRC_Init(&ushort_result);
							CRC_AddBlock(&ushort_result, (byte *) p->challenge, strlen(p->challenge));
							CRC_AddBlock(&ushort_result, (byte *) qtv_password.string, strlen(qtv_password.string));
							p->hasauthed = (ushort_result == atoi(password));
							break;
						case QTVAM_MD4:
							{
								char hash[512];
								int md4sum[4];
								
								snprintf (hash, sizeof(hash), "%s%s", p->challenge, qtv_password.string);
								Com_BlockFullChecksum (hash, strlen(hash), (unsigned char*)md4sum);
								snprintf (hash, sizeof(hash), "%X%X%X%X", md4sum[0], md4sum[1], md4sum[2], md4sum[3]);
								p->hasauthed = !strcmp(password, hash);
							}
							break;
						case QTVAM_MD5:
						default:
							e = ("QTVSV 1\n"
								 "PERROR: FTEQWSV bug detected.\n\n");
							break;
						}
						if (!p->hasauthed && !e)
						{
							if (raw)
								e = "";
							else
								e = ("QTVSV 1\n"
									 "PERROR: Bad password.\n\n");
						}
					}
					else
					{
						//no password, and not automagically authed
						switch (authmethod)
						{
						case QTVAM_NONE:
							if (raw)
								e = "";
							else
								e = ("QTVSV 1\n"
									 "PERROR: You need to provide a common auth method.\n\n");
							break;
						case QTVAM_PLAIN:
							p->hasauthed = !strcmp(qtv_password.string, password);
							break;

							if (0)
							{
						case QTVAM_CCITT:
									e =	("QTVSV 1\n"
										"AUTH: CCITT\n"
										"CHALLENGE: ");
							}
							else if (0)
							{
						case QTVAM_MD4:
									e = ("QTVSV 1\n"
										"AUTH: MD4\n"
										"CHALLENGE: ");
							}
							else
							{
						case QTVAM_MD5:
									e = ("QTVSV 1\n"
										"AUTH: MD5\n"
										"CHALLENGE: ");
							}

							send(p->socket, e, strlen(e), 0);
							send(p->socket, p->challenge, strlen(p->challenge), 0);
							e = "\n\n";
							send(p->socket, e, strlen(e), 0);
							continue;

						default:
							e = ("QTVSV 1\n"
								 "PERROR: FTEQWSV bug detected.\n\n");
							break;
						}
					}

					if (e)
					{
					}
					else if (!versiontouse)
					{
						e = ("QTVSV 1\n"
							 "PERROR: Incompatable version (valid version is v1)\n\n");
					}
					else if (raw)
					{
						if (p->hasauthed == false)
						{
							e =	"";
						}
						else
						{
							SV_MVD_Record(SV_InitStream(p->socket));
							p->socket = -1;	//so it's not cleared wrongly.
						}
						p->error = true;
					}
					else
					{
						if (p->hasauthed == true)
						{
							e = ("QTVSV 1\n"
								 "BEGIN\n"
								 "\n");
							send(p->socket, e, strlen(e), 0);
							e = NULL;
							SV_MVD_Record(SV_InitStream(p->socket));
							p->socket = -1;	//so it's not cleared wrongly.
						}
						else
						{
							e = ("QTVSV 1\n"
								"PERROR: You need to provide a password.\n\n");
						}
						p->error = true;
					}

					if (e)
					{
						send(p->socket, e, strlen(e), 0);
						p->error = true;
					}
				}
			}
			else if (len == 0)
				p->error = true;
			else
			{	//error of some kind. would block or something
				int err;
				err = qerrno;
				if (err != EWOULDBLOCK && err != EAGAIN)
					p->error = true;
			}
		}
	}
}

void DemoWriteQTVTimePad (int msecs)	//broadcast to all proxies
{
	mvddest_t *d;
	unsigned char buffer[6];
	while (msecs > 0)
	{
		//duration
		if (msecs > 255)
			buffer[0] = 255;
		else
			buffer[0] = msecs;
		msecs -= buffer[0];
		//message type
		buffer[1] = dem_read;
		//length
		buffer[2] = 0;
		buffer[3] = 0;
		buffer[4] = 0;
		buffer[5] = 0;

		for (d = demo.dest; d; d = d->nextdest)
		{
			if (d->desttype == DEST_STREAM)
			{
				DemoWriteDest(buffer, sizeof(buffer), d);
			}
		}
	}
}

//====================================

void QTV_Init(void)
{
	Cvar_Register (&qtv_streamport);
	Cvar_Register (&qtv_maxstreams);
	Cvar_Register (&qtv_password);
}