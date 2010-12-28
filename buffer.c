/*
	quIRC - simple terminal-based IRC client
	Copyright (C) 2010 Edward Cree

	See quirc.c for license information
	buffer: multiple-buffer control
*/

#include "buffer.h"

int init_start_buffer(void)
{
	s_buf.nlines=0;
	s_buf.lt=NULL;
	s_buf.lc=NULL;
	s_buf.ts=NULL;
	s_buf.errs=0;
	return(0);
}

int add_to_start_buffer(colour lc, char *lt)
{
	s_buf.nlines++;
	char **nlt;colour *nlc;time_t *nts;
	nlt=(char **)realloc(s_buf.lt, s_buf.nlines*sizeof(char *));
	if(!nlt)
	{
		s_buf.nlines--;
		s_buf.errs++;
		return(1);
	}
	s_buf.lt=nlt;
	s_buf.lt[s_buf.nlines-1]=strdup(lt);
	nlc=(colour *)realloc(s_buf.lc, s_buf.nlines*sizeof(colour));
	if(!nlc)
	{
		s_buf.nlines--;
		s_buf.errs++;
		return(1);
	}
	s_buf.lc=nlc;
	s_buf.lc[s_buf.nlines-1]=lc;
	nts=(time_t *)realloc(s_buf.ts, s_buf.nlines*sizeof(time_t));
	if(!nts)
	{
		s_buf.nlines--;
		s_buf.errs++;
		return(1);
	}
	s_buf.ts=nts;
	s_buf.ts[s_buf.nlines-1]=time(NULL);
	return(0);
}

int asb_failsafe(colour lc, char *lt)
{
	int e=0;
	if((e=add_to_start_buffer(lc, lt)))
	{
		fprintf(stderr, "init[%d]: %s\n", e, lt);
	}
	return(e);
}

int free_start_buffer(void)
{
	int i;
	if(s_buf.lt)
	{
		for(i=0;i<s_buf.nlines;i++)
		{
			if(s_buf.lt[i]) free(s_buf.lt[i]);
		}
		free(s_buf.lt);
		s_buf.lt=NULL;
	}
	if(s_buf.lc) free(s_buf.lc);
	if(s_buf.ts) free(s_buf.ts);
	s_buf.nlines=0;
	return(0);
}

int initialise_buffers(int buflines)
{
	bufs=(buffer *)malloc(sizeof(buffer));
	if(!bufs)
		return(1);
	init_buffer(0, STATUS, "status", buflines); // buf 0 is always STATUS
	nbufs=1;
	cbuf=0;
	bufs[0].live=true; // STATUS is never dead
	bufs[0].nick=nick;
	bufs[0].ilist=igns;
	w_buf_print(0, c_status, "Copyright (C) 2010 Edward Cree", "quirc -- ");
	w_buf_print(0, c_status, "This program comes with ABSOLUTELY NO WARRANTY.", "");
	w_buf_print(0, c_status, "This is free software, and you are welcome to redistribute it under certain conditions.  (GNU GPL v3+)", "");
	w_buf_print(0, c_status, "For further details, see the file 'COPYING' in the quirc directory.", "");
	return(0);
}

int init_buffer(int buf, btype type, char *bname, int nlines)
{
	bufs[buf].type=type;
	bufs[buf].bname=strdup(bname);
	bufs[buf].realsname=NULL;
	bufs[buf].nlist=NULL;
	bufs[buf].ilist=NULL;
	bufs[buf].handle=0;
	bufs[buf].server=0;
	bufs[buf].nick=NULL;
	bufs[buf].topic=NULL;
	bufs[buf].nlines=nlines;
	bufs[buf].ptr=0;
	bufs[buf].scroll=0;
	bufs[buf].lc=(colour *)malloc(sizeof(colour[nlines]));
	bufs[buf].lt=(char **)malloc(sizeof(char *[nlines]));
	int i;
	for(i=0;i<bufs[buf].nlines;i++)
	{
		bufs[buf].lt[i]=NULL;
	}
	bufs[buf].ts=(time_t *)malloc(sizeof(time_t[nlines]));
	bufs[buf].filled=false;
	bufs[buf].alert=false;
	bufs[buf].hi_alert=0;
	bufs[buf].ping=0;
	bufs[buf].last=time(NULL);
	bufs[buf].namreply=false;
	bufs[buf].live=false;
	bufs[buf].conninpr=false;
	initibuf(&bufs[buf].input);
	bufs[buf].casemapping=RFC1459;
	bufs[buf].prefixes=NULL;
	bufs[buf].autoent=NULL;
	return(0);
}

int free_buffer(int buf)
{
	if(bufs[buf].live)
	{
		w_buf_print(buf, c_err, "Buffer is still live!", "free_buffer:");
		return(1);
	}
	else
	{
		if(bufs[buf].bname) free(bufs[buf].bname);
		if(bufs[buf].realsname) free(bufs[buf].realsname);
		n_free(bufs[buf].nlist);
		bufs[buf].nlist=NULL;
		n_free(bufs[buf].ilist);
		bufs[buf].ilist=NULL;
		free(bufs[buf].lc);
		if(bufs[buf].topic) free(bufs[buf].topic);
		int l;
		for(l=0;l<bufs[buf].nlines;l++)
			if(bufs[buf].lt[l]) free(bufs[buf].lt[l]);
		free(bufs[buf].lt);
		free(bufs[buf].ts);
		freeibuf(&bufs[buf].input);
		if(cbuf>=buf)
			cbuf--;
		nbufs--;
		int b;
		for(b=buf;b<nbufs;b++)
		{
			bufs[b]=bufs[b+1];
		}
		for(b=0;b<nbufs;b++)
		{
			if(bufs[b].server==buf)
			{
				bufs[b].server=0; // orphaned; should not happen
				bufs[b].live=false;
			}
			else if(bufs[b].server>buf)
			{
				bufs[b].server--;
			}
		}
		return(0);
	}
}

int add_to_buffer(int buf, colour lc, char *lt)
{
	if(buf>=nbufs)
	{
		if(bufs&&buf)
		{
			w_buf_print(0, c_err, "Line was written to bad buffer!  Contents below.", "add_to_buffer(): ");
			w_buf_print(0, lc, lt, "");
		}
		return(1);
	}
	char *nl;
	while((nl=strchr(lt, '\n'))!=NULL)
	{
		char *ln=strndup(lt, (size_t)(nl-lt));
		add_to_buffer(buf, lc, ln);
		free(ln);
		lt=nl+1;
	}
	bufs[buf].lc[bufs[buf].ptr]=lc;
	if(bufs[buf].lt[bufs[buf].ptr]) free(bufs[buf].lt[bufs[buf].ptr]);
	bufs[buf].lt[bufs[buf].ptr]=strdup(lt);
	bufs[buf].ts[bufs[buf].ptr]=time(NULL);
	bufs[buf].ptr=(bufs[buf].ptr+1)%bufs[buf].nlines;
	if(bufs[buf].ptr==0)
		bufs[buf].filled=true;
	if(bufs[buf].scroll)
		bufs[buf].scroll=min(bufs[buf].scroll+1, bufs[buf].nlines-1);
	bufs[buf].alert=true;
	bufs[cbuf].alert=false;
	return(0);
}

int redraw_buffer(void)
{
	int sl = ( bufs[cbuf].filled ? (bufs[cbuf].ptr+max(bufs[cbuf].nlines-(bufs[cbuf].scroll+height-3), 1))%bufs[cbuf].nlines : max(bufs[cbuf].ptr-(bufs[cbuf].scroll+height-3), 0) );
	int el = ( bufs[cbuf].filled ? (bufs[cbuf].ptr+bufs[cbuf].nlines-bufs[cbuf].scroll)%bufs[cbuf].nlines : max(bufs[cbuf].ptr-bufs[cbuf].scroll, 0) );
	int dl=el-sl;
	if(dl<0) dl+=bufs[cbuf].nlines;
	printf(CLS LOCATE, height-(dl+1), 1);
	resetcol();
	int l;
	for(l=sl;l!=el;l=(l+1)%bufs[cbuf].nlines)
	{
		setcolour(bufs[cbuf].lc[l]);
		if(full_width_colour)
		{
			printf("%s" CLR, bufs[cbuf].lt[l]);
			resetcol();
			printf("\n");
		}
		else
		{
			printf("%s", bufs[cbuf].lt[l]);
			resetcol();
			printf(CLR "\n");
		}
	}
	printf(CLA "\n");
	switch(bufs[cbuf].type)
	{
		case STATUS:
			settitle("quIRC - status");
		break;
		case SERVER: // have to scope it for the cstr 'variably modified type'
			{
				char cstr[16+strlen(bufs[cbuf].bname)];
				sprintf(cstr, "quIRC - %s", bufs[cbuf].bname);
				settitle(cstr);
			}
		break;
		case CHANNEL: // have to scope it for the cstr 'variably modified type'
			{
				char cstr[16+strlen(bufs[cbuf].bname)+strlen(bufs[bufs[cbuf].server].bname)];
				sprintf(cstr, "quIRC - %s on %s", bufs[cbuf].bname, bufs[bufs[cbuf].server].bname);
				settitle(cstr);
			}
		break;
		default:
			settitle("quIRC");
		break;
	}
	if(tsb)
		titlebar();
	bufs[cbuf].alert=false;
	return(0);
}

int buf_print(int buf, colour lc, char *lt)
{
	if(!bufs) return(2);
	if(force_redraw)
	{
		int e=add_to_buffer(buf, lc, lt);
		redraw_buffer();
		return(e);
	}
	if((buf==cbuf) && (bufs[buf].scroll==0))
	{
		setcolour(lc);
		printf(CLA "\n");
		printf(LOCATE CLA, height-1, 1);
		printf(LOCATE, height-2, 1);
		if(full_width_colour)
		{
			printf(CLA "%s" CLR, lt);
			resetcol();
			printf("\n" CLA "\n");
		}
		else
		{
			printf(CLA "%s", lt);
			resetcol();
			printf(CLR "\n" CLA "\n");
		}
		if(tsb)
			titlebar();
	}
	return(add_to_buffer(buf, lc, lt));
}

void in_update(iline inp)
{
	height=max(height, 5); // anything less than this is /silly/
	width=max(width, 30); // widths less than 30 break things, and are /also silly/
	resetcol();
	printf(LOCATE, height-1, 1);
	// tab strip
	int mbw = (width-1)/nbufs;
	int b;
	for(b=0;b<nbufs;b++)
	{
		colour c={7, hilite_tabstrip?5:0, false, false};
		setcolour(c);
		putchar(' ');
		// (status) {server} [channel] <user>
		char brack[2]={'!', '!'};
		switch(bufs[b].type)
		{
			case STATUS:
				brack[0]='(';brack[1]=')';
			break;
			case SERVER:
				brack[0]='{';brack[1]='}';
			break;
			case CHANNEL:
				brack[0]='[';brack[1]=']';
			break;
			case PRIVATE:
				brack[0]='<';brack[1]='>';
			break;
		}
		if(b==cbuf)
		{
			c.back=2; // green
			c.hi=true;
		}
		else if(b==bufs[cbuf].server)
		{
			c.back=4; // blue
			c.ul=true;
		}
		if(bufs[b].hi_alert%2)
		{
			c.fore=6; // cyan
			c.hi=true;
			c.ul=false; // can't have both at once: it's not really a bitmask
		}
		else if(bufs[b].alert)
		{
			c.fore=1; // red
			c.hi=true;
			c.ul=false; // can't have both at once: it's not really a bitmask
		}
		if((!LIVE(b)) && (c.fore!=6))
		{
			c.fore=3; // yellow
			c.hi=true;
			c.ul=false; // can't have both at once: it's not really a bitmask
		}
		setcolour(c);
		putchar(brack[0]);
		char *tab=strdup(bufs[b].bname);
		if(bufs[b].type==SERVER)
		{
			scrush(&tab, mbw-3);
		}
		else
		{
			crush(&tab, mbw-3);
		}
		printf("%s", tab);
		free(tab);
		putchar(brack[1]);
		c.fore=7;
		c.back=hilite_tabstrip?5:0;
		c.hi=c.ul=false;
		setcolour(c);
	}
	printf(CLR);
	resetcol();
	printf("\n");
	// input
	if(inp.left.i+inp.right.i+1<width)
	{
		char *lh=highlight(inp.left.data?inp.left.data:"");
		char *rh=highlight(inp.right.data?inp.right.data:"");
		printf("%s" SAVEPOS "%s" CLR RESTPOS, lh, rh);
		free(lh);
		free(rh);
	}
	else
	{
		if(inp.left.i<width*0.75)
		{
			char *lh=highlight(inp.left.data?inp.left.data:"");
			char rl[width-inp.left.i-3-max(3, (width-inp.left.i)/4)];
			snprintf(rl, width-inp.left.i-3-max(3, (width-inp.left.i)/4), "%s", inp.right.data);
			char *rlh=highlight(rl);
			char *rrh=highlight(inp.right.data+inp.right.i-max(3, (width-inp.left.i)/4));
			printf("%s" SAVEPOS "%s...%s" CLR RESTPOS, lh, rlh, rrh);
			free(lh);
			free(rlh);
			free(rrh);
		}
		else if(inp.right.i<width*0.75)
		{
			char ll[max(3, (width-inp.right.i)/4)];
			snprintf(ll, max(3, (width-inp.right.i)/4), "%s", inp.left.data);
			char *llh=highlight(ll);
			char *lrh=highlight(inp.left.data+inp.left.i-width+3+inp.right.i+max(3, (width-inp.right.i)/4));
			char *rh=highlight(inp.right.data?inp.right.data:"");
			printf("%s...%s" SAVEPOS "%s" CLR RESTPOS, llh, lrh, rh);
			free(llh);
			free(lrh);
			free(rh);
		}
		else
		{
			int torem=floor((width/4.0)*floor(((inp.left.i-(width/2.0))*4.0/width)+0.5));
			torem=min(torem, inp.left.i-3);
			int c=inp.left.i+4-torem;
			char ll[max(3, c/4)+1];
			snprintf(ll, max(3, c/4)+1, "%s", inp.left.data);
			char *llh=highlight(ll);
			char *lrh=highlight(inp.left.data+torem+max(3, c/4));
			char rl[width-c-2-max(3, (width-c)/4)];
			snprintf(rl, width-c-2-max(3, (width-c)/4), "%s", inp.right.data);
			char *rlh=highlight(rl);
			char *rrh=highlight(inp.right.data+inp.right.i-max(3, (width-c)/4));
			printf("%s...%s" SAVEPOS "%s...%s" CLR RESTPOS, llh, lrh, rlh, rrh);
			free(llh);
			free(lrh);
			free(rlh);
			free(rrh);
		}
	}
	fflush(stdout);
}

char *highlight(char *src)
{
	int l,i;char *rv;
	init_char(&rv, &l, &i);
	while(*src)
	{
		if(*src=='\\')
		{
			switch(src[1])
			{
				case 'n':
					s_setcol(7, 0, 1, 0, &rv, &l, &i);
					append_char(&rv, &l, &i, '\\');
					append_char(&rv, &l, &i, 'n');
					s_setcol(7, 0, 0, 0, &rv, &l, &i);
					src++;
				break;
				case 'r':
					s_setcol(7, 0, 1, 0, &rv, &l, &i);
					append_char(&rv, &l, &i, '\\');
					append_char(&rv, &l, &i, 'r');
					s_setcol(7, 0, 0, 0, &rv, &l, &i);
					src++;
				break;
				case '\\':
					s_setcol(3, 0, 1, 0, &rv, &l, &i);
					append_char(&rv, &l, &i, '\\');
					append_char(&rv, &l, &i, '\\');
					s_setcol(7, 0, 0, 0, &rv, &l, &i);
					src++;
				break;
				case 0:
					s_setcol(4, 0, 1, 0, &rv, &l, &i);
					append_char(&rv, &l, &i, '\\');
					s_setcol(7, 0, 0, 0, &rv, &l, &i);
				break;
				case '0':
				case '1':
				case '2':
				case '3':
					s_setcol(6, 0, 1, 0, &rv, &l, &i);
					append_char(&rv, &l, &i, '\\');
					append_char(&rv, &l, &i, *++src);
					int digits=0;
					while(isdigit(src[1])&&(src[1]<'8')&&(++digits<3))
					{
						append_char(&rv, &l, &i, *++src);
					}
					s_setcol(7, 0, 0, 0, &rv, &l, &i);
				break;
				default:
					s_setcol(1, 0, 1, 0, &rv, &l, &i);
					append_char(&rv, &l, &i, '\\');
					s_setcol(7, 0, 0, 0, &rv, &l, &i);
				break;
			}
		}
		else
		{
			append_char(&rv, &l, &i, *src);
		}
		src++;
	}
	return(rv);
}

int w_buf_print(int buf, colour lc, char *lt, char *lead)
{
	char *ltd=strdup(lt);
	char *out=strdup(lead);
	wordline(ltd, strlen(out), &out, lc);
	int e=buf_print(buf, lc, out);
	free(ltd);
	free(out);
	return(e);
}

int e_buf_print(int buf, colour lc, message pkt, char *lead)
{
	int arg;
	int len=(pkt.prefix?strlen(pkt.prefix):0)+strlen(pkt.cmd)+8;
	for(arg=0;arg<pkt.nargs;arg++)
	{
		len+=strlen(pkt.args[arg])+3;
	}
	char text[len];
	if(pkt.prefix)
	{
		strcpy(text, pkt.prefix);
	}
	else
	{
		*text=0;
	}
	strcat(text, " : ");
	strcat(text, pkt.cmd);
	for(arg=0;arg<pkt.nargs;arg++)
	{
		strcat(text, " _ ");
		strcat(text, pkt.args[arg]);
	}
	return(w_buf_print(buf, lc, text, lead));
}

int transfer_start_buffer(void)
{
	int i,e=0;
	for(i=0;i<s_buf.nlines;i++)
	{
		e|=w_buf_print(0, s_buf.lc[i], s_buf.lt[i], "init: ");
	}
	return(e);
}

int push_buffer(void)
{
	if(!bufs || transfer_start_buffer())
	{
		w_buf_print(0, c_err, "Failed to transfer start_buffer", "init[xsb]: ");
		int i;
		for(i=0;i<s_buf.nlines;i++)
		{
			fprintf(stderr, "init[xsb]: %s\n", s_buf.lt[i]);
		}
		char msg[32];
		sprintf(msg, "%d messages written to stderr", s_buf.nlines);
		w_buf_print(0, c_status, msg, "init[xsb]: ");
	}
	
	if(s_buf.errs)
	{
		char msg[80];
		sprintf(msg, "%d messages were written to stderr due to start_buffer errors", s_buf.errs);
		w_buf_print(0, c_err, msg, "init[errs]: ");
	}
	
	return(free_start_buffer());
}

void titlebar(void)
{
	printf(LOCATE CLA, 1, 1);
	setcol(0, 7, true, false);
	int gits;
	sscanf(VERSION_TXT, "%u", &gits);
	const char *hashgit=strchr(VERSION_TXT, ' ');
	if(hashgit)
		hashgit++;
	char *cserv=strdup(bufs[bufs[cbuf].server].bname?bufs[bufs[cbuf].server].bname:"");
	char *cnick=strdup(bufs[bufs[cbuf].server].nick?bufs[bufs[cbuf].server].nick:"");
	char *cchan=(bufs[cbuf].type==CHANNEL)?strdup(bufs[cbuf].bname?bufs[cbuf].bname:""):strdup("");
	char *topic=(bufs[cbuf].type==CHANNEL)?bufs[cbuf].topic:NULL;
	scrush(&cserv, 16);
	crush(&cnick, 16);
	crush(&cchan, 16);
	char tbar[width];
	int wleft=width-1;
	bool use[8]; // #chan	nick	quIRC	version	gits	ghashgit	server	topic...
	char version[32];
	sprintf(version, "%hhu.%hhu.%hhu", VERSION_MAJ, VERSION_MIN, VERSION_REV);
	char vgits[8];
	sprintf(vgits, "%hhu", gits);
	memset(use, 0, sizeof(bool[8]));
	if(*cchan && (wleft>=strlen(cchan)+1))
	{
		use[0]=true;
		wleft-=strlen(cchan)+1;
	}
	if(*cnick && (wleft>=strlen(cnick)+1))
	{
		use[1]=true;
		wleft-=strlen(cnick)+1;
	}
	if(wleft>=6)
	{
		use[2]=true;
		wleft-=6;
	}
	if(wleft>=strlen(version)+1)
	{
		use[3]=true;
		wleft-=strlen(version)+1;
	}
	if(VERSION_TXT[0] && gits && use[3] && (wleft>=strlen(vgits)+1))
	{
		use[4]=true;
		wleft-=strlen(vgits)+1;
	}
	if(hashgit && use[4] && (wleft>=strlen(hashgit)+1))
	{
		use[5]=true;
		wleft-=strlen(hashgit)+1;
	}
	if(*cserv && (wleft>=strlen(cserv)+1))
	{
		use[6]=true;
		wleft-=strlen(cserv)+1;
	}
	else if(*cserv && wleft>1)
	{
		use[6]=true;
		scrush(&cserv, wleft-1);
		wleft-=strlen(cserv)+1;
	}
	if(topic && (wleft>1))
	{
		use[7]=true;
	}
	int i;
	for(i=0;i<width;i++)
		tbar[i]='-';
	tbar[width]=0;
	int p=1;
	// 2quIRC 3version 4gits 5ghashgit 6server 0chan 1nick 7topic
	// 0#chan	1nick	2quIRC	3version	4gits	5ghashgit	6server	7topic...
	if(use[2])
	{
		memcpy(tbar+p, "quIRC", 5);
		p+=6;
	}
	if(use[3])
	{
		memcpy(tbar+p, version, strlen(version));
		p+=strlen(version)+1;
	}
	if(use[4])
	{
		memcpy(tbar+p, vgits, strlen(vgits));
		p+=strlen(vgits)+1;
	}
	if(use[5])
	{
		memcpy(tbar+p, hashgit, strlen(hashgit));
		p+=strlen(hashgit)+1;
	}
	if(use[6])
	{
		memcpy(tbar+p, cserv, strlen(cserv));
		p+=strlen(cserv)+1;
	}
	if(use[0])
	{
		memcpy(tbar+p, cchan, strlen(cchan));
		p+=strlen(cchan)+1;
	}
	if(use[1])
	{
		memcpy(tbar+p, cnick, strlen(cnick));
		p+=strlen(cnick)+1;
	}
	if(use[7])
	{
		memcpy(tbar+p, topic, min(wleft-1, strlen(topic)));
	}
	printf("%s", tbar);
	free(cserv);
	free(cchan);
	free(cnick);
	resetcol();
	printf("\n");
	printf(LOCATE, height-1, 1);
}

int findptab(int b, char *src)
{
	int b2;
	for(b2=0;b2<nbufs;b2++)
	{
		if((bufs[b2].server==b)&&(bufs[b2].type==PRIVATE)&&(irc_strcasecmp(bufs[b2].bname, src, bufs[b].casemapping)==0))
			return(b2);
	}
	return(-1);
}
