/*
	quIRC - simple terminal-based IRC client
	Copyright (C) 2010-11 Edward Cree

	See quirc.c for license information
	irc: networking functions
*/

#include "irc.h"

void handle_signals(int sig)
{
	if(sig==SIGPIPE)
		sigpipe=1;
	else if(sig==SIGWINCH)
		sigwinch=1;
	else if(sig==SIGUSR1)
		sigusr1=1;
}

#if ASYNCH_NL
nl_list *irc_connect(char *server, const char *portno)
{
	// Look up server
	struct addrinfo *hints=malloc(sizeof(struct addrinfo));
	if(!hints)
	{
		add_to_buffer(0, c_err, strerror(errno), "malloc: ");
		return(NULL);
	}
	memset(hints, 0, sizeof(*hints));
	hints->ai_family=AF_INET;
	hints->ai_socktype = SOCK_STREAM; // TCP stream sockets
	struct gaicb *nl_details=malloc(sizeof(struct gaicb));
	if(!nl_details)
	{
		add_to_buffer(0, c_err, strerror(errno), "malloc: ");
		return(NULL);
	}
	nl_list *nl=malloc(sizeof(nl_list));
	if(!nl)
	{
		add_to_buffer(0, c_err, strerror(errno), "malloc: ");
		free(nl_details);
		return(NULL);
	}
	*nl=(nl_list){.nl_details=nl_details, .autoent=NULL, .reconn_b=0, .next=nl_active, .prev=NULL};
	*nl_details=(struct gaicb){.ar_name=strdup(server), .ar_service=strdup(portno), .ar_request=hints};
	if(nl_active) nl_active->prev=nl;
	nl_active=nl;
	getaddrinfo_a(GAI_NOWAIT, &nl->nl_details, 1, &(struct sigevent){.sigev_notify=SIGEV_SIGNAL, .sigev_signo=SIGUSR1});
	return(nl_active);
}

int irc_conn_found(nl_list **list, fd_set *master, int *fdmax)
{
	int serverhandle=0;
	struct addrinfo *servinfo;
	while(*list)
	{
		if(gai_error((*list)->nl_details)) *list=(*list)->next;
		else break;
	}
	if(!*list) return(0); // 0 indicates failure as rv is new serverhandle value
	servinfo=(*list)->nl_details->ar_result;
	
#else /* ASYNCH_NL */
int irc_connect(char *server, const char *portno, fd_set *master, int *fdmax)
{
	int serverhandle=0;
	struct addrinfo hints, *servinfo;
	// Look up server
	memset(&hints, 0, sizeof(hints));
	hints.ai_family=AF_INET;
	hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
	int rv;
	if((rv=getaddrinfo(server, portno, &hints, &servinfo))!=0)
	{
		add_to_buffer(0, c_err, (char *)gai_strerror(rv), "getaddrinfo: ");
		return(0); // 0 indicates failure as rv is new serverhandle value
	}
#endif /* ASYNCH_NL */
	char sip[INET_ADDRSTRLEN];
	struct addrinfo *p;
	// loop through all the results and connect to the first we can
	for(p=servinfo;p;p=p->ai_next)
	{
		inet_ntop(p->ai_family, &(((struct sockaddr_in*)p->ai_addr)->sin_addr), sip, sizeof(sip));
		if((serverhandle=socket(p->ai_family, p->ai_socktype, p->ai_protocol))==-1)
		{
			add_to_buffer(0, c_err, strerror(errno), "socket: ");
			continue;
		}
		if(fcntl(serverhandle, F_SETFD, O_NONBLOCK)==-1)
		{
			close(serverhandle);
			add_to_buffer(0, c_err, strerror(errno), "fcntl: ");
			continue;
		}
		connect_loop:
		if(connect(serverhandle, p->ai_addr, p->ai_addrlen)==-1)
		{
			if(errno!=EINPROGRESS)
			{
				if(errno==EINTR)
				{
					goto connect_loop;
				}
				else
				{
					close(serverhandle);
					add_to_buffer(0, c_err, strerror(errno), "connect: ");
					continue;
				}
			}
		}
		break;
	}
	if(debug)
	{
		char cmsg[16+strlen(sip)];
		sprintf(cmsg, "fd=%d, ip=%s", serverhandle, sip);
		add_to_buffer(0, c_status, cmsg, "DBG connect: ");
	}
	if(!p)
	{
		add_to_buffer(0, c_err, "failed to connect to server", "/connect: ");
		return(0); // 0 indicates failure as rv is new serverhandle value
	}
	freeaddrinfo(servinfo);
	servinfo=NULL;
	FD_SET(serverhandle, master);
	*fdmax=max(*fdmax, serverhandle);
	return(serverhandle);
}

int irc_conn_rest(int b, char *nick, char *username, char *passwd, char *fullname)
{
	if(debug)
	{
		add_to_buffer(0, c_status, "", "DBG connect rest");
	}
	bufs[b].live=true; // mark it as live
	bufs[b].conninpr=false;
	bufs[b].ping=0;
	bufs[b].last=time(NULL);
	if(bufs[b].autoent && bufs[b].autoent->nick)
		nick=bufs[b].autoent->nick;
	if(bufs[b].autoent && bufs[b].autoent->pass)
		passwd=bufs[b].autoent->pass;
	if(passwd) // Optionally send a PASS message before the NICK/USER
	{
		char passmsg[6+strlen(passwd)];
		sprintf(passmsg, "PASS %s", passwd); // PASS <password>
		irc_tx(bufs[b].handle, passmsg);
	}
	char nickmsg[6+strlen(nick)];
	sprintf(nickmsg, "NICK %s", nick); // NICK <nickname>
	irc_tx(bufs[b].handle, nickmsg);
	struct utsname arch;
	uname(&arch);
	char usermsg[16+strlen(username)+strlen(arch.nodename)+strlen(fullname)];
	sprintf(usermsg, "USER %s 0 %s :%s", username, arch.nodename, fullname); // USER <user> <mode> <unused> <realname>
	irc_tx(bufs[b].handle, usermsg);
	return(0);
}

int autoconnect(fd_set *master, int *fdmax, servlist *serv) // XXX broken in the face of asynch nl
{
	servlist *curr=serv;
	if(!curr) return(0);
	#if ASYNCH_NL
	char cstr[36+strlen(serv->name)+strlen(serv->portno)];
	sprintf(cstr, "Connecting to %s on port %s...", serv->name, serv->portno);
	nl_list *list=irc_connect(serv->name, serv->portno);
	if(!list) return(autoconnect(master, fdmax, serv->next));
	if(!quiet) add_to_buffer(0, c_status, cstr, "auto: ");
	list->reconn_b=0;
	list->autoent=serv;
	#else /* ASYNCH_NL */
	char cstr[36+strlen(serv->name)+strlen(serv->portno)];
	sprintf(cstr, "Connecting to %s on port %s...", serv->name, serv->portno);
	if(!quiet) add_to_buffer(0, c_status, cstr, "auto: ");
	int serverhandle=irc_connect(serv->name, serv->portno, master, fdmax);
	if(serverhandle)
	{
		bufs=(buffer *)realloc(bufs, ++nbufs*sizeof(buffer));
		cbuf=nbufs-1;
		init_buffer(cbuf, SERVER, serv->name, buflines);
		bufs[cbuf].handle=serverhandle;
		bufs[cbuf].nick=strdup(serv->nick);
		bufs[cbuf].autoent=serv;
		if(serv) bufs[cbuf].ilist=serv->igns;
		bufs[cbuf].server=cbuf;
		bufs[cbuf].conninpr=true;
		if(!quiet) add_to_buffer(cbuf, c_status, cstr, "auto: ");
		if(force_redraw<3) redraw_buffer();
	}
	#endif /* ASYNCH_NL */
	autoconnect(master, fdmax, serv->next);
	return(1);
}

int irc_tx(int fd, char * packet)
{
	sigpipe=0;
    char pq[512];
	low_quote(packet, pq);
	unsigned long l=min(strlen(pq), 511);
	unsigned long p=0;
	while((p<l)&&!sigpipe)
	{
		signed long j=send(fd, pq+p, l-p, 0);
		if(j<1)
		{
			if(errno==EINTR)
				continue;
			return(p); // Something went wrong with send()!
		}
		p+=j;
	}
	if(sigpipe)
	{
		if(debug)
		{
			char tmsg[32+strlen(pq)];
			sprintf(tmsg, "%d, %lu bytes: %s", fd, p, pq);
			add_to_buffer(0, c_status, tmsg, "DBG SIGPIPE tx: ");
		}
		sigpipe=0;
		return(-1);
	}
	send(fd, "\n", 1, 0);
	if(debug)
	{
		char tmsg[32+strlen(pq)];
		sprintf(tmsg, "%d, %lu bytes: %s", fd, l, pq);
		add_to_buffer(0, c_status, tmsg, "DBG tx: ");
	}
	if(sigpipe)
	{
		sigpipe=0;
		return(-1);
	}
	return(l); // Return the number of bytes sent
}

int irc_rx(int fd, char ** data)
{
	sigpipe=0;
	char buf[512];
	unsigned long int l=0;
	bool cr=false;
	while(!(cr||sigpipe))
	{
		long bytes=recv(fd, buf+l, 1, 0);
		if(bytes>0)
		{
			char c=buf[l];
			if((strchr("\n\r", c)!=NULL) || (l>510))
			{
				cr=true;
				buf[l]=0;
			}
			l++;
		}
		else if(bytes<0)
		{
			if(errno==EINTR)
				continue;
			int b;
			for(b=0;b<nbufs;b++)
			{
				if((fd==bufs[b].handle) && (bufs[b].type==SERVER))
				{
					add_to_buffer(b, c_err, strerror(errno), "irc_rx: recv:");
					bufs[b].live=false;
					close(bufs[b].handle);
					bufs[b].handle=0; // de-bind fd
				}
			}
			cr=true; // just crash out with a partial message
			buf[l]=0;
		}
	}
	*data=strdup(buf);
	if(debug && *buf)
	{
		char rmsg[32+strlen(buf)];
		sprintf(rmsg, "%d, %lu bytes: %s", fd, l, buf);
		add_to_buffer(0, c_status, rmsg, sigpipe?"DBG SIGPIPE rx: ":"DBG rx: ");
	}
	if(sigpipe)
	{
		sigpipe=0;
		return(-1);
	}
	if(!*data)
		return(1);
	return(0);
}

message irc_breakdown(char *packet)
{
	char *pp=packet;
	message rv;
	if(*packet==':')
	{
		rv.prefix=strtok(packet, " ");
		rv.cmd=strtok(NULL, " ");
		packet=strtok(NULL, "");
	}
	else
	{
		rv.prefix=NULL;
		rv.cmd=strtok(packet, " ");
		packet=strtok(NULL, "");
	}
	if(rv.prefix) rv.prefix=low_dequote(rv.prefix+1);
	if(rv.cmd) rv.cmd=low_dequote(rv.cmd);
	if(!(rv.cmd&&packet))
	{
		rv.nargs=0;
		free(pp);
		return(rv);
	}
	int arg=0;
	char *p;
	while(*packet)
	{
		p=packet;
		if((*p==':')||(arg==14))
		{
			p++;
			rv.args[arg++]=low_dequote(p);
			rv.nargs=arg;
			break;
		}
		while((*p)&&((*p)!=' '))
		{
			p++;
		}
		char c=*p;
		*p=0;
		rv.args[arg++]=low_dequote(packet);
		packet=p+(c?1:0);
	}
	rv.nargs=arg;
	free(pp);
	return(rv);
}

void message_free(message pkt)
{
	free(pkt.prefix);
	free(pkt.cmd);
	int arg;
	for(arg=0;arg<pkt.nargs;arg++)
		free(pkt.args[arg]);
}

void prefix_split(char * prefix, char **src, char **user, char **host)
{
	// {server|nick[[!user]@host]}
	*src=prefix?prefix:"";
	*host=strchr(*src, '@');
	if(*host)
		**host++=0;
	else
		*host="";
	*user=strchr(*src, '!');
	if(*user)
		**user++=0;
	else
		*user="";
}

void low_quote(char *from, char to[512])
{
	int o=0;
	while((*from) && (o<510))
	{
		char c=*from++;
		switch(c)
		{
			case '\n':
				to[o++]=MQUOTE;
				to[o++]='n';
			break;
			case '\r':
				to[o++]=MQUOTE;
				to[o++]='r';
			break;
			case MQUOTE:
				to[o++]=MQUOTE;
				to[o++]=MQUOTE;
			break;
			case '\\':
				if(*from=='0') // "\\0", is an encoded '\0'
				{
					to[o++]=MQUOTE; // because this will produce ^P 0, the proper representation
				}
				else
				{
					to[o++]=c;
				}
			break;
			case 0: // can't happen right now
				to[o++]=MQUOTE;
				to[o++]='0';
			break;
			default:
				to[o++]=c;
			break;
		}
	}
	to[o]=0;
}

char * low_dequote(char *buf)
{
	char *rv=(char *)malloc(512);
	if(!rv) return(NULL);
	char *p=buf;
	int o=0;
	while((*p) && (o<510))
	{
		if(*p==MQUOTE)
		{
			char c=*++p;
			switch(c)
			{
				case '0':
					rv[o++]='\\';
					rv[o]='0'; // We will have to defer '\0' handling as we can't stick '\0's in char *s (NUL terminated strings)
				break;
				case 'n':
					rv[o]='\n';
				break;
				case 'r':
					rv[o]='\r';
				break;
				case MQUOTE: // MQUOTE MQUOTE => MQUOTE, so fall through
				default:
					rv[o]=c;
				break;
			}
		}
		else
		{
			rv[o]=*p;
		}
		p++;o++;
	}
	rv[o]=0;
	return(rv);
}

char irc_to_upper(char c, cmap casemapping)
{
	// 97 to 126 -> 65 to 94 (CASEMAPPING=rfc1459; non-strict)
	// 97 to 125 -> 65 to 93 (CASEMAPPING=strict-rfc1459)
	// 97 to 122 -> 65 to 90 (CASEMAPPING=ascii)
	switch(casemapping)
	{
		case ASCII:
			if((97<=c)&&(c<=122))
				return(c-32);
		break;
		case STRICT_RFC1459:
			if((97<=c)&&(c<=125))
				return(c-32);
		break;
		case RFC1459: // fallthrough
		default:
			if((97<=c)&&(c<=126))
				return(c-32);
		break;
	}
	return(c);
}

char irc_to_lower(char c, cmap casemapping)
{
	// 65 to 94 -> 97 to 126 (CASEMAPPING=rfc1459; non-strict)
	// 65 to 93 -> 97 to 125 (CASEMAPPING=strict-rfc1459)
	// 65 to 90 -> 97 to 122 (CASEMAPPING=ascii)
	switch(casemapping)
	{
		case ASCII:
			if((65<=c)&&(c<=90))
				return(c+32);
		break;
		case STRICT_RFC1459:
			if((65<=c)&&(c<=93))
				return(c+32);
		break;
		case RFC1459: // fallthrough
		default:
			if((65<=c)&&(c<=94))
				return(c+32);
		break;
	}
	return(c);
}

int irc_strcasecmp(const char *c1, const char *c2, cmap casemapping)
{
	char t1,t2;
	while(*c1||*c2)
	{
		t1=irc_to_upper(*c1, casemapping);
		t2=irc_to_upper(*c2, casemapping);
		if(t2!=t1)
			return(t2>t1?-1:1);
		c1++;c2++;
	}
	return(0);
}

int irc_strncasecmp(const char *c1, const char *c2, int n, cmap casemapping)
{
	int i=0;
	char t1,t2;
	while((i<n)&&(c1[i]||c2[i]))
	{
		t1=irc_to_upper(c1[i], casemapping);
		t2=irc_to_upper(c2[i], casemapping);
		if(t2!=t1)
			return(t2>t1?-1:1);
		i++;
	}
	return(0);
}

int irc_numeric(message pkt, int b)
{
	int num=0;
	int b2;
	sscanf(pkt.cmd, "%d", &num);
	// arg[0] is invariably dest, with numeric replies; TODO check dest is us (not vital)
	int arg;
	switch(num)
	{
		case RPL_X_ISUPPORT:
			// 005 dest {[-]parameter|parameter=value}+ :are supported by this server
			for(arg=1;arg<pkt.nargs-1;arg++) // last argument is always the :postfix descriptive text
			{
				char *rest=pkt.args[arg];
				bool min=false;
				char *value=NULL;
				if(*rest=='-')
				{
					min=true;
					rest++;
				}
				else
				{
					char *eq=strchr(rest, '=');
					if(eq)
					{
						value=eq+1;
						*eq=0;
					}
				}
				if(strcmp(rest, "CASEMAPPING")==0)
				{
					if(value)
					{
						if(strcmp(value, "ascii")==0)
						{
							bufs[b].casemapping=ASCII;
						}
						else if(strcmp(value, "strict-rfc1459")==0)
						{
							bufs[b].casemapping=STRICT_RFC1459;
						}
						else
						{
							bufs[b].casemapping=RFC1459;
						}
					}
					else
					{
						bufs[b].casemapping=RFC1459;
					}
				}
				else if(strcmp(rest, "PREFIX")==0)
				{
					if(value)
					{
						if(*value=='(')
						{
							char *p=strchr(value, ')');
							if(p)
							{
								unsigned int npfx=p-value-1;
								prefix *pfxs=malloc(npfx*sizeof(prefix));
								if(!pfxs)
									e_buf_print(b, c_err, pkt, "RPL_ISUPPORT: Discarded PREFIX - malloc failure: ");
								else
								{
									unsigned int i;
									for(i=0;i<npfx;i++)
									{
										pfxs[i].letter=value[i+1];
										if(!(pfxs[i].pfx=p[i+1]))
										{
											if(!quiet) e_buf_print(b, c_err, pkt, "RPL_ISUPPORT: Malformed PREFIX - unbalanced parts: ");
											break;
										}
									}
									if(i==npfx)
									{
										free(bufs[b].prefixes);
										bufs[b].npfx=npfx;
										bufs[b].prefixes=pfxs;
									}
								}
							}
							else
								if(!quiet) e_buf_print(b, c_err, pkt, "RPL_ISUPPORT: Malformed PREFIX - missing ')': ");
						}
						else
							if(!quiet) e_buf_print(b, c_err, pkt, "RPL_ISUPPORT: Malformed PREFIX - missing '(': ");
					}
					else
					{
						free(bufs[b].prefixes);
						bufs[b].npfx=0;
						bufs[b].prefixes=NULL;
					}
				}
				else
				{
					char isup[strlen(rest)+(value?strlen(value):0)+3];
					sprintf(isup, "%s%s%s%s", min?"-":"", rest, value?"=":"", value?value:"");
					if(!quiet) add_to_buffer(b, c_unn, isup, "RPL_ISUPPORT: ");
				}
			}
		break;
		case RPL_NAMREPLY:
			// 353 dest {=|/|\*|@} #chan :([@|\+]nick)+
			if(pkt.nargs<3)
			{
				if(!quiet) e_buf_print(b, c_err, pkt, "RPL_NAMREPLY: Not enough arguments: ");
				break;
			}
			for(b2=0;b2<nbufs;b2++)
			{
				if((bufs[b2].server==b) && (bufs[b2].type==CHANNEL) && (irc_strcasecmp(pkt.args[2], bufs[b2].bname, bufs[b].casemapping)==0))
				{
					if(!bufs[b2].namreply)
					{
						bufs[b2].namreply=true;
						n_free(bufs[b2].nlist);
						bufs[b2].nlist=NULL;
						bufs[b2].us=NULL;
					}
					char *nn=strtok(pkt.args[3], " ");
					while(nn)
					{
						unsigned int plen=0;
						if(bufs[b].prefixes) // skip over prefix characters
						{
							while(nn[plen])
							{
								unsigned int i;
								for(i=0;i<bufs[b].npfx;i++)
								{
									if(nn[plen]==bufs[b].prefixes[i].pfx)
										break;
								}
								if(i<bufs[b].npfx) plen++;
								else break;
							}
						}
						name *n=n_add(&bufs[b2].nlist, nn+plen, bufs[b].casemapping);
						if(n)
						{
							if((n->npfx=plen))
							{
								n->prefixes=malloc(plen*sizeof(prefix));
								for(unsigned int i=0;i<plen;i++)
								{
									n->prefixes[i]=(prefix){.pfx=nn[i], .letter=0};
									for(unsigned int j=0;j<bufs[b].npfx;j++)
									{
										if(nn[i]==bufs[b].prefixes[j].pfx)
										{
											n->prefixes[i].letter=bufs[b].prefixes[j].letter;
											break;
										}
									}
								}
							}
							if(strcmp(nn+plen, bufs[b].nick)==0)
								bufs[b2].us=n;
						}
						nn=strtok(NULL, " ");
					}
				}
			}
		break;
		case RPL_ENDOFNAMES:
			// 366 dest #chan :End of /NAMES list
			if(pkt.nargs<1)
			{
				if(!quiet) e_buf_print(b, c_err, pkt, "RPL_ENDOFNAMES: Not enough arguments: ");
				break;
			}
			for(b2=0;b2<nbufs;b2++)
			{
				if((bufs[b2].server==b) && (bufs[b2].type==CHANNEL) && (irc_strcasecmp(pkt.args[1], bufs[b2].bname, bufs[b].casemapping)==0))
				{
					bufs[b2].namreply=false;
					char lmsg[32+strlen(pkt.args[1])];
					sprintf(lmsg, "NAMES list received for %s", pkt.args[1]);
					if(!quiet) add_to_buffer(b2, c_status, lmsg, "");
				}
			}
		break;
		case RPL_ENDOFMOTD: // 376 dest :End of MOTD command
		case RPL_MOTDSTART: // 375 dest :- <server> Message of the day -
		case RPL_MOTD: // 372 dest :- <text>
			if(pkt.nargs<2)
			{
				if(!quiet) e_buf_print(b, c_err, pkt, num==RPL_MOTD?"RPL_MOTD: Not enough arguments: ":num==RPL_MOTDSTART?"RPL_MOTDSTART: Not enough arguments: ":num==RPL_ENDOFMOTD?"RPL_ENDOFMOTD: Not enough arguments: ":"RPL_MOTD???: Not enough arguments: ");
				break;
			}
			add_to_buffer(b, c_notice[1], pkt.args[1], "");
		break;
		case ERR_NOMOTD: // 422 <dest> :MOTD File is missing
			if(pkt.nargs<2)
			{
				if(!quiet) e_buf_print(b, c_err, pkt, "ERR_NOMOTD: Not enough arguments: ");
				break;
			}
			add_to_buffer(b, c_notice[1], pkt.args[1], "");
		break;
		case RPL_TOPIC: // 332 dest <channel> :<topic>
			if(pkt.nargs<3)
			{
				if(!quiet) e_buf_print(b, c_err, pkt, "RPL_TOPIC: Not enough arguments: ");
				break;
			}
			for(b2=0;b2<nbufs;b2++)
			{
				if((bufs[b2].server==b) && (bufs[b2].type==CHANNEL) && (irc_strcasecmp(pkt.args[1], bufs[b2].bname, bufs[b].casemapping)==0))
				{
					char tmsg[32+strlen(pkt.args[1])];
					sprintf(tmsg, "Topic for %s is ", pkt.args[1]);
					add_to_buffer(b2, c_notice[1], pkt.args[2], tmsg);
					free(bufs[b2].topic);
					bufs[b2].topic=strdup(pkt.args[2]);
				}
			}
		break;
		case RPL_NOTOPIC: // 331 dest <channel> :No topic is set
			if(pkt.nargs<2)
			{
				if(!quiet) e_buf_print(b, c_err, pkt, "RPL_NOTOPIC: Not enough arguments: ");
				break;
			}
			for(b2=0;b2<nbufs;b2++)
			{
				if((bufs[b2].server==b) && (bufs[b2].type==CHANNEL) && (irc_strcasecmp(pkt.args[1], bufs[b2].bname, bufs[b].casemapping)==0))
				{
					char tmsg[32+strlen(pkt.args[1])];
					sprintf(tmsg, "No topic is set for %s", pkt.args[1]);
					add_to_buffer(b2, c_notice[1], tmsg, "");
					free(bufs[b2].topic);
				}
			}
		break;
		case RPL_X_TOPICWASSET: // 331 dest <channel> <nick> <time>
			if(pkt.nargs<3)
			{
				if(!quiet) e_buf_print(b, c_err, pkt, "RPL_X_TOPICWASSET: Not enough arguments: ");
				break;
			}
			for(b2=0;b2<nbufs;b2++)
			{
				if((bufs[b2].server==b) && (bufs[b2].type==CHANNEL) && (irc_strcasecmp(pkt.args[1], bufs[b2].bname, bufs[b].casemapping)==0))
				{
					time_t when;
					sscanf(pkt.args[3], "%u", (unsigned int *)&when);
					char ts[256];
					struct tm *tm = gmtime(&when);
					size_t tslen = strftime(ts, sizeof(ts), "%H:%M:%S GMT on %a, %d %b %Y", tm); // TODO options controlling date format (eg. ISO 8601)
					char tmsg[32+strlen(pkt.args[2])+tslen];
					sprintf(tmsg, "Topic was set by %s at %s", pkt.args[2], ts);
					add_to_buffer(b2, c_status, tmsg, "");
				}
			}
		break;
		case RPL_LUSERCLIENT: // 251 <dest> :There are <integer> users and <integer> invisible on <integer> servers"
		case RPL_LUSERME: // 255 <dest> ":I have <integer> clients and <integer> servers
			if(pkt.nargs<2)
			{
				if(!quiet) e_buf_print(b, c_err, pkt, num==RPL_LUSERCLIENT?"RPL_LUSERCLIENT: Not enough arguments: ":num==RPL_LUSERME?"RPL_LUSERME: Not enough arguments: ":"RPL_LUSER???: Not enough arguments: ");
				break;
			}
			if(!quiet) add_to_buffer(b, c_status, pkt.args[1], ": ");
		break;
		case RPL_LUSEROP: // 252 <dest> <integer> :operator(s) online
		case RPL_LUSERUNKNOWN: // 253 <dest> <integer> :unknown connection(s)
		case RPL_LUSERCHANNELS: // 254 <dest> <integer> :channels formed
			if(pkt.nargs<3)
			{
				if(!quiet) e_buf_print(b, c_err, pkt, num==RPL_LUSEROP?"RPL_LUSEROP: Not enough arguments: ":num==RPL_LUSERUNKNOWN?"RPL_LUSERUNKNOWN: Not enough arguments: ":num==RPL_LUSERCHANNELS?"RPL_LUSERCHANNELS: Not enough arguments: ":"RPL_LUSER???: Not enough arguments: ");
				break;
			}
			else
			{
				char lmsg[2+strlen(pkt.args[1])+strlen(pkt.args[2])];
				sprintf(lmsg, "%s %s", pkt.args[1], pkt.args[2]);
				if(!quiet) add_to_buffer(b, c_status, lmsg, ": ");
			}
		break;
		case RPL_AWAY: // 301 <dest> <nick> :<away message>
			if(pkt.nargs<2)
			{
				if(!quiet) e_buf_print(b, c_err, pkt, "RPL_AWAY: Not enough arguments: ");
				break;
			}
			else
			{
				int b2;
				for(b2=0;b2<nbufs;b2++)
				{
					if((bufs[b2].server==b)&&(bufs[b2].type==PRIVATE)&&(irc_strcasecmp(pkt.args[1], bufs[b2].bname, bufs[b].casemapping)==0))
					{
						char *tag=mktag("(%s away) ", pkt.args[1]);
						add_to_buffer(b2, c_notice[1], pkt.nargs>2?pkt.args[2]:"no message set", tag);
						free(tag);
						break;
					}
				}
				if(b2==nbufs)
				{
					char *tag=mktag("(%s away) ", pkt.args[1]);
					add_to_buffer(b, c_notice[1], pkt.nargs>2?pkt.args[2]:"no message set", tag);
					free(tag);
				}
			}
		break;
		case RPL_NOWAWAY: // 306 <dest> :You have been marked as being away
			if(!quiet) add_to_buffer(b, c_status, pkt.nargs>1?pkt.args[1]:"You have been marked as being away", "/away: ");
		break;
		case RPL_UNAWAY: // 305 <dest> :You are no longer marked as being away
			if(!quiet) add_to_buffer(b, c_status, pkt.nargs>1?pkt.args[1]:"You are no longer marked as being away", "/unaway: ");
		break;
		case RPL_X_LOCALUSERS: // 265 <dest> :Current Local Users: <integer>\tMax: <integer>
		case RPL_X_GLOBALUSERS: // 266 <dest> :Current Global Users: <integer>\tMax: <integer>
			if(pkt.nargs<2)
			{
				if(!quiet) e_buf_print(b, c_err, pkt, num==RPL_X_LOCALUSERS?"RPL_X_LOCALUSERS: Not enough arguments: ":num==RPL_X_GLOBALUSERS?"RPL_X_GLOBALUSERS: Not enough arguments: ":"RPL_???USERS: Not enough arguments: ");
				break;
			}
			if(!quiet) add_to_buffer(b, c_status, pkt.args[1], ": ");
		break;
		case ERR_NOSUCHNICK: // 401 <dest> <nick> :No such nick/channel
			if(pkt.nargs<2)
			{
				if(!quiet) e_buf_print(b, c_err, pkt, "ERR_NOSUCHNICK: Not enough arguments: ");
				break;
			}
			int b2=findptab(b, pkt.args[1]);
			if(b2<0)
				b2=b;
			if(!quiet) add_to_buffer(b2, c_err, pkt.args[1], "No such nick/channel: ");
		break;
		default:
			if(!quiet) e_buf_print(b, c_unn, pkt, "Unknown numeric: ");
		break;
	}
	return(num);
}

int rx_ping(message pkt, int b)
{
	// PING <sender>
	if(pkt.nargs<1)
	{
		if(!quiet) e_buf_print(b, c_err, pkt, "Not enough arguments: ");
		return(0);
	}
	char pong[8+strlen(username)+strlen(pkt.args[0])];
	sprintf(pong, "PONG %s %s", username, pkt.args[0]); // PONG <user> <sender>
	return(irc_tx(bufs[bufs[b].server].handle, pong));
}

int rx_mode(message pkt, int b)
{
	// MODE <nick> ({\+|-}{i|w|o|O|r}*)*
	// or MODE <channel> {[\+|-]|o|p|s|i|t|n|b|v} [<limit>] [<user>] [<ban mask>]
	int fd=bufs[b].handle;
	// If appropriate, trigger auto-join
	servlist *serv=bufs[b].autoent;
	if(autojoin && serv && serv->chans && !serv->join)
	{
		chanlist * curr=serv->chans;
		while(curr)
		{
			char joinmsg[8+strlen(curr->name)];
			sprintf(joinmsg, "JOIN %s %s", curr->name, curr->key?curr->key:"");
			irc_tx(fd, joinmsg);
			char jmsg[16+strlen(curr->name)];
			sprintf(jmsg, "auto: Joining %s", curr->name);
			if(!quiet) add_to_buffer(b, c_join[0], jmsg, "");
			curr=curr->next;
		}
		serv->join=true;
	}
	if(pkt.nargs<2)
	{
		if(!quiet) e_buf_print(b, c_err, pkt, "Not enough arguments: ");
		return(0);
	}
	char *from, *user, *host;
	prefix_split(pkt.prefix, &from, &user, &host);
	for(int b2=0;b2<nbufs;b2++)
	{
		if((bufs[b2].type==CHANNEL)&&(bufs[b2].server==b)&&(irc_strcasecmp(pkt.args[0], bufs[b2].bname, bufs[b].casemapping)==0))
		{
			bool plus=false;
			switch(*pkt.args[1])
			{
				case '+':
					plus=true;
				case '-':; /* fallthrough */
					unsigned int i;
					for(i=0;i<bufs[b].npfx;i++)
					{
						if(bufs[b].prefixes[i].letter==pkt.args[1][1])
						{
							// user expected
							if(pkt.nargs<3)
							{
								if(!quiet) e_buf_print(b, c_err, pkt, "Not enough arguments: ");
								return(0);
							}
							name *curr=bufs[b2].nlist;
							while(curr)
							{
								if(irc_strcasecmp(curr->data, pkt.args[2], bufs[b].casemapping)==0)
								{
									bool found=false;
									for(unsigned int j=0;j<curr->npfx;j++)
									{
										if(curr->prefixes[j].letter==pkt.args[1][1])
										{
											if(plus)
												found=true;
											else
											{
												curr->npfx--;
												for(unsigned int k=j;k<curr->npfx;k++)
													curr->prefixes[k]=curr->prefixes[k+1];
												if(curr==bufs[b2].us)
												{
													char ms[curr->npfx?curr->npfx+1:2];
													if(curr->npfx)
													{
														for(unsigned int i=0;i<curr->npfx;i++)
															ms[i]=curr->prefixes[i].letter;
														ms[curr->npfx]=0;
													}
													else
													{
														ms[0]='-';
														ms[1]=0;
													}
													char mm[24+strlen(curr->data)+strlen(ms)+strlen(from)];
													sprintf(mm, "You (%s) are now mode %s (%s)", curr->data, ms, from);
													add_to_buffer(b2, c_nick[1], mm, "");
												}
												else
												{
													char ms[curr->npfx?curr->npfx+1:2];
													if(curr->npfx)
													{
														for(unsigned int i=0;i<curr->npfx;i++)
															ms[i]=curr->prefixes[i].letter;
														ms[curr->npfx]=0;
													}
													else
													{
														ms[0]='-';
														ms[1]=0;
													}
													char mm[16+strlen(ms)+strlen(from)];
													char *nick=strdup(curr->data);
													crush(&nick, maxnlen);
													char *tag=mktag("=%s= ", nick);
													free(nick);
													sprintf(mm, "is now mode %s (%s)", ms, from);
													add_to_buffer(b2, c_nick[1], mm, tag);
													free(tag);
												}
											}
										}
									}
									if(plus&&!found)
									{
										unsigned int n=curr->npfx++;
										prefix *pfx=realloc(curr->prefixes, curr->npfx*sizeof(prefix));
										if(pfx)
											(curr->prefixes=pfx)[n]=bufs[b].prefixes[i];
										else
											curr->npfx=n; // XXX silent fail
										if(curr==bufs[b2].us)
										{
											char ms[curr->npfx?curr->npfx+1:2];
											if(curr->npfx)
											{
												for(unsigned int i=0;i<curr->npfx;i++)
													ms[i]=curr->prefixes[i].letter;
												ms[curr->npfx]=0;
											}
											else
											{
												ms[0]='-';
												ms[1]=0;
											}
											char mm[24+strlen(curr->data)+strlen(ms)+strlen(from)];
											sprintf(mm, "You (%s) are now mode %s (%s)", curr->data, ms, from);
											add_to_buffer(b2, c_nick[1], mm, "");
										}
										else
										{
											char ms[curr->npfx?curr->npfx+1:2];
											if(curr->npfx)
											{
												for(unsigned int i=0;i<curr->npfx;i++)
													ms[i]=curr->prefixes[i].letter;
												ms[curr->npfx]=0;
											}
											else
											{
												ms[0]='-';
												ms[1]=0;
											}
											char mm[16+strlen(ms)+strlen(from)];
											char *nick=strdup(curr->data);
											crush(&nick, maxnlen);
											char *tag=mktag("=%s= ", nick);
											free(nick);
											sprintf(mm, "is now mode %s (%s)", ms, from);
											add_to_buffer(b2, c_nick[1], mm, tag);
											free(tag);
										}
									}
									break;
								}
								curr=curr->next;
							}
							if(!curr)
								if(!quiet) e_buf_print(b, c_err, pkt, "No such nick: ");
							break;
						}
					}
					if(i==bufs[b].npfx)
					{
						// it's a channel mode
						bool found=false;
						for(i=0;i<bufs[b2].npfx;i++)
						{
							if(bufs[b2].prefixes[i].letter==pkt.args[1][1])
							{
								if(plus)
									found=true;
								else
								{
									bufs[b2].npfx--;
									for(unsigned int j=i;j<bufs[b2].npfx;j++)
										bufs[b2].prefixes[j]=bufs[b2].prefixes[j+1];
									char ms[bufs[b2].npfx?bufs[b2].npfx+1:2];
									if(bufs[b2].npfx)
									{
										for(unsigned int i=0;i<bufs[b2].npfx;i++)
											ms[i]=bufs[b2].prefixes[i].letter;
										ms[bufs[b2].npfx]=0;
									}
									else
									{
										ms[0]='-';
										ms[1]=0;
									}
									char mm[16+strlen(ms)+strlen(from)];
									sprintf(mm, " is now mode %s (%s)", ms, from);
									add_to_buffer(b2, c_nick[1], mm, bufs[b2].bname);
								}
							}
						}
						if(plus&&!found)
						{
							unsigned int n=bufs[b2].npfx++;
							prefix *pfx=realloc(bufs[b2].prefixes, bufs[b2].npfx*sizeof(prefix));
							if(pfx)
								(bufs[b2].prefixes=pfx)[n]=(prefix){.letter=pkt.args[1][1], .pfx=0}; // channel modes don't have associated prefixes
							else
								bufs[b2].npfx=n; // XXX silent fail
							char ms[bufs[b2].npfx?bufs[b2].npfx+1:2];
							if(bufs[b2].npfx)
							{
								for(unsigned int i=0;i<bufs[b2].npfx;i++)
									ms[i]=bufs[b2].prefixes[i].letter;
								ms[bufs[b2].npfx]=0;
							}
							else
							{
								ms[0]='-';
								ms[1]=0;
							}
							char mm[16+strlen(ms)+strlen(from)];
							sprintf(mm, " is now mode %s (%s)", ms, from);
							add_to_buffer(b2, c_nick[1], mm, bufs[b2].bname);
						}
					}
				break;
				default:
					if(!quiet) e_buf_print(b, c_err, pkt, "Malformed modespec - missing +/-: ");
				break;
			}
		}
	}
	if(tsb)
		titlebar();
#if 0 // somewhat broken usermode handling (broken in that it writes to places that store /channel/-usermodes)
	// Find the nick in the nlist and apply the MODE changes
	bool found=false;
	for(int b2=0;b2<nbufs;b2++)
	{
		if((bufs[b2].type==CHANNEL)&&(bufs[b2].server==b))
		{
			name *curr=bufs[b].nlist;
			while(curr)
			{
				if(irc_strcasecmp(curr->data, pkt.args[0], bufs[b].casemapping)==0)
				{
					bool malformed=false;
					for(int i=1;i<pkt.nargs;i++)
					{
						const char *ms=pkt.args[i];
						switch(*ms)
						{
							case '+':
								while(*++ms)
								{
									bool found=false;
									for(unsigned int j=0;j<curr->npfx;j++)
									{
										if(*ms==curr->prefixes[j].letter)
										{
											found=true;
											break;
										}
									}
									if(!found)
									{
										for(unsigned int j=0;j<bufs[b].npfx;j++)
										{
											if(bufs[b].prefixes[j].letter==*ms)
											{
												unsigned int n=curr->npfx++;
												prefix *pfx=realloc(curr->prefixes, curr->npfx*sizeof(prefix));
												if(pfx)
													(curr->prefixes=pfx)[n]=bufs[b].prefixes[j];
												else
													curr->npfx=n;
												break;
											}
										}
									}
								}
							break;
							case '-':
								while(*++ms)
								{
									for(unsigned int j=0;j<curr->npfx;j++)
									{
										if(*ms==curr->prefixes[j].letter)
										{
											curr->npfx--;
											for(unsigned int k=j;k<curr->npfx;k++)
												curr->prefixes[k]=curr->prefixes[k+1];
										}
									}
								}
							break;
							default:
								malformed=true;
							break;
						}
					}
					if(malformed)
						if(!quiet) e_buf_print(b, c_err, pkt, "Malformed modespec - missing +/-: ");
					break;
				}
				curr=curr->next;
			}
			if(curr) found=true;
		}
	}
	if(!found)
		if(!quiet) e_buf_print(b, c_err, pkt, "No such nick: ");
#endif
	return(0);
}

int rx_kill(message pkt, int b, fd_set *master)
{
	// KILL <nick> <comment>
	if(pkt.nargs<1)
	{
		if(!quiet) e_buf_print(b, c_err, pkt, "Not enough arguments: ");
		return(0);
	}
	int fd=bufs[b].handle;
	if(strcmp(pkt.args[0], bufs[b].nick)==0) // if it's us, we disconnect from the server
	{
		close(fd);
		FD_CLR(fd, master);
		int b2;
		for(b2=1;b2<nbufs;b2++)
		{
			if((bufs[b2].server==b) || (bufs[b2].server==0))
			{
				add_to_buffer(b2, c_quit[0], pkt.nargs<2?"":pkt.args[1], "KILLed: ");
				bufs[b2].live=false;
				close(bufs[b2].handle);
				bufs[b2].handle=0; // de-bind fd
				bufs[b2].hi_alert=5;
			}
		}
		redraw_buffer();
	}
	else // if it's not us, generate quit messages into the relevant channel tabs
	{
		int b2;
		for(b2=1;b2<nbufs;b2++)
		{
			if((bufs[b2].server==b) || (bufs[b2].server==0))
			{
				if(n_cull(&bufs[b2].nlist, pkt.args[0], bufs[b2].casemapping)&&!bufs[b2].conf)
				{
					if(pkt.nargs<2)
					{
						char kmsg[24+strlen(pkt.args[0])+strlen(bufs[b].bname)];
						sprintf(kmsg, "=%s= has left %s (Killed)", pkt.args[0], bufs[b].bname);
						add_to_buffer(b2, c_quit[1], kmsg, "");
					}
					else
					{
						char kmsg[28+strlen(pkt.args[0])+strlen(pkt.args[1])+strlen(bufs[b].bname)];
						sprintf(kmsg, "=%s= has left %s (Killed: %s)", pkt.args[0], bufs[b].bname, pkt.args[1]);
						add_to_buffer(b2, c_quit[1], kmsg, "");
					}
				}
			}
		}
	}
	return(0);
}

int rx_kick(message pkt, int b)
{
	// KICK #chan user [comment]
	// From RFC2812: "The server MUST NOT send KICK messages with multiple channels or users to clients.  This is necessarily to maintain backward compatibility with old client software."
	if(pkt.nargs<2)
	{
		if(!quiet) e_buf_print(b, c_err, pkt, "Not enough arguments: ");
		return(0);
	}
	if(irc_strcasecmp(pkt.args[1], bufs[b].nick, bufs[b].casemapping)==0) // if it's us, generate a message and de-live the channel
	{
		int b2;
		for(b2=1;b2<nbufs;b2++)
		{
			if((bufs[b2].server==b) && (bufs[b2].type==CHANNEL) && (irc_strcasecmp(pkt.args[0], bufs[b2].bname, bufs[b].casemapping)==0))
			{
				add_to_buffer(b2, c_quit[0], pkt.nargs<3?"(No reason)":pkt.args[2], "Kicked: ");
				bufs[b2].live=false;
				bufs[b2].hi_alert=5;
			}
		}
		redraw_buffer();
	}
	else // if it's not us, just generate kick message
	{
		int b2;
		for(b2=1;b2<nbufs;b2++)
		{
			if((bufs[b2].server==b) && (bufs[b2].type==CHANNEL) && (irc_strcasecmp(pkt.args[0], bufs[b2].bname, bufs[b].casemapping)==0))
			{
				if(n_cull(&bufs[b2].nlist, pkt.args[1], bufs[b2].casemapping)&&!bufs[b2].conf)
				{
					if(pkt.nargs<3)
					{
						char kmsg[32+strlen(pkt.args[1])];
						sprintf(kmsg, "=%s= was kicked.  (No reason)", pkt.args[1]);
						add_to_buffer(b2, c_quit[1], kmsg, "");
					}
					else
					{
						char kmsg[32+strlen(pkt.args[1])+strlen(pkt.args[2])];
						sprintf(kmsg, "=%s= was kicked.  Reason: %s", pkt.args[1], pkt.args[2]);
						add_to_buffer(b2, c_quit[1], kmsg, "");
					}
				}
			}
		}
	}
	return(0);
}

int rx_error(message pkt, int b, fd_set *master)
{
	// ERROR [message]
	// assume it's fatal
	int fd=bufs[b].handle;
	close(fd);
	FD_CLR(fd, master);
	int b2;
	for(b2=1;b2<nbufs;b2++)
	{
		if((bufs[b2].server==b) || (bufs[b2].server==0))
		{
			e_buf_print(b2, c_quit[0], pkt, "Disconnected: ");
			bufs[b2].live=false;
			close(bufs[b2].handle);
			bufs[b2].handle=0; // de-bind fd
			bufs[b2].hi_alert=5;
		}
	}
	return(redraw_buffer());
}

int rx_privmsg(message pkt, int b, bool notice)
{
	// :nick[[!user]@host] PRIVMSG msgtarget text
	// :nick[[!user]@host] NOTICE msgtarget text
	if(pkt.nargs<2)
	{
		if(!quiet) e_buf_print(b, c_err, pkt, "Not enough arguments: ");
		return(0);
	}
	char *src, *user, *host;
	prefix_split(pkt.prefix, &src, &user, &host);
	char nm[strlen(src)+strlen(user)+strlen(host)+3];
	sprintf(nm, "%s!%s@%s", src, user, host);
	char *from=strdup(src);
	crush(&from, maxnlen);
	int b2;
	bool match=false;
	bool ha=strstr(pkt.args[1], bufs[b].nick);
	for(b2=0;b2<nbufs;b2++)
	{
		if((bufs[b2].server==b) && (bufs[b2].type==CHANNEL) && (irc_strcasecmp(pkt.args[0], bufs[b2].bname, bufs[b].casemapping)==0))
		{
			match=true;
			if(i_match(bufs[b].ilist, nm, false, bufs[b].casemapping)||i_match(bufs[0].ilist, nm, false, bufs[b].casemapping))
				break;
			if(i_match(bufs[b2].ilist, nm, false, bufs[b].casemapping))
				continue;
			if(*pkt.args[1]==1) // CTCP TODO: proper CTCP handling of embedded messages
			{
				ctcp(pkt.args[1], from, src, b2, ha, notice, false);
			}
			else
			{
				char mk[6]="<%s> ";
				if(show_prefix)
				{
					name *curr=bufs[b2].nlist;
					while(curr)
					{
						if(irc_strcasecmp(src, curr->data, bufs[b].casemapping)==0)
						{
							int po=-1;
							for(unsigned int i=0;i<curr->npfx;i++)
							{
								for(unsigned int j=0;j<(po<0?bufs[b].npfx:(unsigned)po);j++)
								{
									if(bufs[b].prefixes[j].letter==curr->prefixes[i].letter)
									{
										po=j;
										mk[0]=mk[3]=curr->prefixes[i].pfx;
									}
								}
							}
							break;
						}
						curr=curr->next;
					}
				}
				char *tag=mktag(mk, from);
				add_to_buffer(b2, notice?c_notice[1]:c_msg[1], pkt.args[1], tag);
				free(tag);
				if(ha)
					bufs[b2].hi_alert=5;
			}
		}
	}
	if(!match)
	{
		if(i_match(bufs[b].ilist, nm, true, bufs[b].casemapping)||i_match(bufs[0].ilist, nm, true, bufs[b].casemapping))
			return(0);
		if((irc_strcasecmp(pkt.args[0], bufs[b].nick, bufs[b].casemapping)==0) || (irc_strcasecmp(pkt.args[0], "AUTH", bufs[b].casemapping)==0) || (irc_strcasecmp(pkt.args[0], "Global", bufs[b].casemapping)==0))
		{
			if(*pkt.args[1]==1) // CTCP TODO: proper CTCP handling of embedded messages
			{
				ctcp(pkt.args[1], from, src, b, true, notice, true);
			}
			else
			{
				if(!notice)
				{
					int b2=findptab(b, src);
					if(b2<0)
					{
						bufs=(buffer *)realloc(bufs, ++nbufs*sizeof(buffer));
						init_buffer(nbufs-1, PRIVATE, src, buflines);
						b2=nbufs-1;
						bufs[b2].server=bufs[b].server;
						bufs[b2].live=true;
						n_add(&bufs[b2].nlist, bufs[bufs[b2].server].nick, bufs[b].casemapping);
						n_add(&bufs[b2].nlist, src, bufs[b].casemapping);
					}
					char *tag=mktag("<%s> ", from);
					add_to_buffer(b2, notice?c_notice[1]:c_msg[1], pkt.args[1], tag);
					free(tag);
					bufs[b2].hi_alert=5;
				}
				else
				{
					char *tag=mktag("(from %s) ", from);
					add_to_buffer(b, notice?c_notice[1]:c_msg[1], pkt.args[1], tag);
					free(tag);
				}
			}
		}
		else
		{
			if(!quiet) e_buf_print(b, c_err, pkt, "Bad destination: ");
		}
	}
	free(from);
	return(0);
}

int rx_topic(message pkt, int b)
{
	// TOPIC <dest> [<topic>]
	if(pkt.nargs<1)
	{
		if(!quiet) e_buf_print(b, c_err, pkt, "Not enough arguments: ");
		return(0);
	}
	char *src, *user, *host;
	prefix_split(pkt.prefix, &src, &user, &host);
	char *from=strdup(src);
	scrush(&from, maxnlen);
	bool match=false;
	if(pkt.nargs<2)
	{
		char *tag=mktag("%s ", from);
		int b2;
		for(b2=0;b2<nbufs;b2++)
		{
			if((bufs[b2].server==b) && (bufs[b2].type==CHANNEL) && (irc_strcasecmp(pkt.args[0], bufs[b2].bname, bufs[b].casemapping)==0))
			{
				add_to_buffer(b2, c_notice[1], "removed the Topic", tag);
				match=true;
				free(bufs[b2].topic);
				bufs[b2].topic=NULL;
			}
		}
		free(tag);
	}
	else
	{
		
		char *tag=mktag("%s set the Topic to ", from);
		int b2;
		for(b2=0;b2<nbufs;b2++)
		{
			if((bufs[b2].server==b) && (bufs[b2].type==CHANNEL) && (irc_strcasecmp(pkt.args[0], bufs[b2].bname, bufs[b].casemapping)==0))
			{
				add_to_buffer(b2, c_notice[1], pkt.args[1], tag);
				match=true;
				free(bufs[b2].topic);
				bufs[b2].topic=strdup(pkt.args[1]);
			}
		}
		free(tag);
	}
	free(from);
	if(match) titlebar();
	return(match?0:1);
}

int rx_join(message pkt, int b)
{
	// :nick[[!user]@host] JOIN #chan
	if(pkt.nargs<1)
	{
		if(!quiet) e_buf_print(b, c_err, pkt, "Not enough arguments: ");
		return(0);
	}
	char *src, *user, *host;
	prefix_split(pkt.prefix, &src, &user, &host);
	if(strcmp(src, bufs[b].nick)==0)
	{
		char dstr[20+strlen(src)+strlen(pkt.args[0])];
		sprintf(dstr, "You (%s) have joined %s", src, pkt.args[0]);
		int b2;
		for(b2=1;b2<nbufs;b2++)
		{
			if((bufs[b2].server==b) && (bufs[b2].type==CHANNEL) && (irc_strcasecmp(pkt.args[0], bufs[b2].bname, bufs[b].casemapping)==0))
			{
				cbuf=b2;
				break;
			}
		}
		if(b2>=nbufs)
		{
			bufs=(buffer *)realloc(bufs, ++nbufs*sizeof(buffer));
			init_buffer(nbufs-1, CHANNEL, pkt.args[0], buflines);
			cbuf=nbufs-1;
			bufs[cbuf].server=bufs[b].server;
		}
		add_to_buffer(cbuf, c_join[0], dstr, "");
		bufs[cbuf].live=true;
		if(force_redraw<3) redraw_buffer();
	}
	else
	{
		char *from=strdup(src);
		crush(&from, maxnlen);
		char *tag=mktag("=%s= ", from);
		free(from);
		bool match=false;
		int b2;
		for(b2=0;b2<nbufs;b2++)
		{
			if((bufs[b2].server==b) && (bufs[b2].type==CHANNEL) && (irc_strcasecmp(pkt.args[0], bufs[b2].bname, bufs[b].casemapping)==0))
			{
				match=true;
				if(!bufs[b2].conf)
				{
					char dstr[16+strlen(pkt.args[0])];
					sprintf(dstr, "has joined %s", pkt.args[0]);
					add_to_buffer(b2, c_join[1], dstr, tag);
				}
				n_add(&bufs[b2].nlist, src, bufs[b].casemapping);
			}
		}
		free(tag);
		if(!match)
		{
			if(!quiet) e_buf_print(b, c_err, pkt, "Bad destination: ");
		}
	}
	return(0);
}

int rx_part(message pkt, int b)
{
	// :nick[[!user]@host] PART #chan message
	if(pkt.nargs<1)
	{
		if(!quiet) e_buf_print(b, c_err, pkt, "Not enough arguments: ");
		return(0);
	}
	char *src, *user, *host;
	prefix_split(pkt.prefix, &src, &user, &host);
	if(strcmp(src, bufs[b].nick)==0)
	{
		int b2;
		for(b2=0;b2<nbufs;b2++)
		{
			if((bufs[b2].server==b) && (bufs[b2].type==CHANNEL) && (irc_strcasecmp(pkt.args[0], bufs[b2].bname, bufs[b].casemapping)==0))
			{
				if(b2==cbuf)
				{
					cbuf=b;
					if(force_redraw<3) redraw_buffer();
				}
				bufs[b2].live=false;
				free_buffer(b2);
			}
		}
	}
	else
	{
		char *from=strdup(src);
		crush(&from, maxnlen);
		char *tag=mktag("=%s= ", from);
		free(from);
		bool match=false;
		int b2;
		for(b2=0;b2<nbufs;b2++)
		{
			if((bufs[b2].server==b) && (bufs[b2].type==CHANNEL) && (irc_strcasecmp(pkt.args[0], bufs[b2].bname, bufs[b].casemapping)==0))
			{
				match=true;
				if(!bufs[b2].conf)
				{
					if(pkt.nargs<2)
					{
						char dstr[16+strlen(pkt.args[0])];
						sprintf(dstr, "has left %s", pkt.args[0]);
						add_to_buffer(b2, c_part[1], dstr, tag);
					}
					else
					{
						char dstr[24+strlen(pkt.args[0])+strlen(pkt.args[1])];
						sprintf(dstr, "has left %s (Part: %s)", pkt.args[0], pkt.args[1]);
						add_to_buffer(b2, c_part[1], dstr, tag);
					}
				}
				n_cull(&bufs[b2].nlist, src, bufs[b].casemapping);
			}
		}
		free(tag);
		if(!match)
		{
			if(!quiet) e_buf_print(b, c_err, pkt, "Bad destination: ");
		}
	}
	return(0);
}

int rx_quit(message pkt, int b)
{
	// :nick[[!user]@host] QUIT message
	char *src, *user, *host;
	prefix_split(pkt.prefix, &src, &user, &host);
	char *reason=pkt.nargs>0?pkt.args[0]:"";
	if(strcmp(src, bufs[b].nick)==0) // this shouldn't happen
	{
		if(!quiet) e_buf_print(b, c_err, pkt, "Should not be from us: ");
	}
	else
	{
		char *from=strdup(src);
		crush(&from, maxnlen);
		char *tag=mktag("=%s= ", from);
		free(from);
		int b2;
		for(b2=0;b2<nbufs;b2++)
		{
			if((bufs[b2].server==b) && ((bufs[b2].type==CHANNEL)||(bufs[b2].type==PRIVATE)))
			{
				if(n_cull(&bufs[b2].nlist, src, bufs[b].casemapping)&&!bufs[b2].conf)
				{
					char dstr[24+strlen(bufs[b].bname)+strlen(reason)];
					sprintf(dstr, "has left %s (%s)", bufs[b].bname, reason);
					add_to_buffer(b2, c_quit[1], dstr, tag);
				}
			}
		}
		free(tag);
	}
	return(0);
}

int rx_nick(message pkt, int b)
{
	// :nick[[!user]@host] NICK newnick
	if(pkt.nargs<1)
	{
		if(!quiet) e_buf_print(b, c_err, pkt, "Not enough arguments: ");
		return(0);
	}
	char *src, *user, *host;
	prefix_split(pkt.prefix, &src, &user, &host);
	if((strcmp(src, bufs[b].nick)==0)||(strcmp(pkt.args[0], bufs[b].nick)==0))
	{
		char dstr[30+strlen(src)+strlen(pkt.args[0])];
		sprintf(dstr, "You (%s) are now known as %s", src, pkt.args[0]);
		int b2;
		for(b2=0;b2<nbufs;b2++)
		{
			if(bufs[b2].server==b)
			{
				add_to_buffer(b2, c_nick[1], dstr, "");
				n_cull(&bufs[b2].nlist, src, bufs[b].casemapping);
				bufs[b2].us=n_add(&bufs[b2].nlist, pkt.args[0], bufs[b].casemapping);
			}
		}
	}
	else
	{
		char *from=strdup(src);
		crush(&from, maxnlen);
		char *tag=mktag("=%s= ", from);
		free(from);
		int b2;
		bool match=false;
		for(b2=0;b2<nbufs;b2++)
		{
			if((bufs[b2].server==b) && ((bufs[b2].type==CHANNEL)||(bufs[b2].type==PRIVATE)))
			{
				match=true;
				if(n_cull(&bufs[b2].nlist, src, bufs[b].casemapping))
				{
					n_add(&bufs[b2].nlist, pkt.args[0], bufs[b].casemapping);
					if(!bufs[b2].conf)
					{
						char dstr[30+strlen(pkt.args[0])];
						sprintf(dstr, "is now known as %s", pkt.args[0]);
						add_to_buffer(b2, c_nick[1], dstr, tag);
					}
				}
			}
		}
		free(tag);
		if(!match)
		{
			if(!quiet) e_buf_print(b, c_err, pkt, "Bad destination: ");
		}
	}
	return(0);
}

int ctcp(char *msg, char *from, char *src, int b2, bool ha, bool notice, bool priv)
{
	int fd=bufs[b2].handle;
	if(strncmp(msg, "\001ACTION ", 8)==0)
	{
		msg[strlen(msg)-1]=0; // remove trailing \001
		if(priv) // TODO: this bit is actually copypasted far too many times.  I need to find a better solution (probably proper CTCP handling generally)
		{
			int b=b2;
			b2=findptab(b, src);
			if(b2<0)
			{
				bufs=(buffer *)realloc(bufs, ++nbufs*sizeof(buffer));
				init_buffer(nbufs-1, PRIVATE, src, buflines);
				b2=nbufs-1;
				bufs[b2].server=bufs[b].server;
				bufs[b2].live=true;
				n_add(&bufs[b2].nlist, bufs[bufs[b2].server].nick, bufs[b].casemapping);
				n_add(&bufs[b2].nlist, src, bufs[b].casemapping);
			}
		}
		char *tag=mktag("  %s ", from);
		add_to_buffer(b2, c_actn[1], msg+8, tag);
		free(tag);
		ha=ha||strstr(msg+8, bufs[bufs[b2].server].nick);
		if(ha)
			bufs[b2].hi_alert=5;
	}
	else if(strncmp(msg, "\001FINGER", 7)==0)
	{
		if(notice)
		{
			if(priv)
			{
				int b=b2;
				b2=findptab(b, src);
				if(b2<0)
				{
					bufs=(buffer *)realloc(bufs, ++nbufs*sizeof(buffer));
					init_buffer(nbufs-1, PRIVATE, src, buflines);
					b2=nbufs-1;
					bufs[b2].server=bufs[b].server;
					bufs[b2].live=true;
					n_add(&bufs[b2].nlist, bufs[bufs[b2].server].nick, bufs[b].casemapping);
					n_add(&bufs[b2].nlist, src, bufs[b].casemapping);
				}
			}
			char *tag=mktag(priv?"(%s) ":"(from %s) ", from);
			add_to_buffer(b2, c_notice[1], msg, tag);
			free(tag);
			if(ha)
				bufs[b2].hi_alert=5;
		}
		else
		{
			char resp[32+strlen(src)+strlen(fname)];
			sprintf(resp, "NOTICE %s \001FINGER :%s\001", src, fname);
			irc_tx(fd, resp);
		}
	}
	else if(strncmp(msg, "\001PING", 5)==0)
	{
		if(notice)
		{
			if(priv)
			{
				int b=b2;
				b2=findptab(b, src);
				if(b2<0)
				{
					bufs=(buffer *)realloc(bufs, ++nbufs*sizeof(buffer));
					init_buffer(nbufs-1, PRIVATE, src, buflines);
					b2=nbufs-1;
					bufs[b2].server=bufs[b].server;
					bufs[b2].live=true;
					n_add(&bufs[b2].nlist, bufs[bufs[b2].server].nick, bufs[b].casemapping);
					n_add(&bufs[b2].nlist, src, bufs[b].casemapping);
				}
			}
			char *tag=mktag(priv?"(%s) ":"(from %s) ", from);
			add_to_buffer(b2, c_notice[1], msg, tag);
			free(tag);
			if(ha)
				bufs[b2].hi_alert=5;
		}
		else
		{
			char resp[16+strlen(src)+strlen(msg)];
			sprintf(resp, "NOTICE %s %s", src, msg);
			irc_tx(fd, resp);
		}
	}
	else if(strncmp(msg, "\001CLIENTINFO", 11)==0)
	{
		if(notice)
		{
			if(priv)
			{
				int b=b2;
				b2=findptab(b, src);
				if(b2<0)
				{
					bufs=(buffer *)realloc(bufs, ++nbufs*sizeof(buffer));
					init_buffer(nbufs-1, PRIVATE, src, buflines);
					b2=nbufs-1;
					bufs[b2].server=bufs[b].server;
					bufs[b2].live=true;
					n_add(&bufs[b2].nlist, bufs[bufs[b2].server].nick, bufs[b].casemapping);
					n_add(&bufs[b2].nlist, src, bufs[b].casemapping);
				}
			}
			char *tag=mktag(priv?"(%s) ":"(from %s) ", from);
			add_to_buffer(b2, c_notice[1], msg, tag);
			free(tag);
			if(ha)
				bufs[b2].hi_alert=5;
		}
		else
		{
			char resp[64+strlen(src)];
			sprintf(resp, "NOTICE %s \001CLIENTINFO ACTION FINGER PING CLIENTINFO VERSION\001", src);
			irc_tx(fd, resp);
		}
	}
	else if(strncmp(msg, "\001VERSION", 8)==0)
	{
		if(notice)
		{
			if(priv)
			{
				int b=b2;
				b2=findptab(b, src);
				if(b2<0)
				{
					bufs=(buffer *)realloc(bufs, ++nbufs*sizeof(buffer));
					init_buffer(nbufs-1, PRIVATE, src, buflines);
					b2=nbufs-1;
					bufs[b2].server=bufs[b].server;
					bufs[b2].live=true;
					n_add(&bufs[b2].nlist, bufs[bufs[b2].server].nick, bufs[b].casemapping);
					n_add(&bufs[b2].nlist, src, bufs[b].casemapping);
				}
			}
			char *tag=mktag(priv?"(%s) ":"(from %s) ", from);
			add_to_buffer(b2, c_notice[1], msg, tag);
			free(tag);
			if(ha)
				bufs[b2].hi_alert=5;
		}
		else
		{
			char resp[32+strlen(src)+strlen(version)+strlen(CC_VERSION)];
			sprintf(resp, "NOTICE %s \001VERSION %s:%s:%s\001", src, "quIRC", version, CC_VERSION);
			irc_tx(fd, resp);
		}
	}
	else
	{
		if(priv)
		{
			int b=b2;
			b2=findptab(b, src);
			if(b2<0)
			{
				bufs=(buffer *)realloc(bufs, ++nbufs*sizeof(buffer));
				init_buffer(nbufs-1, PRIVATE, src, buflines);
				b2=nbufs-1;
				bufs[b2].server=bufs[b].server;
				bufs[b2].live=true;
				n_add(&bufs[b2].nlist, bufs[bufs[b2].server].nick, bufs[b].casemapping);
				n_add(&bufs[b2].nlist, src, bufs[b].casemapping);
			}
		}
		char *tag=mktag(priv?"(%s) ":"(from %s) ", from);
		add_to_buffer(b2, c_notice[1], msg, tag);
		if(ha)
			bufs[b2].hi_alert=5;
		char *cmd=msg+1;
		char *space=strchr(cmd, ' ');
		if(space)
			*space=0;
		char cmsg[32+strlen(cmd)];
		sprintf(cmsg, "Unrecognised CTCP %s (ignoring)", cmd);
		if(!quiet) add_to_buffer(b2, c_unk, cmsg, tag);
		free(tag);
		if(!notice)
		{
			char resp[32+strlen(src)+strlen(cmd)];
			sprintf(resp, "NOTICE %s \001ERRMSG %s\001", src, cmd);
			irc_tx(fd, resp);
		}
		if(ha)
			bufs[b2].hi_alert=5;
	}
	return(0);
}

int talk(char *iinput)
{
	if((bufs[cbuf].type==CHANNEL)||(bufs[cbuf].type==PRIVATE))
	{
		if(bufs[bufs[cbuf].server].handle)
		{
			if(LIVE(cbuf))
			{
				char pmsg[12+strlen(bufs[cbuf].bname)+strlen(iinput)];
				sprintf(pmsg, "PRIVMSG %s :%s", bufs[cbuf].bname, iinput);
				irc_tx(bufs[bufs[cbuf].server].handle, pmsg);
				char *cnick=strdup(bufs[bufs[cbuf].server].nick);
				crush(&cnick, maxnlen);
				char *tag=mktag("<%s> ", cnick);
				free(cnick);
				add_to_buffer(cbuf, c_msg[0], iinput, tag);
				free(tag);
			}
			else
			{
				add_to_buffer(cbuf, c_err, "Can't talk - tab is not live!", "");
			}
		}
		else
		{
			add_to_buffer(cbuf, c_err, "Can't talk - tab is disconnected!", "");
		}
	}
	else
	{
		add_to_buffer(cbuf, c_err, "Can't talk - view is not a channel!", "");
	}
	return(0);
}
