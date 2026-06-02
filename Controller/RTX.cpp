#include "RTX.h"

RTX::RTX() {

}
	
int RTX::getVfo(int vfoId) {
	return vfo[vfoId];
}

void RTX::setVfo(int vfoId, int value) {
	if (vfoId>=0 && vfoId<N_MAX_CHANNEL) 
		vfo[vfoId] = value;
}

int RTX::getIf(int vfoId) {
	return _if[vfoId];
}

void RTX::setIf(int vfoId, int value) {
	if (vfoId>=0 && vfoId<N_MAX_CHANNEL) 
		_if[vfoId] = value;
}

void RTX::setRxFilterLower(int value) {
		rx_filter_band_lower = value;
}

int RTX::getRxFilterLower() {
	return rx_filter_band_lower;
}

void RTX::setRxFilterTop(int value){
	rx_filter_band_top = value;
}

int RTX::getRxFilterTop() {
	return rx_filter_band_top;
}

bool RTX::getRxChannelEnable(int vfoId) {
	return rx_channel_enable[vfoId];
}

void RTX::setRxChannelEnable(int vfoId, bool value) {	
	rx_channel_enable[vfoId] = value;
}

bool RTX::isTxFootswitch() {
	return tx_footswitch;
}

void RTX::setTxFootswitch(bool value) {	
	tx_footswitch = value;
}

int RTX::getDds() {
	return dds;
}

void RTX::setDds(int value) {
	dds = value;
}
	
int RTX::getRitOffset() {
	return rit_offset;
}

void RTX::setRitOffset(int value) {
	rit_offset = value;
}

int RTX::getXitOffset() {
	return xit_offset;
}

void RTX::setXitOffset(int value) {
	xit_offset = value;
}	

int RTX::getSqlLevel() {
	return sql_level;
}

void RTX::setSqlLevel(int value) {
	sql_level = value;
}

bool RTX::getRxEnable() {
	return rx_enable;
}

void RTX::setRxEnable(bool value) {
	rx_enable = value;
}

bool RTX::getTxEnable() {
	return tx_enable;
}

void RTX::setTxEnable(bool value) {
	tx_enable = value;
}

bool RTX::getTrx() {
	return trx;
}

void RTX::setTrx(bool value) {
	trx = value;
}

bool RTX::getTune() {
	return tune;
}

void RTX::setTune(bool value) {
	tune = value;
}

bool RTX::getRitEnable() {
	return rit_enable;
}

void RTX::setRitEnable(bool value) {
	rit_enable = value;
}

bool RTX::getXitEnable() {
	return xit_enable;
}

void RTX::setXitEnable(bool value) {
	xit_enable = value;
}

bool RTX::getSplitEnable() {
	return split_enable;
}

void RTX::setSplitEnable(bool value) {
	split_enable = value;
}

bool RTX::getSqlEnable() {
	return sql_enable;
}

void RTX::setSqlEnable(bool value) {
	sql_enable = value;
}

bool RTX::getRxMute() {
	return rx_mute;
}

void RTX::setRxMute(bool value) {
	rx_mute = value;
}

bool RTX::isIqStarted() {
	return iq_started;
}

void RTX::iqStart() {
	iq_started = true;
}

void RTX::iqStop() {
	iq_started = false;
}

bool RTX::isAudioStarted() {
	return audio_started;
}

void RTX::audioStart() {
	audio_started = true;
}

void RTX::audioStop() {
	audio_started = false;
}

bool RTX::isLineOutStarted() {
	return line_out_started;
}

void RTX::lineOutStart() {
	line_out_started = true;
}

void RTX::lineOutStop() {
	line_out_started = false;
}

void RTX::setRecordDuration(int value) {
	record_duration = value;
}

int RTX::getRecordDuration() {
	return record_duration;
}

void RTX::setRecordFileName(const char *value) {
	memset(record_filename, 0, sizeof(record_filename));
	strcpy(record_filename, value);
}
	
char *RTX::getRecordFileName() {
	return record_filename;
}

void RTX::setModulation(const char *value) {
	memset(modulation, 0, sizeof(modulation));
	strcpy(modulation, value);
}
	
char *RTX::getModulation() {
	return modulation;
}

int RTX::getDrive() {
	return drive;
}

void RTX::setDrive(int value) {
	drive = value;
}

int RTX::getTuneDrive() {
	return tune_drive;
}

void RTX::setTuneDrive(int value) {
	tune_drive = value;
}

int RTX::getRxVolume(int vfoId) {
	return rx_volume[vfoId];
}

void RTX::setRxVolume(int vfoId, int value) {
	rx_volume[vfoId] = value;
}

int RTX::getRxBalance(int vfoId) {
	return rx_balance[vfoId];
}

void RTX::setRxBalance(int vfoId, int value) {
	rx_balance[vfoId] = value;
}

void RTX::setRxSmeterEnabled(bool value) {
	rx_smeter = value;
}

bool RTX::isRxSmeterEnabled() {
	return rx_smeter;
}
	
void RTX::setSmeter(int vfoId, float value) {
	if (vfoId>=0 && vfoId<N_MAX_CHANNEL) 
		smeter[vfoId] = value;
}

float RTX::getSmeter(int vfoId) {
	return smeter[vfoId];
}

void RTX::setAgcMode(int value) {
	agc_mode = value;
}

int RTX::getAgcMode() {
	return agc_mode;
}

void RTX::setAgcGain(int value) {
	agc_gain = value;
}

int RTX::getAgcGain() {
	return agc_gain;
}

void RTX::setTriggeringThresold(int value) {
	triggering_thresold = value;
}

int RTX::getTriggeringThresold() {
	return triggering_thresold;
}

void RTX::setPulseDuration(int value) {
	pulse_duration = value;
}

int RTX::getPulseDuration() {
	return pulse_duration;
}

void RTX::setRxNbEnable(bool value) {
	rx_nb_enable = value;
}

bool RTX::isRxNbEnable() {
	return rx_nb_enable;
}

void RTX::setRxBinEnable(bool value) {
	rx_bin_enable = value;
}

bool RTX::isRxBinEnable() {
	return rx_bin_enable;
}

void RTX::setRxNrEnable(bool value) {
	rx_nr_enable = value;
}

bool RTX::isRxNrEnable() {
	return rx_nr_enable;
}

void RTX::setRxAncEnable(bool value) {
	rx_anc_enable = value;
}

bool RTX::isRxAncEnable() {
	return rx_anc_enable;
}

void RTX::setRxAnfEnable(bool value) {
	rx_anf_enable = value;
}

bool RTX::isRxAnfEnable() {
	return rx_anf_enable;
}

void RTX::setRxApfEnable(bool value) {
	rx_apf_enable = value;
}

bool RTX::isRxApfEnable() {
	return rx_apf_enable;
}

void RTX::setRxDseEnable(bool value) {
	rx_dse_enable = value;
}

bool RTX::isRxDseEnable() {
	return rx_dse_enable;
}

void RTX::setRxNfEnable(bool value) {
	rx_nf_enable = value;
}

bool RTX::isRxNfEnable() {
	return rx_nf_enable;
}

void RTX::setLock(bool value) {
	lock = value;
}

bool RTX::isLock() {
	return lock;
}

void RTX::setCwKeyPressed(bool value) {
	cw_key_pressed = value;
}

bool RTX::isCwKeyPressed() {
	return cw_key_pressed;
}

bool RTX::autoInformationEnabled() {
	return auto_information;
}

/*
 * ==============================================================================
 */

int RTX::getBandId(int iaruRegion) {   // -1 = wrong region, 0=GEN, 1=160m,
	int res = -1;
	if (!(iaruRegion>=0 && iaruRegion<=3))
		return res;
	res = 0;         //GEN BAND
	int i = 0;	
	while (i < 12) {
		/*Serial.print("  ==> "); Serial.print(getVfo(0));
		Serial.print("["); Serial.print(REG_MIN[iaruRegion-1][i]);
		Serial.print("-"); Serial.print(REG_MAX[iaruRegion-1][i]);
		Serial.println("]");*/
		
		if (getVfo(0) >= REG_MIN[iaruRegion-1][i] && 
		    getVfo(0) <= REG_MAX[iaruRegion-1][i]) {
			res = i+1;
			break;
		}
		else 		
			i++;
	}

	return res;
}


void RTX::getCatResponse(char *result, char *cmd) {	

	if (strcmp(cmd,"FA;") == 0)
		doFA(result);
	else if (strcmp(cmd,"FB;") == 0)
	    doFB(result);
	else if (strcmp(cmd,"MD;") == 0) 		
		sprintf(result,"MD%d;",getMD());	
	else if (strcmp(cmd,"FR;") == 0) 		
		doFR(result);
	else if (strcmp(cmd,"FT;") == 0) 		
		doFT(result);
	else if (strcmp(cmd,"IF;") == 0) 		
		doIF(result);
	else if (strcmp(cmd,"AI;") == 0) {
		(auto_information==true) ? sprintf(result,"AI2;") : sprintf(result,"AI0;");
	} else if (strcmp(cmd,"AI1;") == 0) {
		sprintf(result,"AI2;"); 
		//Only extended AI format is ON		
	} else if (strcmp(cmd,"AI2;") == 0 ) {
		auto_information = true;
		sprintf(result,"AI2;");
 	} else if (strcmp(cmd,"AI0;") == 0 ) {
		auto_information = false;
		sprintf(result,"AI0;");
	 } else
		sprintf(result,"?;");

}


void RTX::doFA(char *result) {
	sprintf(result,"FA%011d;",getVfo(0));
}


void RTX::doFB(char *result) {
	sprintf(result,"FB%011d;",getVfo(1));
}


void RTX::doFR(char *result) {
	int fr=0;
	if (getRxChannelEnable(1))   //if VFOB is enabled
		fr=1;	
	sprintf(result,"FR%01d;",fr);
}


void RTX::doFT(char *result) {
	int ft=0;
	if (getSplitEnable())   //if VFOB is enabled and TX is on VFOB
		ft=1;
	sprintf(result,"FT%01d;",ft);
}


void RTX::doMD(char *result) {
	sprintf(result,"MD%d;",getMD());
}


int RTX::getMD() {
	int res = 0;
	//am,sam,dsb,lsb,usb,cw,nfm,digl,digu,wfm,drm
	
	char buffer[5];
	sprintf(buffer,getModulation());        

	if (strcmp(buffer, "lsb") == 0)
		res = 1;
	else if (strcmp(buffer, "usb") == 0)
		res = 2;
	else if (strcmp(buffer, "cw") == 0)
		res = 3;
	else if ((strcmp(buffer, "nfm") == 0) ||
			 (strcmp(buffer, "nfm") == 0))
		res = 4;
	else if ((strcmp(buffer, "am") == 0) ||
			 (strcmp(buffer, "sam") == 0) ||
			 (strcmp(buffer, "dsb") == 0))
		res = 5;
	else if (strcmp(buffer, "digl") == 0)
		res = 6;	
	else if (strcmp(buffer, "digu") == 0)
		res = 9;	
	else if (strcmp(buffer, "drm") == 0)
		res = 0;	
		
	return res;
}

/*
 * The IF command is also sent when the AI (auto information) mode is enable
 */
void RTX::doIF(char *result ) {  
    //<1>  <2>  <3> <4> <5> <6> <8> <9> <10><11><12><13>
    sprintf(result,
	        "IF%011d%05d%+05d%01d%01d%03d%01d%01d%01d%01d%01d%04d;",
            getVfo(0),                //p1
			0,                        //p2
			getRitOffset(),           //p3
			getRitEnable(),           //p4
			getXitEnable(),           //p5
			0,                        //p6-7
			getTrx(),                 //p8
			getMD(),                  //p9
			getRxChannelEnable(1),    //p10
			0,                        //p11
			getSplitEnable(),         //p12
			0);                       //p13-14-15
}
