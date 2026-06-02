#ifndef RTX_h
#define RTX_h

#include "Arduino.h"

#define N_MAX_CHANNEL 2

class RTX
{
  public:
    RTX();
	
	//accessor methods
	int getVfo(int vfoId);
	void setVfo(int vfoId, int value);
	int getIf(int vfoId);
	void setIf(int vfoId, int value);
	bool getRxChannelEnable(int vfoId);
	void setRxChannelEnable(int vfoId, bool value);
	bool isTxFootswitch();
	void setTxFootswitch(bool value);
	
	int getDds();
	void setDds(int value);
	int getRitOffset();
	void setRitOffset(int value);
	int getXitOffset();
	void setXitOffset(int value);
	int getSqlLevel();
	void setSqlLevel(int value);
	bool getRxEnable();
	void setRxEnable(bool value);
	bool getTxEnable();
	void setTxEnable(bool value);
	bool getTrx();
	void setTrx(bool value);	
	bool getTune();
	void setTune(bool value);
	bool getRitEnable();
	void setRitEnable(bool value);
	bool getXitEnable();
	void setXitEnable(bool value);
	bool getSplitEnable();
	void setSplitEnable(bool value);
	bool getSqlEnable();
	void setSqlEnable(bool value);
	bool getRxMute();
	void setRxMute(bool value);
	bool isIqStarted();
	void iqStart();
	void iqStop();
	bool isAudioStarted();
	void audioStart();
	void audioStop();
	bool isLineOutStarted();
	void lineOutStart();
	void lineOutStop();
	bool isLineOutRecorderStarted();
	void lineOutRecorderStart();
	void setRecordDuration(int value);
	int getRecordDuration();
	void setRecordFileName(const char *value);
	char* getRecordFileName();
	void setRxFilterLower(int value);
	int getRxFilterLower();
	void setRxFilterTop(int value);
	int getRxFilterTop();
	void setModulation(const char *value);
	char* getModulation();
	int getDrive();
	void setDrive(int value);
	int getTuneDrive();
	void setTuneDrive(int value);
	int getRxVolume(int vfoId);
	void setRxVolume(int vfoId, int value);
	int getRxBalance(int vfoId);
	void setRxBalance(int vfoId, int value);
	void setRxSmeterEnabled(bool value);
	bool isRxSmeterEnabled();
	void setSmeter(int vfoId, float value);
	float getSmeter(int vfoId);
	void getCatResponse(char *result, char *cmd);
	int getBandId(int iaruRegion);
	void setAgcMode(int value);
	int getAgcMode();
	void setAgcGain(int value);
	int getAgcGain();
	void setTriggeringThresold(int value);
	int getTriggeringThresold();
	void setPulseDuration(int value);
	int getPulseDuration();
	void setRxNbEnable(bool value);
	bool isRxNbEnable();
	void setRxBinEnable(bool value);
	bool isRxBinEnable();
	void setRxNrEnable(bool value);
	bool isRxNrEnable();
	void setRxAncEnable(bool value);
	bool isRxAncEnable();
	void setRxAnfEnable(bool value);
	bool isRxAnfEnable();
	void setRxApfEnable(bool value);
	bool isRxApfEnable();
	void setRxDseEnable(bool value);
	bool isRxDseEnable();
	void setRxNfEnable(bool value);
	bool isRxNfEnable();
	void setLock(bool value);
	bool isLock();
	void setCwKeyPressed(bool value);
	bool isCwKeyPressed();
	bool autoInformationEnabled();
	
  private:
	int vfo[N_MAX_CHANNEL];
	int _if[N_MAX_CHANNEL];
	bool rx_channel_enable[N_MAX_CHANNEL];
	int dds;
	int rit_offset;
	int xit_offset;
	int sql_level;
	bool rx_enable;
	bool tx_enable;
	bool trx;
	bool tune;
	bool rit_enable;
	bool xit_enable;
	bool split_enable;
	bool sql_enable;
	bool rx_mute;
	bool iq_started;
	bool audio_started;
	bool line_out_started;	
	int record_duration;
	char record_filename[128];
	bool tx_footswitch;
	int rx_filter_band_lower;
	int rx_filter_band_top;
	char modulation[10];	
	bool rx_smeter;
	float smeter[N_MAX_CHANNEL];
	int drive;
	int tune_drive;
	int agc_mode;
	int rx_volume[N_MAX_CHANNEL];
	int rx_balance[N_MAX_CHANNEL];
	int agc_gain;
	int triggering_thresold;
	int pulse_duration;	
	bool rx_nb_enable;
	bool rx_bin_enable;
	bool rx_nr_enable;
	bool rx_anc_enable;
	bool rx_anf_enable;
	bool rx_apf_enable;
	bool rx_dse_enable;
	bool rx_nf_enable;
	bool lock;
	bool cw_key_pressed;	
	bool auto_information;
	
	//BAND definitions
	int REG_MIN[3][12] = { {1810000, 3500000, 5352000, 7000000, 10000000, 14000000, 18068000, 21000000, 24890000, 28000000, 50000000, 144000000},
                           {1800000, 3500000, 5352000, 7000000, 10000000, 14000000, 18068000, 21000000, 24890000, 28000000, 50000000, 144000000},
					       {1800000, 3500000, 5352000, 7000000, 10000000, 14000000, 18068000, 21000000, 24890000, 28000000, 50000000, 144000000}	 
					     };
	
	int REG_MAX[3][12] = { {2000000, 3800000, 5367000, 7200000, 10150000, 14350000, 18168000, 21450000, 24990000, 29700000, 54000000, 146000000},
                           {2000000, 4000000, 5367000, 7300000, 10150000, 14350000, 18168000, 21450000, 24990000, 29700000, 54000000, 148000000},
					       {2000000, 3900000, 5367000, 7300000, 10150000, 14350000, 18168000, 21450000, 24990000, 29700000, 54000000, 148000000} 
					     };
	
	//CAT features
	boolean AIenabled=false;
	void doFA(char *result);
	void doFB(char *result);
	void doFR(char *result);
	void doFT(char *result);
	void doIF(char *result);
	void doMD(char *result);		
	int getMD();
};

#endif