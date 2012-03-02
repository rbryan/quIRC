/* Generated by genconfig */
int conf_check(void)
{
	if(width<30)
	{
		atr_failsafe(&s_buf, STA, "width set to minimum 30", "init: ");
		width=30;
	}
	if(height<5)
	{
		atr_failsafe(&s_buf, STA, "height set to minimum 5", "init: ");
		height=5;
	}
	if(mirc_colour_compat>2)
	{
		atr_failsafe(&s_buf, STA, "mcc set to maximum 2", "init: ");
		mirc_colour_compat=2;
	}
	if(force_redraw>3)
	{
		atr_failsafe(&s_buf, STA, "fred set to maximum 3", "init: ");
		force_redraw=3;
	}
	if(buflines<32)
	{
		atr_failsafe(&s_buf, STA, "buf set to minimum 32", "init: ");
		buflines=32;
	}
	if(maxnlen<4)
	{
		atr_failsafe(&s_buf, STA, "mnln set to minimum 4", "init: ");
		maxnlen=4;
	}
	if(tping<15)
	{
		atr_failsafe(&s_buf, STA, "tping set to minimum 15", "init: ");
		tping=15;
	}
	if(ts>6)
	{
		atr_failsafe(&s_buf, STA, "ts set to maximum 6", "init: ");
		ts=6;
	}
	return(0);
}
