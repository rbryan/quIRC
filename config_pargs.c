/* Generated by genconfig */
		else if(strncmp(argv[arg], "--width=", 8)==0)
			sscanf(argv[arg]+8, "%u", &width);
		else if(strncmp(argv[arg], "--height=", 9)==0)
			sscanf(argv[arg]+9, "%u", &height);
		else if(strncmp(argv[arg], "--mcc=", 6)==0)
			sscanf(argv[arg]+6, "%u", &mirc_colour_compat);
		else if(strncmp(argv[arg], "--fred=", 7)==0)
			sscanf(argv[arg]+7, "%u", &force_redraw);
		else if(strncmp(argv[arg], "--buf-lines=", 12)==0)
			sscanf(argv[arg]+12, "%u", &buflines);
		else if(strncmp(argv[arg], "--maxnicklen=", 13)==0)
			sscanf(argv[arg]+13, "%u", &maxnlen);
		else if(strcmp(argv[arg], "--fwc")==0)
			full_width_colour=true;
		else if(strcmp(argv[arg], "--no-fwc")==0)
			full_width_colour=false;
		else if(strcmp(argv[arg], "--hts")==0)
			hilite_tabstrip=true;
		else if(strcmp(argv[arg], "--no-hts")==0)
			hilite_tabstrip=false;
		else if(strcmp(argv[arg], "--tsb")==0)
			tsb=true;
		else if(strcmp(argv[arg], "--no-tsb")==0)
			tsb=false;
		else if(strncmp(argv[arg], "--tping=", 8)==0)
			sscanf(argv[arg]+8, "%u", &tping);
